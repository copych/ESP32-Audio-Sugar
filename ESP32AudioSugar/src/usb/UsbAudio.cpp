#include "UsbAudio.h"
#if SUGAR_USB_AUDIO
#include "PcmRing.h"
#include "UsbFpuBind.h"
#include "USB.h"
#include "USBAudioCard.h"
#include "esp_log.h"
#include "esp_cpu.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This capture integration is ESP32-S3 only; disable SUGAR_USB_AUDIO on other targets."
#endif
// Arduino USB.cpp exposes these helpers for its class wrappers. They target
// the separate Arduino USB event loop, not the general Arduino event task.
esp_err_t arduino_usb_event_post(esp_event_base_t, int32_t, void*, size_t, TickType_t);
esp_err_t arduino_usb_event_handler_register_with(esp_event_base_t, int32_t,
                                                esp_event_handler_t, void*);
#if !CONFIG_TINYUSB_AUDIO_ENABLED
#error "TinyUSB Audio must be enabled (Arduino-ESP32 3.3.11/3.3.12)."
#endif
#if ARDUINO_RUNNING_CORE != 1 || ARDUINO_EVENT_RUNNING_CORE != 1
#error "Set Arduino Runs On and Events Run On to Core 1."
#endif
#if ARDUINO_USB_MODE != 0 || ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Use USB-OTG (TinyUSB); disable USB CDC/MSC/DFU On Boot."
#endif
static_assert(SUGAR_SAMPLE_BYTES == 2, "USB capture requires stereo PCM16");
static_assert(SUGAR_SAMPLE_RATE == 44100, "This first integration targets 44100 Hz");
static_assert(SUGAR_BLOCK_FRAMES > 0 && SUGAR_BLOCK_FRAMES <= 512, "Unsupported block size");

namespace SugarUSB {
static_assert(PcmRing::CAPACITY >= SUGAR_BLOCK_FRAMES * 2U, "Ring must hold two audio blocks");
static const char* TAG = "SugarUSB";
static DRAM_ATTR PcmRing ring;
static DRAM_ATTR SharedWord accepting;
static DRAM_ATTR SharedWord hostCapture;
static DRAM_ATTR SharedWord suspended;
static DRAM_ATTR SharedWord hostRate;
static DRAM_ATTR SharedWord overflowBlocks;
static DRAM_ATTR SharedWord tapMaxCycles;
static USBAudioCard uac(SUGAR_SAMPLE_RATE, UAC_BPS_16, UAC_SPK_NONE, UAC_MIC_STEREO);
static TaskHandle_t feederTask = nullptr;
static TaskHandle_t usbEventTask = nullptr;
static USB_FPU::BindResult eventBinding;
ESP_EVENT_DEFINE_BASE(SugarUSB_BIND_EVENTS);

// Feeder publishes, existing MIDI task prints. A bounded version check avoids
// waiting or taking a lock if publication happened during a control-side read.
struct DiagnosticWords {
    SharedWord version, capture, rate, bytesPerSecond, frames, high;
    SharedWord drops, empty, fifoFull, tapCycles;
};
static DiagnosticWords diagnostics;

void reportIfDue() {
    static uint32_t lastVersion = 0;
    const uint32_t version = diagnostics.version.load();
    if (!version || (version & 1U) || version == lastVersion) return;
    const uint32_t capture = diagnostics.capture.load();
    const uint32_t rate = diagnostics.rate.load();
    const uint32_t bytesPerSecond = diagnostics.bytesPerSecond.load();
    const uint32_t frames = diagnostics.frames.load();
    const uint32_t high = diagnostics.high.load();
    const uint32_t drops = diagnostics.drops.load();
    const uint32_t empty = diagnostics.empty.load();
    const uint32_t fifoFull = diagnostics.fifoFull.load();
    const uint32_t tapCycles = diagnostics.tapCycles.load();
    if (diagnostics.version.load() != version) return;
    lastVersion = version;
    ESP_LOGI(TAG,
        "capture=%u rate=%lu B/s=%lu ring=%lu hi=%lu dropBlocks=%lu empty=%lu fifoFull=%lu tapMaxCycles=%lu",
        unsigned(capture), (unsigned long)rate, (unsigned long)bytesPerSecond,
        (unsigned long)frames, (unsigned long)high, (unsigned long)drops,
        (unsigned long)empty, (unsigned long)fifoFull, (unsigned long)tapCycles);
    USB_FPU::report();
    ESP_LOGI(TAG, "USB events affinity=%s",
             usbEventTask ? USB_FPU::affinityName(xTaskGetCoreID(usbEventTask)) : "MISSING");
}

static void bindEventCallback(void*, esp_event_base_t, int32_t, void*) {
    // Same one-time mechanism tested in usbd, now in the USB event task.
    if (xPortInIsrContext()) return;
    eventBinding.actual = xTaskGetCurrentTaskHandle();
    if (eventBinding.actual == eventBinding.expected) {
        vTaskSuspendAll();
        eventBinding.core = xPortGetCoreID();
        eventBinding.before = xTaskGetCoreID(eventBinding.actual);
        if (eventBinding.core == 1 && eventBinding.before == tskNO_AFFINITY) {
            USB_FPU::touchFpu();
            eventBinding.usedFpu = true;
        }
        eventBinding.after = xTaskGetCoreID(eventBinding.actual);
        xTaskResumeAll();
    }
    eventBinding.done.store(true, std::memory_order_release);
}

static bool bindUsbEventTask() {
    // FreeRTOS truncates task names; look up the same truncated spelling.
    char name[configMAX_TASK_NAME_LEN];
    strncpy(name, "arduino_usb_events", sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    usbEventTask = xTaskGetHandle(name);
    if (!usbEventTask) {
        ESP_LOGE(TAG, "Arduino USB event task not found");
        return false;
    }
    const BaseType_t initial = xTaskGetCoreID(usbEventTask);
    if (initial == 1) return true;
    if (initial != tskNO_AFFINITY) {
        ESP_LOGE(TAG, "USB event task already pinned away from Core1");
        return false;
    }
    eventBinding.expected = usbEventTask;
    const esp_err_t registered = arduino_usb_event_handler_register_with(
        SugarUSB_BIND_EVENTS, 1, bindEventCallback, nullptr);
    if (registered != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register USB event binding callback");
        return false;
    }
    for (unsigned attempt = 1; attempt <= 8; ++attempt) {
        eventBinding.actual = nullptr;
        eventBinding.core = -1;
        eventBinding.usedFpu = false;
        eventBinding.done.store(false, std::memory_order_relaxed);
        if (arduino_usb_event_post(SugarUSB_BIND_EVENTS, 1, nullptr, 0,
                                   pdMS_TO_TICKS(100)) != ESP_OK) {
            ESP_LOGE(TAG, "Cannot post USB event binding request");
            return false;
        }
        const uint32_t started = millis();
        while (!eventBinding.done.load(std::memory_order_acquire)) {
            if (uint32_t(millis() - started) >= 1000) {
                ESP_LOGE(TAG, "USB event binding timed out");
                return false;
            }
            vTaskDelay(1);
        }
        if (eventBinding.actual != usbEventTask) return false;
        ESP_LOGI(TAG, "Event bind %u: core=%d before=%s after=%s fpu=%u",
                 attempt, int(eventBinding.core),
                 USB_FPU::affinityName(eventBinding.before),
                 USB_FPU::affinityName(eventBinding.after),
                 unsigned(eventBinding.usedFpu));
        if (eventBinding.after == 1) return true;
        if (eventBinding.after != tskNO_AFFINITY) return false;
        vTaskDelay(1);
    }
    return false;
}

static void usbEvent(void*, esp_event_base_t, int32_t id, void*) {
    if (id == ARDUINO_USB_STOPPED_EVENT) hostCapture.store(0);
    if (id == ARDUINO_USB_SUSPEND_EVENT) suspended.store(1);
    if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT)
        suspended.store(0);
}

static void audioEvent(void*, esp_event_base_t, int32_t id, void* payload) {
    if (!payload) return;
    const auto* event = static_cast<arduino_usb_audio_card_event_data_t*>(payload);
    if (id == ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT &&
        event->interface_enable.interface == UAC_INTERFACE_MIC)
        hostCapture.store(event->interface_enable.enable ? 1U : 0U);
    else if (id == ARDUINO_USB_AUDIO_CARD_SAMPLE_RATE_EVENT)
        hostRate.store(event->sample_rate.rate);
}

void IRAM_ATTR pushPcm16(const int16_t* pcm, size_t frames) {
    if (!accepting.load()) return;
    const uint32_t beginCycles = esp_cpu_get_cycle_count();
    if (!ring.push(pcm, uint32_t(frames)))
        overflowBlocks.store(overflowBlocks.load() + 1);
    const uint32_t elapsed = esp_cpu_get_cycle_count() - beginCycles;
    if (elapsed > tapMaxCycles.load()) tapMaxCycles.store(elapsed);
}

static void feedTask(void*) {
    // Core 1 owns the staging buffer and all USBAudioCard.write calls.
    int16_t staging[SUGAR_BLOCK_FRAMES * 2];
    uint16_t pending = 0, offset = 0;
    bool running = false, primed = false, fault = false;
    uint32_t emptyEpisodes = 0, fifoFullAttempts = 0, txBytes = 0;
    uint32_t maxRingFrames = 0, lastReport = millis();
    bool wasEmpty = false;
    for (;;) {
        const bool wanted = bool(USB) && !suspended.load() && hostCapture.load() &&
                            hostRate.load() == SUGAR_SAMPLE_RATE && !fault;
        if (!wanted) {
            accepting.store(0);
            ring.discard();
            pending = offset = 0;
            running = primed = wasEmpty = false;
        } else {
            if (!running) {
                ring.discard();
                accepting.store(1);
                running = true;
            }
            const uint32_t available = ring.available();
            if (available > maxRingFrames) maxRingFrames = available;
            if (!primed && available >= SUGAR_BLOCK_FRAMES * 2U) primed = true;
            if (primed) {
                // Feed real I2S-produced samples. Do not synthesize a separate
                // 44.1 kHz micros()-paced clock or discard/repeat samples to
                // force a nominal rate. TinyUSB owns IN packet sizing.
                for (unsigned attempt = 0; attempt < 4; ++attempt) {
                    if (offset == pending) {
                        const uint32_t frames = ring.pop(staging, SUGAR_BLOCK_FRAMES);
                        pending = uint16_t(frames * 4);
                        offset = 0;
                        if (!pending) {
                            if (!wasEmpty) ++emptyEpisodes;
                            wasEmpty = true;
                            break;
                        }
                        wasEmpty = false;
                    }
                    const uint16_t n = uac.write(
                        reinterpret_cast<const uint8_t*>(staging) + offset,
                        pending - offset);
                    if (n > pending - offset || (n & 3U)) {
                        fault = true;
                        accepting.store(0);
                        ESP_LOGE(TAG, "USB FIFO returned non-frame-aligned length %u; capture stopped", n);
                        break;
                    }
                    offset += n;
                    txBytes += n;
                    if (!n) { ++fifoFullAttempts; break; }
                }
            }
        }
        const uint32_t now = millis();
        if (uint32_t(now - lastReport) >= 5000) {
            const uint32_t elapsed = now - lastReport;
            const uint32_t version = diagnostics.version.load();
            diagnostics.version.store(version + 1); // Publication in progress.
            diagnostics.capture.store(wanted ? 1U : 0U);
            diagnostics.rate.store(hostRate.load());
            diagnostics.bytesPerSecond.store(uint32_t(uint64_t(txBytes) * 1000 / elapsed));
            diagnostics.frames.store(ring.available());
            diagnostics.high.store(maxRingFrames);
            diagnostics.drops.store(overflowBlocks.load());
            diagnostics.empty.store(emptyEpisodes);
            diagnostics.fifoFull.store(fifoFullAttempts);
            diagnostics.tapCycles.store(tapMaxCycles.load());
            diagnostics.version.store(version + 2); // Complete snapshot.
            txBytes = maxRingFrames = 0;
            lastReport = now;
        }
        vTaskDelay(1); // USB service on Core 1, never inside PCM render/copy.
    }
}

bool prepare() {
    if (xPortGetCoreID() != 1) return false;
    hostRate.store(SUGAR_SAMPLE_RATE);
    USB.onEvent(usbEvent);
    uac.onEvent(audioEvent);
    return uac.begin(); // Microphone-only path creates no speaker task/buffer.
}

bool start() {
    // Bind both USB tasks before creating the feeder or the Core 0 renderer.
    // These startup calls are made by setup() on Core 1, not by an ISR.
    if (!USB_FPU::bind() || !bindUsbEventTask()) {
        ESP_LOGE(TAG, "FPU binding failed; Core0 audio startup refused");
        return false;
    }
    TaskHandle_t usbd = xTaskGetHandle("usbd");
    if (!usbd || xTaskGetCoreID(usbd) != 1 ||
        !usbEventTask || xTaskGetCoreID(usbEventTask) != 1) {
        ESP_LOGE(TAG, "USB task affinity verification failed");
        return false;
    }
    if (feederTask) return true;
    if (xTaskCreatePinnedToCore(feedTask, "usb_audio", 4096, nullptr,
                               7, &feederTask, 1) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create USB audio feeder");
        return false;
    }
    ESP_LOGI(TAG, "Capture ready: I2S-paced stereo PCM16 %u Hz, ring=%lu frames, Core 1 USB",
             SUGAR_SAMPLE_RATE, (unsigned long)PcmRing::CAPACITY);
    return true;
}
}
#endif




#pragma once
#include <Arduino.h>
#include <atomic>
#include "esp_log.h"
#include "esp32-hal-tinyusb.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "FPU binding test targets ESP32-S3 only."
#endif
#if __has_include("device/usbd_pvt.h")
#include "device/usbd_pvt.h"
#else
#error "Bundled TinyUSB private deferred-function API is unavailable."
#endif

namespace USB_FPU {
static const char* TAG = "USB_FPU";

struct BindResult {
    TaskHandle_t expected = nullptr;
    TaskHandle_t actual = nullptr;
    BaseType_t core = -1;
    BaseType_t before = tskNO_AFFINITY;
    BaseType_t after = tskNO_AFFINITY;
    bool usedFpu = false;
    std::atomic<bool> done{false};
};
// Static lifetime: remains valid even if a deferred call completes after timeout.
static BindResult result;

// Separate non-inlined function: no floating-point instructions can be hoisted
// into the caller before its core/context checks. Volatile operands prevent
// constant folding, including under the RDX fast-math build options.
static __attribute__((noinline, noclone)) void touchFpu() {
    volatile float input = 1.25f;
    volatile float output = input + 0.5f;
    (void)output;
}

// Runs through TinyUSB's USBD_EVENT_FUNC_CALL in the existing usbd task.
// No logging, waits, allocations, descriptor overrides or TCB manipulation.
static void deferredBind(void*) {
    if (xPortInIsrContext()) return; // Never perform FPU work in an ISR.
    result.actual = xTaskGetCurrentTaskHandle();
    if (result.actual == result.expected) {
        // Prevent migration between checking the core and first FPU instruction.
        // No blocking operation occurs while this local scheduler is suspended.
        vTaskSuspendAll();
        result.core = xPortGetCoreID();
        result.before = xTaskGetCoreID(result.actual);
        if (result.core == 1 && result.before == tskNO_AFFINITY) {
            touchFpu();
            result.usedFpu = true;
        }
        result.after = xTaskGetCoreID(result.actual);
        xTaskResumeAll();
    }
    // Control/startup synchronization only; no lock-free guarantee is assumed.
    result.done.store(true, std::memory_order_release);
}

static const char* affinityName(BaseType_t value) {
    if (value == tskNO_AFFINITY) return "ANY";
    if (value == 0) return "Core0";
    if (value == 1) return "Core1";
    return "UNKNOWN";
}

// Invoke on Core 1 after USB.begin(), before starting the audio renderer.
static bool bind() {
    if (xPortGetCoreID() != 1) {
        ESP_LOGE(TAG, "Run setup() on Core 1");
        return false;
    }
    result.expected = xTaskGetHandle("usbd");
    if (!result.expected) {
        ESP_LOGE(TAG, "usbd task not found; call bind() after USB.begin()");
        return false;
    }
    const BaseType_t initial = xTaskGetCoreID(result.expected);
    ESP_LOGI(TAG, "Initial usbd affinity=%s", affinityName(initial));
    if (initial == 1) {
        ESP_LOGW(TAG, "Already Core1: FPU binding not demonstrated. Restore stock task creation to test it.");
        return true;
    }
    if (initial != tskNO_AFFINITY) {
        ESP_LOGE(TAG, "Already pinned away from Core1; FPU binding cannot move it");
        return false;
    }
    for (unsigned attempt = 1; attempt <= 8; ++attempt) {
        result.actual = nullptr;
        result.core = -1;
        result.usedFpu = false;
        result.done.store(false, std::memory_order_relaxed);
        // Internal TinyUSB API, not USB.onEvent(): callback runs in usbd itself.
        usbd_defer_func(deferredBind, nullptr, false);
        const uint32_t started = millis();
        while (!result.done.load(std::memory_order_acquire)) {
            if (uint32_t(millis() - started) >= 1000) {
                ESP_LOGE(TAG, "Deferred callback timed out; binding unverified");
                return false; // Do not queue further calls over an outstanding call.
            }
            vTaskDelay(1);
        }
        if (result.actual != result.expected) {
            ESP_LOGE(TAG, "Wrong callback task; binding refused");
            return false;
        }
        ESP_LOGI(TAG, "Attempt %u: task=usbd core=%d before=%s after=%s fpu=%u",
                 attempt, int(result.core), affinityName(result.before),
                 affinityName(result.after), unsigned(result.usedFpu));
        if (result.after == 1) {
            ESP_LOGI(TAG, "PASS: usbd is pinned to Core1");
            return true;
        }
        if (result.after != tskNO_AFFINITY) {
            ESP_LOGE(TAG, "Unexpected affinity; test stopped");
            return false;
        }
        vTaskDelay(1); // Retry from the Core 1 setup task, not from usbd.
    }
    ESP_LOGE(TAG, "No Core1 binding after eight attempts; audio must not start");
    return false;
}

static void report() {
    TaskHandle_t task = xTaskGetHandle("usbd");
    ESP_LOGI(TAG, "Monitor: usbd affinity=%s",
             task ? affinityName(xTaskGetCoreID(task)) : "MISSING");
}
}



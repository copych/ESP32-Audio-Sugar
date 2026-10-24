#include "AudioTransport.h"
#include "../../Config.h"
#include "../../BoardConfig.h"
#include "InstrumentEngine.h"
#include "../usb/UsbAudio.h"
#include "driver/i2s_std.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "../core/SharedWord.h"

namespace SugarAudio {
static i2s_chan_handle_t tx = nullptr;
static TaskHandle_t task = nullptr;
static DRAM_ATTR int16_t pcm[SUGAR_BLOCK_FRAMES * 2];
static DRAM_ATTR SugarCore::SharedWord maxRenderCycles, overBudget, ioErrors, blocks;
static DRAM_ATTR SugarCore::SharedWord storageRequested,storagePaused;

static_assert(SUGAR_SAMPLE_RATE == 44100, "Test instrument requires 44100 Hz");

static void audioTask(void*) {
    const uint32_t budgetCycles = uint32_t(uint64_t(getCpuFrequencyMhz()) * 1000000U *
                                          SUGAR_BLOCK_FRAMES / SUGAR_SAMPLE_RATE);
    uint32_t maximum = 0, late = 0, completed = 0;
    for (;;) {
        if(storageRequested.load()) {
            // Explicit storage boundary, not render: stop notes and flush two
            // silence blocks before disabling clocks. Flash writes can stall
            // both cores, so we do not pretend this is gap-free playback.
            SugarEngine::quiesceForStorage();
            memset(pcm,0,sizeof(pcm));
            for(unsigned i=0;i<2;++i) {
                size_t written=0;
                if(i2s_channel_write(tx,pcm,sizeof(pcm),&written,100)!=ESP_OK || written!=sizeof(pcm)) ioErrors.store(1);
            }
            if(i2s_channel_disable(tx)!=ESP_OK) ioErrors.store(1);
            storagePaused.store(1);
            while(storageRequested.load()) vTaskDelay(1);
            if(i2s_channel_enable(tx)!=ESP_OK) ioErrors.store(1);
            storagePaused.store(0);
            if(ioErrors.load()) { i2s_channel_disable(tx); vTaskSuspend(nullptr); }
        }
        const uint32_t before = esp_cpu_get_cycle_count();
        SugarEngine::render(pcm);
        const uint32_t elapsed = esp_cpu_get_cycle_count() - before;
        if (elapsed > maximum) {
            maximum = elapsed;
            maxRenderCycles.store(maximum);
        }
        if (elapsed > budgetCycles) overBudget.store(++late);

        // Transport boundary: intentional DMA pacing, outside render().
        // A bounded timeout turns driver failure into a stopped, silent stream.
        size_t written = 0;
        const esp_err_t result = i2s_channel_write(tx, pcm, sizeof(pcm), &written, 100);
        if (result != ESP_OK || written != sizeof(pcm)) {
            ioErrors.store(1);
            i2s_channel_disable(tx);
            vTaskSuspend(nullptr);
        }
        SugarUSB::pushPcm16(pcm, SUGAR_BLOCK_FRAMES);
        blocks.store(++completed);
    }
}

bool begin() {
    if (xPortGetCoreID() != 1 || task) return false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 2;
    channel.dma_frame_num = SUGAR_BLOCK_FRAMES;
    channel.auto_clear = true;
    esp_err_t result = i2s_new_channel(&channel, &tx, nullptr);
    if (result != ESP_OK) return false;
    i2s_std_config_t standard = {};
    standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SUGAR_SAMPLE_RATE);
    standard.slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    standard.gpio_cfg.bclk = gpio_num_t(SugarBoard::bclk);
    standard.gpio_cfg.ws = gpio_num_t(SugarBoard::wordClock);
    standard.gpio_cfg.dout = gpio_num_t(SugarBoard::dout);
    standard.gpio_cfg.din = I2S_GPIO_UNUSED;
    result = i2s_channel_init_std_mode(tx, &standard);
    if (result == ESP_OK) result = i2s_channel_enable(tx);
    if (result == ESP_OK && xTaskCreatePinnedToCore(audioTask, "sugar_audio", 4096,
                                                 nullptr, 8, &task, 0) == pdPASS)
        return true;
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    tx = nullptr;
    return false;
}

bool pauseForStorage() {
    if(xPortGetCoreID()!=1 || !task || ioErrors.load() || SugarEngine::presetBusy()) return false;
    storageRequested.store(1);
    const uint32_t start=millis();
    while(!storagePaused.load()) {
        if(uint32_t(millis()-start)>=250) { storageRequested.store(0); return false; }
        delay(1);
    }
    if(ioErrors.load()) { storageRequested.store(0); return false; }
    return true;
}
bool resumeAfterStorage() {
    storageRequested.store(0);
    const uint32_t start=millis();
    while(storagePaused.load()) {
        if(uint32_t(millis()-start)>=250) return false;
        delay(1);
    }
    return !ioErrors.load();
}

void report() {
    ESP_LOGI("SugarAudio", "core=%d blocks=%lu renderMaxCycles=%lu overBudget=%lu ioFault=%lu",
             task ? int(xTaskGetCoreID(task)) : -1,
             (unsigned long)blocks.load(),
             (unsigned long)maxRenderCycles.load(),
             (unsigned long)overBudget.load(),
             (unsigned long)ioErrors.load());
}
}






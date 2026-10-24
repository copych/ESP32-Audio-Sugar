#include "OutputMeter.h"
#include "esp_log.h"
#include <math.h>
namespace SugarMix {
void IRAM_ATTR OutputMeter::finishBlock() {
    if(++blocks_!=windowBlocks) return;
    const uint32_t version=version_.load();
    version_.store(version+1);
    squaresLow_.store(uint32_t(squares_)); squaresHigh_.store(uint32_t(squares_>>32));
    inputPeakWord_.store(inputPeak_); outputPeakWord_.store(outputPeak_); compressedWord_.store(compressed_);
    version_.store(version+2);
    squares_=0; inputPeak_=outputPeak_=compressed_=blocks_=0;
}
void OutputMeter::report() {
    const auto version=version_.load();
    if(!version || (version&1U)) return;
    const uint64_t squares=(uint64_t(squaresHigh_.load())<<32)|squaresLow_.load();
    const auto input=inputPeakWord_.load(),output=outputPeakWord_.load(),compressed=compressedWord_.load();
    if(version_.load()!=version) return;
    constexpr uint32_t samples=windowBlocks*SUGAR_BLOCK_FRAMES*2;
    const float rms=sqrtf(float(squares)*(1.0f/float(samples)));
    const float rmsDb=rms>0.0f ? 20.0f*log10f(rms*(1.0f/32768.0f)) : -120.0f;
    const float peakDb=output ? 20.0f*log10f(float(output)*(1.0f/32768.0f)) : -120.0f;
    ESP_LOGI("SugarMix","windowMs=%lu prePeak=%lu peak=%lu peakDbFS=%.2f rmsDbFS=%.2f softSamples=%lu/%lu",
        (unsigned long)(windowBlocks*SUGAR_BLOCK_FRAMES*1000U/SUGAR_SAMPLE_RATE),(unsigned long)input,
        (unsigned long)output,double(peakDb),double(rmsDb),(unsigned long)compressed,(unsigned long)samples);
}
}

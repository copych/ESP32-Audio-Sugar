#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../core/SharedWord.h"
#include "MixMath.h"
namespace SugarMix {
// Core0 writer; Core1 may read a complete snapshot once, without waiting.
class OutputMeter {
public:
    inline __attribute__((always_inline)) void add(int32_t input,int16_t output) {
        const auto before=magnitude(input),after=magnitude(output);
        if(before>inputPeak_) inputPeak_=before;
        if(after>outputPeak_) outputPeak_=after;
        squares_+=uint32_t(int32_t(output)*int32_t(output));
        compressed_+=before>uint32_t(knee);
    }
    void finishBlock();
    void report();
private:
    static constexpr uint32_t windowBlocks=1024;
    uint64_t squares_=0;
    uint32_t inputPeak_=0,outputPeak_=0,compressed_=0,blocks_=0;
    SugarCore::SharedWord version_,squaresLow_,squaresHigh_,inputPeakWord_,outputPeakWord_,compressedWord_;
};
}

#pragma once
#include <stdint.h>
namespace SugarSynthDsp {
constexpr uint32_t envelopePeak=1U<<24;
constexpr uint32_t decayStep(int32_t ms,uint32_t rate) {
    return ms>0 && rate ? uint32_t(uint64_t(envelopePeak)*1000U/(uint64_t(uint32_t(ms))*rate))+1U : envelopePeak;
}
constexpr uint32_t decay(uint32_t value,uint32_t step) { return value>step ? value-step : 0; }
constexpr int32_t clamp(int32_t value,int32_t limit) { return value>limit ? limit : value< -limit ? -limit : value; }
// Every pole stays within +/-16384: coefficient is Q15 in [0,16384].
// Feedback is clipped before the cascade, including maximum resonance.
constexpr int32_t filter(int32_t input,int32_t coefficient,int32_t feedback,int32_t (&poles)[4]) {
    int32_t x=clamp(input-((poles[3]*feedback)>>14),16384);
    for(auto& pole:poles) { pole+=((x-pole)*coefficient)>>15; x=pole; }
    return x;
}
constexpr uint32_t noise(uint32_t state) { state^=state<<13; state^=state>>17; return state^(state<<5); }
}

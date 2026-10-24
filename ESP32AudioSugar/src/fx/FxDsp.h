#pragma once
#include <stdint.h>
#include "../audio/MixMath.h"
namespace SugarFX {
constexpr int32_t unity=4096;
constexpr unsigned rampFrames=128;
constexpr unsigned delayFrames=16384;
struct Frame { int32_t left=0,right=0; };
// All callers keep |sample|<=65536 and coefficient<=32768. The largest
// product is a PCM16 drive sample *8*unity, safely within signed32.
// Constant division rounds toward zero, so feedback cannot retain a -1 DC tail.
constexpr __attribute__((always_inline)) int32_t scale(int32_t sample,int32_t coefficient) { return sample*coefficient/unity; }
struct Ramp {
    int32_t start=0,current=0,target=0;
    unsigned position=rampFrames;
    constexpr void __attribute__((always_inline)) reset(int32_t value) { start=current=target=value; position=rampFrames; }
    constexpr void __attribute__((always_inline)) set(int32_t value) {
        if(value==target) return;
        start=current; target=value; position=0;
    }
    constexpr int32_t __attribute__((always_inline)) skip(unsigned frames) {
        if(position<rampFrames) {
            position=frames>=rampFrames-position ? rampFrames : position+frames;
            current=start+(target-start)*int32_t(position)/int32_t(rampFrames);
        }
        return current;
    }
    constexpr int32_t __attribute__((always_inline)) step() { return skip(1); }
};
}

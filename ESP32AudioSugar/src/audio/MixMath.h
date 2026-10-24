#pragma once
#include <stdint.h>
namespace SugarMix {
// Integer square root, evaluated at compile time for fixed voice/part counts.
constexpr uint32_t root(uint32_t value) {
    uint32_t result=0,bit=1U<<30;
    for(unsigned i=0;i<16;++i,bit>>=2) {
        if(value>=result+bit) { value-=result+bit; result=(result>>1)+bit; }
        else result>>=1;
    }
    return result;
}
template<unsigned Count> constexpr uint32_t gainQ15() {
    static_assert(Count>0 && Count<=32,"Fixed mix contributor count must be 1..32");
    return root((1U<<30)/Count);
}
template<unsigned Count> constexpr __attribute__((always_inline)) int32_t normalize(int32_t sum) {
    constexpr uint32_t coefficient=gainQ15<Count>();
    const int64_t absolute=sum<0 ? -int64_t(sum) : int64_t(sum);
    const int64_t scaled=(absolute*coefficient+16384)>>15;
    return int32_t(sum<0 ? -scaled : scaled);
}
constexpr uint32_t magnitude(int32_t value) { return value<0 ? 0U-uint32_t(value) : uint32_t(value); }
constexpr int32_t knee=16383;
constexpr uint32_t width=32768;
constexpr int32_t voicePeak=16384; // Factory per-voice peak reference, not RMS normalization.
// Unity below ~-6 dBFS. Quadratic shoulder has unity slope at its knee and
// zero slope at the saturation boundary. Signed INT_MIN is handled safely.
constexpr __attribute__((always_inline)) int16_t softClip(int32_t input) {
    const uint32_t absolute=magnitude(input);
    if(absolute<=uint32_t(knee)) return int16_t(input);
    const uint32_t over=absolute-uint32_t(knee);
    const int32_t shaped=over>=width ? 32767 : knee+int32_t(over)-int32_t((over*over)>>16);
    return int16_t(input<0 ? -shaped : shaped);
}
}

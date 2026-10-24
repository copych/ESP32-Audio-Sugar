#pragma once
#include "FxDsp.h"
#include "FxControl.h"
namespace SugarFX {
// Three useful controls; parameter count comes from this effect, not the host.
class CrusherEffect {
    unsigned shift=0,period=1,remaining=0;
    Frame held;
    Ramp output;
public:
    enum eControl : uint8_t { CONTROL_BITS, CONTROL_HOLD, CONTROL_OUTPUT, CONTROLS_COUNT };
    static constexpr Control controls[CONTROLS_COUNT]={
        {"bits","Bits","bit",4,16,12,1},
        {"hold","Hold","frames",1,32,1,1},
        {"output","Output","",0,127,127,1}};
    static constexpr unsigned controlCount() { return CONTROLS_COUNT; }
    static constexpr const Control* control(unsigned id) { return id<CONTROLS_COUNT ? &controls[id] : nullptr; }
    static constexpr const char* key() { return "sugar.crusher.v1"; }
    static constexpr const char* name() { return "Bit crusher"; }
    static constexpr bool supportsRate(uint32_t rate) { return rate>0; }
    static constexpr uint32_t prepareControl(unsigned id,int32_t value,uint32_t) {
        return id==CONTROL_BITS ? uint32_t(16-value) : id==CONTROL_OUTPUT ? uint32_t(value*unity/127) : uint32_t(value);
    }
    constexpr void prepare(uint32_t) { shift=4; period=1; output.reset(unity); reset(); }
    constexpr void __attribute__((always_inline)) reset() { remaining=0; held={}; output.reset(output.target); }
    constexpr bool __attribute__((always_inline)) applyControl(unsigned id,uint32_t value) {
        switch(id) {
        case CONTROL_BITS: if(value>12) return false; shift=value; return true;
        case CONTROL_HOLD: if(!value || value>32) return false; period=value; remaining=0; return true;
        case CONTROL_OUTPUT: output.set(int32_t(value)); return true;
        default: return false;
        }
    }
    constexpr void __attribute__((always_inline)) advance(unsigned frames) { output.skip(frames); remaining=0; held={}; }
    constexpr Frame __attribute__((always_inline)) process(Frame input) {
        if(!remaining) {
            // Magnitude quantization avoids implementation-defined signed shifts
            // and negative bias. PCM16 input bounds make negation/products safe.
            const int32_t left=int32_t((SugarMix::magnitude(input.left)>>shift)<<shift);
            const int32_t right=int32_t((SugarMix::magnitude(input.right)>>shift)<<shift);
            held={input.left<0 ? -left : left,input.right<0 ? -right : right}; remaining=period;
        }
        --remaining;
        const auto gain=output.step();
        return {scale(held.left,gain),scale(held.right,gain)};
    }
};
}

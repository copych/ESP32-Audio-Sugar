#pragma once
#include "EffectProfile.h"
namespace SugarFX {
// Framework-owned level, smoothed bypass and post-insert send. The effect
// owns only its algorithm/control state. Bypass/inactive slots advance with
// no input; an effect may advance a tail using a bounded silence loop.
template<typename Effect> struct PartSignal {
    static_assert(validEffectProfile<Effect>(),"Invalid insert effect contract/profile");
    Ramp level,wet,send;
    Effect effect;
    constexpr PartSignal() { level.reset(unity); }
    constexpr void prepare(uint32_t rate) { effect.prepare(rate); }
    constexpr void __attribute__((always_inline)) reset() { effect.reset(); }
    constexpr void __attribute__((always_inline)) skip(unsigned frames) {
        level.skip(frames); wet.skip(frames); send.skip(frames); effect.advance(frames);
    }
    constexpr Frame __attribute__((always_inline)) process(Frame input,Frame& toBus) {
        const int32_t gain=level.step(),mix=wet.step(),bus=send.step();
        Frame output{scale(input.left,gain),scale(input.right,gain)};
        if(mix) {
            const auto processed=effect.process(output);
            output.left+=scale(processed.left-output.left,mix);
            output.right+=scale(processed.right-output.right,mix);
        } else effect.advance(1);
        toBus={scale(output.left,bus),scale(output.right,bus)};
        return output;
    }
};
}

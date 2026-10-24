#pragma once
#include "FxDsp.h"
#include "FxControl.h"
#include "../parameters/ParameterRegistry.h"
namespace SugarFX {
class DriveEffect {
    Ramp amount;
public:
    enum eControl : uint8_t { CONTROL_AMOUNT, CONTROLS_COUNT };
    static constexpr Control controls[CONTROLS_COUNT]={{"amount","Drive","",0,127,0,1}};
    static constexpr unsigned controlCount() { return CONTROLS_COUNT; }
    static constexpr const Control* control(unsigned id) { return id<CONTROLS_COUNT ? &controls[id] : nullptr; }
    static constexpr uint32_t prepareControl(unsigned id,int32_t value,uint32_t rate) {
        return id==CONTROL_AMOUNT ? prepareParameter(SugarParameters::PARAM_FX_DRIVE,value,rate) : 0;
    }
    constexpr bool __attribute__((always_inline)) applyControl(unsigned id,uint32_t value) {
        return id==CONTROL_AMOUNT && applyParameter(SugarParameters::PARAM_FX_DRIVE,value);
    }
    static constexpr bool supportsRate(uint32_t rate) { return rate>0; }
    static constexpr const char* key() { return "sugar.drive.v1"; }
    static constexpr const char* name() { return "Drive"; }
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        return id==SugarParameters::PARAM_FX_DRIVE ? SugarParameters::descriptor(id) : nullptr;
    }
    static constexpr uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t) {
        return id==SugarParameters::PARAM_FX_DRIVE ? uint32_t(unity+value*(7*unity)/127) : 0;
    }
    constexpr void prepare(uint32_t rate) {
        amount.reset(int32_t(prepareParameter(SugarParameters::PARAM_FX_DRIVE,
            parameterDescriptor(SugarParameters::PARAM_FX_DRIVE)->defaultValue,rate)));
    }
    constexpr void __attribute__((always_inline)) reset() { amount.reset(amount.target); }
    constexpr bool __attribute__((always_inline)) applyParameter(SugarParameters::eParameterId id,uint32_t prepared) {
        if(id!=SugarParameters::PARAM_FX_DRIVE) return false;
        amount.set(int32_t(prepared)); return true;
    }
    constexpr void __attribute__((always_inline)) advance(unsigned frames) { amount.skip(frames); }
    constexpr Frame __attribute__((always_inline)) process(Frame input) {
        const auto gain=amount.step();
        return {SugarMix::softClip(scale(input.left,gain)),SugarMix::softClip(scale(input.right,gain))};
    }
};
}

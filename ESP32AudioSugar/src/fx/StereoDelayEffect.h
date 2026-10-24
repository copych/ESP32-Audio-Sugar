#pragma once
#include "FxDsp.h"
#include "FxControl.h"
#include "../parameters/ParameterRegistry.h"
namespace SugarFX {
template<unsigned Frames> class StereoDelayEffect {
    static_assert(Frames>1 && !(Frames&(Frames-1)),"Delay storage must be a power of two");
    int16_t history[Frames][2]={};
    unsigned position=0,valid=0,tap=1,oldTap=1,pendingTap=1;
    Ramp crossfade;
    constexpr Frame __attribute__((always_inline)) read(unsigned distance) const {
        if(valid<distance) return {};
        const unsigned index=(position+Frames-distance)&(Frames-1);
        return {history[index][0],history[index][1]};
    }
public:
    enum eControl : uint8_t { CONTROL_TIME, CONTROL_FEEDBACK, CONTROLS_COUNT };
    static constexpr Control controls[CONTROLS_COUNT]={
        {"time_ms","Time","ms",10,350,180,10},{"feedback","Feedback","%",0,90,35,1}};
    static constexpr unsigned controlCount() { return CONTROLS_COUNT; }
    static constexpr const Control* control(unsigned id) { return id<CONTROLS_COUNT ? &controls[id] : nullptr; }
    static constexpr uint32_t prepareControl(unsigned id,int32_t value,uint32_t rate) {
        return id==CONTROL_TIME ? uint32_t(uint64_t(value)*rate/1000) : id==CONTROL_FEEDBACK ? uint32_t(value*unity/100) : 0;
    }
    constexpr bool __attribute__((always_inline)) applyControl(unsigned id,uint32_t value) {
        return id==CONTROL_TIME ? setTime(value) : id==CONTROL_FEEDBACK ? applyParameter(SugarParameters::PARAM_DELAY_FEEDBACK,value) : false;
    }
    static constexpr bool supportsRate(uint32_t rate) { return rate>=100 && uint64_t(rate)*350/1000<Frames; }
    static constexpr const char* key() { return "sugar.delay.v1"; }
    static constexpr const char* name() { return "Stereo delay"; }
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        return id==SugarParameters::PARAM_DELAY_TIME_MS || id==SugarParameters::PARAM_DELAY_FEEDBACK ? SugarParameters::descriptor(id) : nullptr;
    }
    static constexpr uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t rate) {
        return id==SugarParameters::PARAM_DELAY_TIME_MS ? uint32_t(uint64_t(value)*rate/1000) :
            id==SugarParameters::PARAM_DELAY_FEEDBACK ? uint32_t(value*unity/100) : 0;
    }
    constexpr void prepare(uint32_t rate) {
        setTime(prepareParameter(SugarParameters::PARAM_DELAY_TIME_MS,180,rate));
        feedback.reset(prepareParameter(SugarParameters::PARAM_DELAY_FEEDBACK,35,rate)); reset();
    }
    constexpr bool __attribute__((always_inline)) applyParameter(SugarParameters::eParameterId id,uint32_t value) {
        if(id==SugarParameters::PARAM_DELAY_TIME_MS) return setTime(value);
        if(id==SugarParameters::PARAM_DELAY_FEEDBACK) { feedback.set(int32_t(value)); return true; }
        return false;
    }
    constexpr void __attribute__((always_inline)) advance(unsigned frames) { for(unsigned f=0;f<frames;++f) process({}); }
    Ramp feedback;
    constexpr StereoDelayEffect() { crossfade.reset(unity); }
    constexpr bool __attribute__((always_inline)) setTime(unsigned frames) {
        if(!frames || frames>=Frames) return false;
        pendingTap=frames; return true;
    }
    // Constant-time history invalidation. Old cells are never read until
    // overwritten; no large memset occurs at a preset/panic/storage boundary.
    constexpr void __attribute__((always_inline)) reset() { position=valid=0; tap=oldTap=pendingTap; crossfade.reset(unity); }
    constexpr Frame __attribute__((always_inline)) process(Frame input) {
        if(crossfade.position==rampFrames && tap!=pendingTap) {
            oldTap=tap; tap=pendingTap; crossfade.reset(0); crossfade.set(unity);
        }
        const int32_t blend=crossfade.step();
        Frame delayed=read(tap);
        if(blend!=unity) {
            const auto old=read(oldTap);
            delayed.left=old.left+scale(delayed.left-old.left,blend);
            delayed.right=old.right+scale(delayed.right-old.right,blend);
        }
        const auto gain=feedback.step();
        history[position][0]=SugarMix::softClip(input.left+scale(delayed.right,gain));
        history[position][1]=SugarMix::softClip(input.right+scale(delayed.left,gain));
        position=(position+1)&(Frames-1);
        if(valid<Frames) ++valid;

        return delayed;
    }
};
}

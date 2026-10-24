#pragma once
#include "EffectTypes.h"
#include "SerialSlot.h"
namespace SugarFX {
constexpr bool parameter(SugarParameters::eParameterId id) { return SugarParameters::frameworkParameter(id); }
constexpr bool sharedParameter(SugarParameters::eParameterId id) {
    using namespace SugarParameters;
    return id==PARAM_DELAY_RETURN || id==PARAM_DELAY_TIME_MS || id==PARAM_DELAY_FEEDBACK;
}
constexpr SugarParameters::Descriptor busCompatibilityDescriptor(SugarParameters::eParameterId id,unsigned local) {
    const auto* p=SharedSendEffect::control(local);
    return p ? SugarParameters::Descriptor{id,p->name,p->unit,SugarParameters::PARAM_SCOPE_PART,SugarParameters::PARAM_KIND_INTEGER,
        p->minimum,p->maximum,p->defaultValue} : *SugarParameters::descriptor(id);
}
constexpr auto busTimeDescriptor=busCompatibilityDescriptor(SugarParameters::PARAM_DELAY_TIME_MS,busTimeControl);
constexpr auto busFeedbackDescriptor=busCompatibilityDescriptor(SugarParameters::PARAM_DELAY_FEEDBACK,busFeedbackControl);
constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id,bool primary) {
    using namespace SugarParameters;
    if(sharedParameter(id) && !primary) return nullptr;
    if(id==PARAM_DELAY_TIME_MS && busTimeControl==SharedSendEffect::controlCount()) return nullptr;
    if(id==PARAM_DELAY_FEEDBACK && busFeedbackControl==SharedSendEffect::controlCount()) return nullptr;
    if(id==PARAM_DELAY_TIME_MS) return &busTimeDescriptor;
    if(id==PARAM_DELAY_FEEDBACK) return &busFeedbackDescriptor;
    return parameter(id) ? descriptor(id) : nullptr;
}
constexpr uint32_t prepare(SugarParameters::eParameterId id,int32_t value,uint32_t rate) {
    using namespace SugarParameters;
    if(id==PARAM_PART_LEVEL || id==PARAM_FX_SEND || id==PARAM_DELAY_RETURN) return uint32_t(value*unity/127);
    if(id==PARAM_FX_DRIVE) return DriveEffect::prepareControl(DriveEffect::CONTROL_AMOUNT,value,rate);
    if(id==PARAM_DELAY_TIME_MS) return SharedSendEffect::prepareControl(busTimeControl,value,rate);
    if(id==PARAM_DELAY_FEEDBACK) return SharedSendEffect::prepareControl(busFeedbackControl,value,rate);
    return uint32_t(value);
}
template<unsigned Capacity,unsigned BlockFrames,unsigned Rate,unsigned Slots=FX_SLOTS_COUNT> class Rack {
    static_assert(Slots>0 && Slots<=8,"Invalid FX slot capacity");
    static_assert(InsertPool::supportsRate(Rate) && SharedSendEffect::supportsRate(Rate),"Effect storage/rate mismatch");
    struct Part {
        Ramp level,send;
        SerialSlot slots[Slots];
        constexpr Part() { level.reset(unity); }
    } parts[Capacity];
    SharedSendEffect busEffect;
    Ramp returnGain;
    int32_t send[BlockFrames*2]={},work[BlockFrames*2]={}; // one reusable block, not per slot/part
    uint32_t peak=0;
public:
    constexpr void prepare() {
        for(auto& part:parts) for(auto& slot:part.slots) slot.prepare(Rate);
        busEffect.prepare(Rate); returnGain.reset(0);
    }
    constexpr __attribute__((always_inline)) void clearTail() {
        for(auto& part:parts) for(auto& slot:part.slots) slot.reset();
        busEffect.reset();
    }
    constexpr __attribute__((always_inline)) bool select(unsigned part,unsigned slot,eAlgorithm type) {
        return part<Capacity && slot<Slots && parts[part].slots[slot].select(type);
    }
    constexpr __attribute__((always_inline)) bool control(unsigned part,unsigned slot,eAlgorithm type,unsigned id,uint32_t value) {
        return part<Capacity && slot<Slots && parts[part].slots[slot].apply(type,id,value);
    }
    constexpr __attribute__((always_inline)) bool busControl(unsigned id,uint32_t value) { return busEffect.applyControl(id,value); }
    constexpr __attribute__((always_inline)) bool apply(unsigned part,SugarParameters::eParameterId id,uint32_t value) {
        using namespace SugarParameters;
        if(part>=Capacity) return false;
        switch(id) {
        case PARAM_PART_LEVEL: parts[part].level.set(int32_t(value)); return true;
        case PARAM_FX_INSERT: return select(part,FX_SLOT_FIRST,value==INSERT_EFFECT ? ALGORITHM_DRIVE : ALGORITHM_BYPASS);
        case PARAM_FX_DRIVE: return control(part,FX_SLOT_FIRST,ALGORITHM_DRIVE,DriveEffect::CONTROL_AMOUNT,value);
        case PARAM_FX_SEND: parts[part].send.set(int32_t(value)); return true;
        case PARAM_DELAY_RETURN: returnGain.set(int32_t(value)); return true;
        case PARAM_DELAY_TIME_MS: return busControl(busTimeControl,value);
        case PARAM_DELAY_FEEDBACK: return busControl(busFeedbackControl,value);
        default: return false;
        }
    }
    constexpr __attribute__((always_inline)) void beginBlock(int32_t* mix) { for(unsigned i=0;i<BlockFrames*2;++i) mix[i]=send[i]=0; }
    constexpr __attribute__((always_inline)) void inactivePart(unsigned part) {
        parts[part].level.skip(BlockFrames); parts[part].send.skip(BlockFrames);
        for(auto& slot:parts[part].slots) slot.advance(BlockFrames);
    }
    constexpr __attribute__((always_inline)) void addPart(unsigned part,const int16_t* input,int32_t* mix) {
        auto& state=parts[part];
        for(unsigned f=0;f<BlockFrames;++f) {
            const auto level=state.level.step(); const unsigned i=f*2;
            work[i]=scale(input[i],level); work[i+1]=scale(input[i+1],level);
        }
        for(auto& slot:state.slots) slot.processBlock(work,BlockFrames);
        for(unsigned f=0;f<BlockFrames;++f) {
            const unsigned i=f*2; const auto amount=state.send.step();
            mix[i]+=work[i]; mix[i+1]+=work[i+1];
            send[i]+=scale(work[i],amount); send[i+1]+=scale(work[i+1],amount);
        }
    }
    constexpr __attribute__((always_inline)) Frame outputFrame(unsigned frame,const int32_t* mix) {
        const unsigned i=frame*2;
        const auto delayed=busEffect.process({SugarMix::normalize<Capacity>(send[i]),SugarMix::normalize<Capacity>(send[i+1])});
        const auto volume=returnGain.step();
        const Frame wet{scale(delayed.left,volume),scale(delayed.right,volume)};
        const auto left=SugarMix::magnitude(wet.left),right=SugarMix::magnitude(wet.right);
        if(left>peak) peak=left;
        if(right>peak) peak=right;
        return {SugarMix::normalize<Capacity>(mix[i])+wet.left,SugarMix::normalize<Capacity>(mix[i+1])+wet.right};
    }
    constexpr __attribute__((always_inline)) uint32_t takePeak() { const auto value=peak; peak=0; return value; }
};
}

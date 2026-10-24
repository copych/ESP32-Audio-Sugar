#pragma once
#include "DriveEffect.h"
#include "CrusherEffect.h"
namespace SugarFX {
class BypassEffect {
public:
    static constexpr const char* key() { return "sugar.bypass.v1"; }
    static constexpr const char* name() { return "Bypass"; }
    static constexpr unsigned controlCount() { return 0; }
    static constexpr const Control* control(unsigned) { return nullptr; }
    static constexpr bool supportsRate(uint32_t rate) { return rate>0; }
    static constexpr uint32_t prepareControl(unsigned,int32_t,uint32_t) { return 0; }
    constexpr void prepare(uint32_t) {}
    constexpr void reset() {}
    constexpr void advance(unsigned) {}
    constexpr bool applyControl(unsigned,uint32_t) { return false; }
    constexpr Frame process(Frame input) { return input; }
};
// Recursive storage/visitor: one type selection per block, concrete sample loop.
template<class... Effects> struct EffectPool;
template<> struct EffectPool<> {
    static constexpr unsigned count=0,maxControls=0;
    static constexpr const char* key(unsigned) { return nullptr; }
    static constexpr const char* name(unsigned) { return nullptr; }
    static constexpr unsigned controlCount(unsigned) { return 0; }
    static constexpr const Control* control(unsigned,unsigned) { return nullptr; }
    static constexpr uint32_t prepareControl(unsigned,unsigned,int32_t,uint32_t) { return 0; }
    static constexpr bool supportsRate(uint32_t) { return true; }
    constexpr void prepare(uint32_t) {}
    template<class Fn> constexpr __attribute__((always_inline)) bool visit(unsigned,Fn&&) { return false; }
};
template<class First,class... Rest> struct EffectPool<First,Rest...> {
    static_assert(validLocalProfile<First>(),"Invalid effect-local controls");
    using Tail=EffectPool<Rest...>;
    static constexpr bool uniqueKey() {
        for(unsigned i=0;i<Tail::count;++i) if(equalKey(First::key(),Tail::key(i))) return false;
        return true;
    }
    static_assert(uniqueKey(),"Duplicate effect preset identity");
    First effect;
    Tail rest;
    static constexpr unsigned count=1+Tail::count;
    static constexpr unsigned maxControls=First::controlCount()>Tail::maxControls ? First::controlCount() : Tail::maxControls;
    static constexpr const char* key(unsigned type) { return type ? Tail::key(type-1) : First::key(); }
    static constexpr const char* name(unsigned type) { return type ? Tail::name(type-1) : First::name(); }
    static constexpr unsigned controlCount(unsigned type) { return type ? Tail::controlCount(type-1) : First::controlCount(); }
    static constexpr const Control* control(unsigned type,unsigned id) { return type ? Tail::control(type-1,id) : First::control(id); }
    static constexpr uint32_t prepareControl(unsigned type,unsigned id,int32_t value,uint32_t rate) {
        return type ? Tail::prepareControl(type-1,id,value,rate) : First::prepareControl(id,value,rate);
    }
    static constexpr bool supportsRate(uint32_t rate) { return First::supportsRate(rate) && Tail::supportsRate(rate); }
    constexpr void prepare(uint32_t rate) { effect.prepare(rate); rest.prepare(rate); }
    template<class Fn> constexpr __attribute__((always_inline)) bool visit(unsigned type,Fn&& fn) {
        if(type) return rest.visit(type-1,static_cast<Fn&&>(fn));
        fn(effect); return true;
    }
};
// Enum order and this list form the registration point. No global parameter IDs.
using InsertPool=EffectPool<BypassEffect,DriveEffect,CrusherEffect>;
static_assert(InsertPool::count==ALGORITHMS_COUNT,"Effect enum/catalog mismatch");
static_assert(InsertPool::maxControls<255,"Queued control index is one byte");
struct SlotState {
    eAlgorithm algorithm=ALGORITHM_BYPASS;
    int32_t values[InsertPool::maxControls]={};
};
constexpr SlotState defaultSlot(eAlgorithm type) {
    SlotState result; result.algorithm=type;
    for(unsigned i=0;i<InsertPool::controlCount(type);++i) result.values[i]=InsertPool::control(type,i)->defaultValue;
    return result;
}
constexpr bool validSlot(const SlotState& slot) {
    if(slot.algorithm>=ALGORITHMS_COUNT) return false;
    for(unsigned i=0;i<InsertPool::maxControls;++i) {
        const auto* p=InsertPool::control(slot.algorithm,i);
        if(p ? !validControl(p,slot.values[i]) : slot.values[i]!=0) return false;
    }
    return true;
}
}

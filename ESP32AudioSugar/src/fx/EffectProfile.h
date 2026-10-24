#pragma once
#include <type_traits>
#include "FxDsp.h"
#include "../parameters/ParameterRegistry.h"
namespace SugarFX {
constexpr bool rackParameter(SugarParameters::eParameterId id) {
    using namespace SugarParameters;
    return id==PARAM_PART_LEVEL || id==PARAM_FX_INSERT || id==PARAM_FX_SEND || id==PARAM_DELAY_RETURN;
}
constexpr bool sameKey(const char* a,const char* b) {
    if(!a || !b) return false;
    while(*a && *b) if(*a++!=*b++) return false;
    return *a==*b;
}
// Compile-time contract only: concrete members, no interface base or vtable.
// prepare / static metadata / coefficient preparation: Core1.
// applyParameter / process / advance / reset: bounded Core0 operations.
// reset clears audible history while preserving accepted parameter targets.
template<typename Effect> constexpr bool validEffectProfile() {
    using namespace SugarParameters;
    if(!std::is_same<decltype(&Effect::supportsRate),bool(*)(uint32_t)>::value) return false;
    if(!std::is_same<decltype(&Effect::process),Frame(Effect::*)(Frame)>::value ||
       !std::is_same<decltype(&Effect::prepare),void(Effect::*)(uint32_t)>::value ||
       !std::is_same<decltype(&Effect::reset),void(Effect::*)()>::value ||
       !std::is_same<decltype(&Effect::advance),void(Effect::*)(unsigned)>::value ||
       !std::is_same<decltype(&Effect::applyParameter),bool(Effect::*)(eParameterId,uint32_t)>::value ||
       !std::is_same<decltype(&Effect::prepareParameter),uint32_t(*)(eParameterId,int32_t,uint32_t)>::value) return false;
    if(!Effect::key() || !Effect::key()[0] || !Effect::name() || !Effect::name()[0]) return false;
    for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
        const auto id=eParameterId(raw);
        if(const auto* d=Effect::parameterDescriptor(id)) {
            if(!frameworkParameter(id) || rackParameter(id) || !d->name || !d->name[0] || !d->unit ||
               d->scope!=PARAM_SCOPE_PART || !validDescriptor(d,id,d->defaultValue)) return false;
        }
    }
    return !Effect::parameterDescriptor(PARAMETERS_COUNT);
}
template<typename Insert,typename Send> constexpr bool validEffectPair() {
    if(!validEffectProfile<Insert>() || !validEffectProfile<Send>()) return false;
    for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw) {
        const auto id=SugarParameters::eParameterId(raw);
        if(Insert::parameterDescriptor(id) && Send::parameterDescriptor(id)) return false;
    }
    return true;
}
}

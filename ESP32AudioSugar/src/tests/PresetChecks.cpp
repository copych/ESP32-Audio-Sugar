#include "../presets/PresetState.h"
namespace {
using namespace SugarPresets;
constexpr bool equal(const char* a,const char* b) {
    if(!a || !b) return a==b;
    while(*a && *b) if(*a++!=*b++) return false;
    return *a==*b;
}
constexpr bool keys() {
    for(unsigned i=0;i<SugarParameters::PARAMETERS_COUNT;++i) {
        const auto* key=parameterKey(SugarParameters::eParameterId(i));
        if(!key || !key[0]) return false;
        for(unsigned j=0;j<i;++j) if(equal(key,parameterKey(SugarParameters::eParameterId(j)))) return false;
    }
    return !parameterKey(SugarParameters::PARAMETERS_COUNT);
}
constexpr bool masks() {
    State s;
    s.active=SugarParts::Routing<SUGAR_PART_CAPACITY>::availableMask;
    for(auto& route:s.routes) route=s.active;
    if(!validMasks(s)) return false;
    s.routes[0]=uint32_t(1)<<SUGAR_PART_CAPACITY;
    if(validMasks(s)) return false;
    s.routes[0]=0; s.active=uint32_t(1)<<SUGAR_PART_CAPACITY;
    return !validMasks(s);
}
static_assert(keys(),"Every parameter has a unique stable persistence key");
static_assert(masks(),"Snapshot activation/routing must fit compiled capacity");
static_assert(equal(voiceModeKey(SugarAudio::VOICE_MODE_POLY),"poly") &&
    equal(voiceModeKey(SugarAudio::VOICE_MODE_MONO_FULL),"mono_full") &&
    equal(voiceModeKey(SugarAudio::VOICE_MODE_MONO_LEGATO),"mono_legato") &&
    !voiceModeKey(SugarAudio::VOICE_MODES_COUNT),"Stable voice mode names");
}

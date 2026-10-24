#pragma once
#include "ParameterRegistry.h"
namespace SugarSynth {
// Numeric IDs are local to an instrument. Legacy IDs are optional compatibility only.
struct Parameter {
    const char* key;
    const char* name;
    const char* unit;
    SugarParameters::eParameterScope scope;
    int32_t minimum,maximum,defaultValue,step;
    const char* const* choices=nullptr;
    unsigned choiceCount=0;
    int16_t midiCC=-1;
    const SugarParameters::Descriptor* legacy=nullptr;
};
constexpr bool equal(const char* a,const char* b) {
    if(!a || !b) return false;
    while(*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}
constexpr bool valid(const Parameter* p,int32_t value,unsigned channel=0) {
    return p && channel<SugarParameters::MIDI_CHANNELS_COUNT &&
        (p->scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL || channel==0) &&
        value>=p->minimum && value<=p->maximum;
}
constexpr unsigned words(const Parameter& p) {
    return p.scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
}
template<class Instrument> constexpr unsigned nativeWords() {
    unsigned count=0;
    for(unsigned id=0;id<Instrument::parameterCount();++id) {
        const auto* p=Instrument::parameter(id);
        if(!p->legacy) count+=words(*p);
    }
    return count;
}
template<class Instrument> constexpr unsigned nativeWordsFor(const Instrument&) { return nativeWords<Instrument>(); }
template<class Instrument> constexpr unsigned nativeOffset(unsigned id) {
    unsigned count=0;
    for(unsigned i=0;i<id;++i) if(!Instrument::parameter(i)->legacy) count+=words(*Instrument::parameter(i));
    return count;
}
template<class Instrument> constexpr unsigned nativeOffsetFor(const Instrument&,unsigned id) { return nativeOffset<Instrument>(id); }
template<class Instrument> constexpr unsigned legacyLocal(SugarParameters::eParameterId id) {
    for(unsigned i=0;i<Instrument::parameterCount();++i) {
        const auto* p=Instrument::parameter(i);
        if(p->legacy && p->legacy->id==id) return i;
    }
    return Instrument::parameterCount();
}
template<class Instrument> constexpr bool validParameters() {
    if(Instrument::parameterCount()>255 || Instrument::parameter(Instrument::parameterCount())) return false;
    for(unsigned id=0;id<Instrument::parameterCount();++id) {
        const auto* p=Instrument::parameter(id);
        if(!p || !p->key || !p->key[0] || !p->name || !p->name[0] || !p->unit ||
           p->scope>=SugarParameters::PARAM_SCOPES_COUNT || p->minimum>p->maximum || p->step<=0 ||
           !valid(p,p->defaultValue) || p->midiCC < -1 || p->midiCC>119 ||
           (p->choiceCount && (!p->choices || p->minimum<0 || unsigned(p->maximum)>=p->choiceCount))) return false;
        for(unsigned c=0;c<p->choiceCount;++c) if(!p->choices[c] || !p->choices[c][0]) return false;
        if(equal(p->key,"part_level") || equal(p->key,"fx_insert") || equal(p->key,"fx_drive") ||
           equal(p->key,"fx_send") || equal(p->key,"delay_time_ms") || equal(p->key,"delay_feedback") || equal(p->key,"delay_return")) return false;
        if(p->legacy && (!SugarParameters::validId(p->legacy->id) || SugarParameters::frameworkParameter(p->legacy->id) || p->legacy->scope!=p->scope ||
            p->legacy->minimum!=p->minimum || p->legacy->maximum!=p->maximum)) return false;
        for(unsigned previous=0;previous<id;++previous) {
            const auto* other=Instrument::parameter(previous);
            if(equal(p->key,other->key) || (p->midiCC>=0 && p->midiCC==other->midiCC) ||
                (p->legacy && other->legacy && p->legacy->id==other->legacy->id)) return false;
        }
    }
    return true;
}
}

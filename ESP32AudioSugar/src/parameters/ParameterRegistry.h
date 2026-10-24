#pragma once
#include <stdint.h>
#include "../audio/VoiceMode.h"
#include "../fx/FxTypes.h"

namespace SugarParameters {
enum eParameterId : uint8_t {
    PARAM_VOICE_MODE,
    PARAM_PORTAMENTO_MS,
    PARAM_GAIN,
    PARAM_PULSE_WIDTH,
    PARAM_WAVEFORM,
    PARAM_CUTOFF,
    PARAM_RESONANCE,
    PARAM_ENV_MOD,
    PARAM_DECAY_MS,
    PARAM_PART_LEVEL,
    PARAM_FX_INSERT,
    PARAM_FX_DRIVE,
    PARAM_FX_SEND,
    PARAM_DELAY_TIME_MS,
    PARAM_DELAY_FEEDBACK,
    PARAM_DELAY_RETURN,
    PARAMETERS_COUNT
};
enum eWaveform : uint8_t { WAVEFORM_SAW, WAVEFORM_SQUARE, WAVEFORMS_COUNT };
enum eParameterScope : uint8_t {
    PARAM_SCOPE_PART,
    PARAM_SCOPE_PART_CHANNEL,
    PARAM_SCOPES_COUNT
};
enum eParameterKind : uint8_t {
    PARAM_KIND_INTEGER,
    PARAM_KIND_CHOICE,
    PARAM_KINDS_COUNT
};
constexpr unsigned MIDI_CHANNELS_COUNT = 16;
constexpr int32_t MAX_PORTAMENTO_MS = 10000;
constexpr int32_t MAX_GAIN = 127;

struct Descriptor {
    eParameterId id;
    const char* name;
    const char* unit;
    eParameterScope scope;
    eParameterKind kind;
    int32_t minimum, maximum, defaultValue;
};
// Metadata is read only on the control core, never in sample rendering.
constexpr Descriptor descriptors[PARAMETERS_COUNT] = {
    {PARAM_VOICE_MODE,"Voice mode","",PARAM_SCOPE_PART,PARAM_KIND_CHOICE,
        SugarAudio::VOICE_MODE_POLY,SugarAudio::VOICE_MODES_COUNT-1,SugarAudio::VOICE_MODE_POLY},
    {PARAM_PORTAMENTO_MS,"Portamento","ms",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,
        0,MAX_PORTAMENTO_MS,100},
    {PARAM_GAIN,"Channel gain","MIDI",PARAM_SCOPE_PART_CHANNEL,PARAM_KIND_INTEGER,0,MAX_GAIN,MAX_GAIN},
    {PARAM_PULSE_WIDTH,"Pulse width","%",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,1,99,50},
    {PARAM_WAVEFORM,"Wave","",PARAM_SCOPE_PART,PARAM_KIND_CHOICE,WAVEFORM_SAW,WAVEFORMS_COUNT-1,WAVEFORM_SAW},
    {PARAM_CUTOFF,"Cutoff","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,45},
    {PARAM_RESONANCE,"Reso","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,75},
    {PARAM_ENV_MOD,"EnvMod","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,70},
    {PARAM_DECAY_MS,"Decay","ms",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,30,1000,250},
    {PARAM_PART_LEVEL,"Level","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,127},
    {PARAM_FX_INSERT,"Insert","",PARAM_SCOPE_PART,PARAM_KIND_CHOICE,SugarFX::INSERT_BYPASS,SugarFX::INSERTS_COUNT-1,SugarFX::INSERT_BYPASS},
    {PARAM_FX_DRIVE,"Drive","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,0},
    {PARAM_FX_SEND,"Send","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,0},
    {PARAM_DELAY_TIME_MS,"DlyTime","ms",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,10,350,180},
    {PARAM_DELAY_FEEDBACK,"DlyFbk","%",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,90,35},
    {PARAM_DELAY_RETURN,"DlyRet","",PARAM_SCOPE_PART,PARAM_KIND_INTEGER,0,127,0}
};
constexpr bool validId(eParameterId id) { return unsigned(id)<PARAMETERS_COUNT; }
constexpr bool frameworkParameter(eParameterId id) {
    switch(id) {
    case PARAM_PART_LEVEL: case PARAM_FX_INSERT: case PARAM_FX_DRIVE: case PARAM_FX_SEND:
    case PARAM_DELAY_TIME_MS: case PARAM_DELAY_FEEDBACK: case PARAM_DELAY_RETURN: return true;
    default: return false;
    }
}
constexpr const Descriptor* descriptor(eParameterId id) {
    return validId(id) ? &descriptors[id] : nullptr;
}
constexpr bool valid(eParameterId id,int32_t value,unsigned channel=0) {
    const auto* d=descriptor(id);
    return d && channel<MIDI_CHANNELS_COUNT &&
        (d->scope==PARAM_SCOPE_PART_CHANNEL || channel==0) && value>=d->minimum && value<=d->maximum;
}
constexpr bool validDescriptor(const Descriptor* d,eParameterId id,int32_t value,unsigned channel=0) {
    const auto* base=descriptor(id);
    return d && base && d->id==id && d->scope==base->scope && d->kind==base->kind &&
        d->minimum<=d->maximum && channel<MIDI_CHANNELS_COUNT &&
        (d->scope==PARAM_SCOPE_PART_CHANNEL || channel==0) && value>=d->minimum && value<=d->maximum;
}
constexpr const char* choiceName(eParameterId id,unsigned choice) {
    if(id==PARAM_FX_INSERT) return choice==SugarFX::INSERT_BYPASS ? "Bypass" : choice==SugarFX::INSERT_EFFECT ? "Effect" : nullptr;
    if(id==PARAM_WAVEFORM) return choice==WAVEFORM_SAW ? "Saw" : choice==WAVEFORM_SQUARE ? "Square" : nullptr;
    if(id!=PARAM_VOICE_MODE) return nullptr;
    switch(choice) {
    case SugarAudio::VOICE_MODE_POLY: return "Poly";
    case SugarAudio::VOICE_MODE_MONO_FULL: return "Mono Full";
    case SugarAudio::VOICE_MODE_MONO_LEGATO: return "Mono Legato";
    default: return nullptr;
    }
}

// Core 1 owns requested values. Plain storage is intentional: no Core 0 reads.
class State {
public:
    constexpr State() { reset(); }
    constexpr void reset() {
        for(unsigned id=0;id<PARAMETERS_COUNT;++id)
            for(unsigned ch=0;ch<MIDI_CHANNELS_COUNT;++ch)
                values_[id][ch]=descriptors[id].defaultValue;
    }
    constexpr bool set(eParameterId id,int32_t value,unsigned channel=0) {
        return setWithDescriptor(descriptor(id),id,value,channel);
    }
    constexpr bool setWithDescriptor(const Descriptor* d,eParameterId id,int32_t value,unsigned channel=0) {
        if(!validDescriptor(d,id,value,channel)) return false;
        values_[id][channel]=value;
        return true;
    }
    constexpr bool get(eParameterId id,int32_t& value,unsigned channel=0) const {
        const auto* d=descriptor(id);
        if(!d || channel>=MIDI_CHANNELS_COUNT || (d->scope==PARAM_SCOPE_PART && channel!=0)) return false;
        value=values_[id][channel];
        return true;
    }
private:
    int32_t values_[PARAMETERS_COUNT][MIDI_CHANNELS_COUNT] = {};
};

// Control-side derived state. Q32 reciprocal avoids a variable division when
// the audio renderer starts a glide. A one-frame glide snaps directly to target.
constexpr uint32_t portamentoFrames(int32_t ms,uint32_t sampleRate) {
    return uint32_t(uint64_t(uint32_t(ms))*sampleRate/1000U);
}
constexpr uint32_t glideReciprocal(int32_t ms,uint32_t sampleRate) {
    const uint32_t frames=portamentoFrames(ms,sampleRate);
    return frames>1 ? uint32_t((uint64_t(1)<<32)/frames) : 0;
}
}



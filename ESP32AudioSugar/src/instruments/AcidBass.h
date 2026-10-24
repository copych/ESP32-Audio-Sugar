#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../audio/Instrument.h"
#include "../audio/InstrumentCapabilities.h"
#include "../audio/VoiceAllocator.h"
#include "../parameters/ParameterRegistry.h"
#include "../parameters/SynthParameter.h"
namespace AcidBass {
class Instrument {
public:
    static constexpr const char* key() { return "sugar.acid.v1"; }
    static constexpr const char* name() { return "Acid bass"; }
    static constexpr uint32_t capabilities() {
        using namespace SugarAudio;
        return capabilityBit(INSTRUMENT_CAP_NOTES)|capabilityBit(INSTRUMENT_CAP_VOICE_MODES)|capabilityBit(INSTRUMENT_CAP_PORTAMENTO);
    }
    static constexpr SugarParameters::Descriptor modeDescriptor{
        SugarParameters::PARAM_VOICE_MODE,"Voice mode","",SugarParameters::PARAM_SCOPE_PART,SugarParameters::PARAM_KIND_CHOICE,
        SugarAudio::VOICE_MODE_MONO_FULL,SugarAudio::VOICE_MODE_MONO_LEGATO,SugarAudio::VOICE_MODE_MONO_LEGATO};
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        using namespace SugarParameters;
        switch(id) {
        case PARAM_VOICE_MODE: return &modeDescriptor;
        case PARAM_GAIN: case PARAM_PORTAMENTO_MS: case PARAM_WAVEFORM: case PARAM_CUTOFF:
        case PARAM_RESONANCE: case PARAM_ENV_MOD: case PARAM_DECAY_MS: return descriptor(id);
        default: return nullptr;
        }
    }
    static constexpr int32_t defaultParameter(SugarParameters::eParameterId id) {
        return parameterDescriptor(id) ? parameterDescriptor(id)->defaultValue : 0;
    }
    static uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t rate);
    enum eParameter : uint8_t { CONTROL_VOICE_MODE, CONTROL_PORTAMENTO_MS, CONTROL_GAIN, CONTROL_WAVEFORM, CONTROL_CUTOFF, CONTROL_RESONANCE, CONTROL_ENV_MOD, CONTROL_DECAY_MS, CONTROLS_COUNT };
    static constexpr unsigned parameterCount() { return CONTROLS_COUNT; }
    static constexpr SugarParameters::eParameterId legacyIds[CONTROLS_COUNT]={SugarParameters::PARAM_VOICE_MODE,SugarParameters::PARAM_PORTAMENTO_MS,SugarParameters::PARAM_GAIN,SugarParameters::PARAM_WAVEFORM,SugarParameters::PARAM_CUTOFF,SugarParameters::PARAM_RESONANCE,SugarParameters::PARAM_ENV_MOD,SugarParameters::PARAM_DECAY_MS};
    static constexpr const char* modeChoices[]={"Poly","Mono Full","Mono Legato"};
    static constexpr const char* waveChoices[]={"Saw","Square"};
    static constexpr SugarSynth::Parameter makeParameter(unsigned id) {
        const auto legacy=legacyIds[id];
        const auto* d=parameterDescriptor(legacy);
        return {parameterKeys[id],legacy==SugarParameters::PARAM_GAIN ? "Gain" : legacy==SugarParameters::PARAM_VOICE_MODE ? "Mode" : legacy==SugarParameters::PARAM_PORTAMENTO_MS ? "Glide" : d->name,d->unit,d->scope,d->minimum,d->maximum,defaultParameter(legacy),
            legacy==SugarParameters::PARAM_PORTAMENTO_MS || legacy==SugarParameters::PARAM_DECAY_MS ? 10 : 1,
            legacy==SugarParameters::PARAM_VOICE_MODE ? modeChoices : legacy==SugarParameters::PARAM_WAVEFORM ? waveChoices : nullptr,
            legacy==SugarParameters::PARAM_VOICE_MODE ? unsigned(SugarAudio::VOICE_MODES_COUNT) : legacy==SugarParameters::PARAM_WAVEFORM ? unsigned(SugarParameters::WAVEFORMS_COUNT) : 0U,
            int16_t(legacy==SugarParameters::PARAM_GAIN ? 7 : legacy==SugarParameters::PARAM_CUTOFF ? 74 : legacy==SugarParameters::PARAM_RESONANCE ? 71 : -1),d};
    }
    static constexpr const char* parameterKeys[CONTROLS_COUNT]={"voice_mode","portamento_ms","gain","waveform","cutoff","resonance","env_mod","decay_ms"};
    static const SugarSynth::Parameter parameters[CONTROLS_COUNT];
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<CONTROLS_COUNT ? &parameters[id] : nullptr; }
    static uint32_t prepareLocalParameter(unsigned id,int32_t value,uint32_t rate) { return prepareParameter(legacyIds[id],value,rate); }
    bool __attribute__((always_inline)) applyLocalParameter(const SugarMidi::Event& event) {
        auto legacy=event; legacy.type=SugarMidi::EVENT_PARAMETER;
        switch(event.data1) {
        case CONTROL_VOICE_MODE: legacy.data1=SugarParameters::PARAM_VOICE_MODE; break;
        case CONTROL_PORTAMENTO_MS: legacy.data1=SugarParameters::PARAM_PORTAMENTO_MS; break;
        case CONTROL_GAIN: legacy.data1=SugarParameters::PARAM_GAIN; break;
        case CONTROL_WAVEFORM: legacy.data1=SugarParameters::PARAM_WAVEFORM; break;
        case CONTROL_CUTOFF: legacy.data1=SugarParameters::PARAM_CUTOFF; break;
        case CONTROL_RESONANCE: legacy.data1=SugarParameters::PARAM_RESONANCE; break;
        case CONTROL_ENV_MOD: legacy.data1=SugarParameters::PARAM_ENV_MOD; break;
        case CONTROL_DECAY_MS: legacy.data1=SugarParameters::PARAM_DECAY_MS; break;
        default: return false;
        }
        return applyParameter(legacy);
    }
    void prepare();
    void silence();
    void onEvent(const SugarMidi::Event& event);
    bool applyParameter(const SugarMidi::Event& event);
    void render(SugarAudio::AudioBlock output);
private:
    SugarAudio::VoiceAllocator<1> allocator_{SugarAudio::VOICE_MODE_MONO_LEGATO};
    uint32_t generation_=0,phase_=0,increment_=0,target_=0,remaining_=0,glideFrames_=0,reciprocal_=0;
    int32_t glideStep_=0,level_=0,poles_[4]={};
    uint32_t envelope_=0,decay_=1;
    uint8_t gain_[SugarParameters::MIDI_CHANNELS_COUNT]={};
    int32_t cutoff_=45,resonance_=75,envMod_=70;
    SugarParameters::eWaveform waveform_=SugarParameters::WAVEFORM_SAW;
};
inline constexpr SugarSynth::Parameter Instrument::parameters[Instrument::CONTROLS_COUNT]={Instrument::makeParameter(Instrument::CONTROL_VOICE_MODE),Instrument::makeParameter(Instrument::CONTROL_PORTAMENTO_MS),Instrument::makeParameter(Instrument::CONTROL_GAIN),Instrument::makeParameter(Instrument::CONTROL_WAVEFORM),Instrument::makeParameter(Instrument::CONTROL_CUTOFF),Instrument::makeParameter(Instrument::CONTROL_RESONANCE),Instrument::makeParameter(Instrument::CONTROL_ENV_MOD),Instrument::makeParameter(Instrument::CONTROL_DECAY_MS)};

}

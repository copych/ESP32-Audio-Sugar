#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../audio/Instrument.h"
#include "../audio/InstrumentCapabilities.h"
#include "../parameters/ParameterRegistry.h"
#include "../parameters/SynthParameter.h"
#include "SynthDsp.h"
namespace DrumSynth {
enum eDrum : uint8_t { DRUM_KICK, DRUM_SNARE, DRUM_CLOSED_HAT, DRUM_OPEN_HAT, DRUM_TOM, DRUMS_COUNT };
constexpr eDrum drumForNote(unsigned note) {
    switch(note) {
    case 35: case 36: return DRUM_KICK;
    case 38: case 40: return DRUM_SNARE;
    case 42: case 44: case 69: return DRUM_CLOSED_HAT;
    case 46: return DRUM_OPEN_HAT;
    case 45: case 47: case 48: return DRUM_TOM;
    default: return DRUMS_COUNT;
    }
}
class Instrument {
public:
    static constexpr const char* key() { return "sugar.drums.v1"; }
    static constexpr const char* name() { return "Drum kit"; }
    static constexpr uint32_t capabilities() {
        return SugarAudio::capabilityBit(SugarAudio::INSTRUMENT_CAP_NOTES)|SugarAudio::capabilityBit(SugarAudio::INSTRUMENT_CAP_POLYPHONY);
    }
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        return id==SugarParameters::PARAM_GAIN || id==SugarParameters::PARAM_DECAY_MS ? SugarParameters::descriptor(id) : nullptr;
    }
    static constexpr int32_t defaultParameter(SugarParameters::eParameterId id) {
        return parameterDescriptor(id) ? parameterDescriptor(id)->defaultValue : 0;
    }
    static constexpr uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t rate) {
        return id==SugarParameters::PARAM_DECAY_MS ? SugarSynthDsp::decayStep(value,rate) : 0;
    }
    enum eParameter : uint8_t { CONTROL_GAIN, CONTROL_DECAY_MS, CONTROLS_COUNT };
    static constexpr unsigned parameterCount() { return CONTROLS_COUNT; }
    static constexpr SugarParameters::eParameterId legacyIds[CONTROLS_COUNT]={SugarParameters::PARAM_GAIN,SugarParameters::PARAM_DECAY_MS};
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
    static constexpr const char* parameterKeys[CONTROLS_COUNT]={"gain","decay_ms"};
    static const SugarSynth::Parameter parameters[CONTROLS_COUNT];
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<CONTROLS_COUNT ? &parameters[id] : nullptr; }
    static uint32_t prepareLocalParameter(unsigned id,int32_t value,uint32_t rate) { return prepareParameter(legacyIds[id],value,rate); }
    bool __attribute__((always_inline)) applyLocalParameter(const SugarMidi::Event& event) {
        auto legacy=event; legacy.type=SugarMidi::EVENT_PARAMETER;
        switch(event.data1) {
        case CONTROL_GAIN: legacy.data1=SugarParameters::PARAM_GAIN; break;
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
    struct Voice { uint32_t phase=0,envelope=0; uint8_t channel=0,velocity=0; };
    Voice voices_[DRUMS_COUNT];
    uint32_t steps_[DRUMS_COUNT]={},random_=0x739AC51U;
    int32_t previousNoise_=0;
    uint8_t gain_[SugarParameters::MIDI_CHANNELS_COUNT]={};
};
inline constexpr SugarSynth::Parameter Instrument::parameters[Instrument::CONTROLS_COUNT]={Instrument::makeParameter(Instrument::CONTROL_GAIN),Instrument::makeParameter(Instrument::CONTROL_DECAY_MS)};

}

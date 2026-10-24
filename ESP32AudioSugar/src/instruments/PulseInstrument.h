#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../audio/Instrument.h"
#include "../audio/InstrumentCapabilities.h"
#include "../parameters/ParameterRegistry.h"
#include "../parameters/SynthParameter.h"
namespace PulseInstrument {
class Instrument {
public:
    static constexpr const char* key() { return "sugar.pulse.v1"; }
    static constexpr int32_t minWidth=10, maxWidth=90;
    static constexpr SugarParameters::Descriptor widthDescriptor{
        SugarParameters::PARAM_PULSE_WIDTH,"Pulse width","%",SugarParameters::PARAM_SCOPE_PART,
        SugarParameters::PARAM_KIND_INTEGER,minWidth,maxWidth,50};
    static constexpr const char* name() { return "Pulse synth"; }
    static constexpr uint32_t capabilities() { return SugarAudio::capabilityBit(SugarAudio::INSTRUMENT_CAP_NOTES); }
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        using namespace SugarParameters;
        return id==PARAM_GAIN ? descriptor(id) : id==PARAM_PULSE_WIDTH ? &widthDescriptor : nullptr;
    }
    static constexpr int32_t defaultParameter(SugarParameters::eParameterId id) {
        return parameterDescriptor(id) ? parameterDescriptor(id)->defaultValue : 0;
    }
    static constexpr uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t) {
        return id==SugarParameters::PARAM_PULSE_WIDTH ? uint32_t((uint64_t(uint32_t(value))<<32)/100U) : 0;
    }
    enum eParameter : uint8_t { CONTROL_PULSE_WIDTH, CONTROL_GAIN, CONTROLS_COUNT };
    static constexpr unsigned parameterCount() { return CONTROLS_COUNT; }
    static constexpr SugarParameters::eParameterId legacyIds[CONTROLS_COUNT]={SugarParameters::PARAM_PULSE_WIDTH,SugarParameters::PARAM_GAIN};
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
    static constexpr const char* parameterKeys[CONTROLS_COUNT]={"pulse_width","gain"};
    static const SugarSynth::Parameter parameters[CONTROLS_COUNT];
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<CONTROLS_COUNT ? &parameters[id] : nullptr; }
    static uint32_t prepareLocalParameter(unsigned id,int32_t value,uint32_t rate) { return prepareParameter(legacyIds[id],value,rate); }
    bool __attribute__((always_inline)) applyLocalParameter(const SugarMidi::Event& event) {
        auto legacy=event; legacy.type=SugarMidi::EVENT_PARAMETER;
        switch(event.data1) {
        case CONTROL_PULSE_WIDTH: legacy.data1=SugarParameters::PARAM_PULSE_WIDTH; break;
        case CONTROL_GAIN: legacy.data1=SugarParameters::PARAM_GAIN; break;
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
    uint32_t phase_=0, increment_=0, threshold_=0x80000000U;
    int32_t level_=0, positiveScale_=32768, negativeScale_=32768;
    uint8_t channel_=0, note_=0, velocity_=0;
    uint8_t gain_[SugarParameters::MIDI_CHANNELS_COUNT];
    bool active_=false;
};
inline constexpr SugarSynth::Parameter Instrument::parameters[Instrument::CONTROLS_COUNT]={Instrument::makeParameter(Instrument::CONTROL_PULSE_WIDTH),Instrument::makeParameter(Instrument::CONTROL_GAIN)};

}

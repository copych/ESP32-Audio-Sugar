#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../audio/Instrument.h"
#include "../audio/VoiceAllocator.h"
#include "../parameters/ParameterRegistry.h"
#include "../parameters/SynthParameter.h"
#include "../audio/InstrumentCapabilities.h"
namespace TestInstrument {
class Instrument {
public:
    static constexpr const char* key() { return "sugar.sine.v1"; }
    static constexpr const char* name() { return "Sine synth"; }
    static constexpr uint32_t capabilities() {
        using namespace SugarAudio;
        return capabilityBit(INSTRUMENT_CAP_NOTES)|capabilityBit(INSTRUMENT_CAP_POLYPHONY)|
            capabilityBit(INSTRUMENT_CAP_VOICE_MODES)|capabilityBit(INSTRUMENT_CAP_PORTAMENTO);
    }
    static constexpr const SugarParameters::Descriptor* parameterDescriptor(SugarParameters::eParameterId id) {
        using namespace SugarParameters;
        return id==PARAM_VOICE_MODE || id==PARAM_PORTAMENTO_MS || id==PARAM_GAIN ? descriptor(id) : nullptr;
    }
    static constexpr int32_t defaultParameter(SugarParameters::eParameterId id) {
        using namespace SugarParameters;
        return id==PARAM_VOICE_MODE ? int32_t(SUGAR_VOICE_MODE) : id==PARAM_PORTAMENTO_MS ? SUGAR_PORTAMENTO_MS :
            parameterDescriptor(id) ? parameterDescriptor(id)->defaultValue : 0;
    }
    static constexpr uint32_t prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t sampleRate) {
        return id==SugarParameters::PARAM_PORTAMENTO_MS ? SugarParameters::glideReciprocal(value,sampleRate) : 0;
    }
    enum eParameter : uint8_t { CONTROL_VOICE_MODE, CONTROL_PORTAMENTO_MS, CONTROL_GAIN, CONTROLS_COUNT };
    static constexpr unsigned parameterCount() { return CONTROLS_COUNT; }
    static constexpr SugarParameters::eParameterId legacyIds[CONTROLS_COUNT]={SugarParameters::PARAM_VOICE_MODE,SugarParameters::PARAM_PORTAMENTO_MS,SugarParameters::PARAM_GAIN};
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
    static constexpr const char* parameterKeys[CONTROLS_COUNT]={"voice_mode","portamento_ms","gain"};
    static const SugarSynth::Parameter parameters[CONTROLS_COUNT];
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<CONTROLS_COUNT ? &parameters[id] : nullptr; }
    static uint32_t prepareLocalParameter(unsigned id,int32_t value,uint32_t rate) { return prepareParameter(legacyIds[id],value,rate); }
    bool __attribute__((always_inline)) applyLocalParameter(const SugarMidi::Event& event) {
        auto legacy=event; legacy.type=SugarMidi::EVENT_PARAMETER;
        switch(event.data1) {
        case CONTROL_VOICE_MODE: legacy.data1=SugarParameters::PARAM_VOICE_MODE; break;
        case CONTROL_PORTAMENTO_MS: legacy.data1=SugarParameters::PARAM_PORTAMENTO_MS; break;
        case CONTROL_GAIN: legacy.data1=SugarParameters::PARAM_GAIN; break;
        default: return false;
        }
        return applyParameter(legacy);
    }
    void prepare();
    void onEvent(const SugarMidi::Event& event);
    bool applyParameter(const SugarMidi::Event& event);
    void silence();
    void render(SugarAudio::AudioBlock output);
private:
    SugarAudio::VoiceAllocator<SUGAR_VOICES> allocator_{SugarAudio::eVoiceMode(SUGAR_VOICE_MODE)};
    struct DspVoice {
        uint32_t phase=0, increment=0, targetIncrement=0, glideRemaining=0, generation=0;
        int32_t glideStep=0, level=0;
    } voices_[SUGAR_VOICES];
    uint8_t gain_[16];
    uint32_t tonePosition_=0;
    uint32_t glideFrames_=0, glideReciprocal_=0;
};
inline constexpr SugarSynth::Parameter Instrument::parameters[Instrument::CONTROLS_COUNT]={Instrument::makeParameter(Instrument::CONTROL_VOICE_MODE),Instrument::makeParameter(Instrument::CONTROL_PORTAMENTO_MS),Instrument::makeParameter(Instrument::CONTROL_GAIN)};

}





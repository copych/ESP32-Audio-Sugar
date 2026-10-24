#pragma once
#include "PulseInstrument.h"
#include "../parameters/SynthParameter.h"
namespace ParameterExample {
// Integration example: native local parameters around the existing pulse DSP.
// No legacy descriptor/profile methods are required by this instrument.
class Instrument {
    PulseInstrument::Instrument voice;
public:
    enum eParameter : uint8_t { PARAM_SHAPE, PARAM_TRIM, PARAMETERS_COUNT };
    static constexpr SugarSynth::Parameter parameters[PARAMETERS_COUNT]={
        {"shape","Shape","%",SugarParameters::PARAM_SCOPE_PART,10,90,50,1,nullptr,0,12},
        {"trim","Trim","",SugarParameters::PARAM_SCOPE_PART_CHANNEL,0,127,127,1,nullptr,0,7}};
    static constexpr unsigned parameterCount() { return PARAMETERS_COUNT; }
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &parameters[id] : nullptr; }
    static constexpr const char* key() { return "sugar.parameter-example.v1"; }
    static constexpr const char* name() { return "Local params"; }
    static constexpr uint32_t capabilities() { return SugarAudio::capabilityBit(SugarAudio::INSTRUMENT_CAP_NOTES); }
    static constexpr uint32_t prepareLocalParameter(unsigned id,int32_t value,uint32_t rate) {
        return PulseInstrument::Instrument::prepareParameter(id==PARAM_SHAPE ? SugarParameters::PARAM_PULSE_WIDTH : SugarParameters::PARAM_GAIN,value,rate);
    }
    void prepare() { voice.prepare(); }
    void silence() { voice.silence(); }
    void onEvent(const SugarMidi::Event& event) { voice.onEvent(event); }
    bool applyLocalParameter(const SugarMidi::Event& event) {
        if(event.data1>=PARAMETERS_COUNT) return false;
        auto mapped=event;
        mapped.data1=event.data1==PARAM_SHAPE ? SugarParameters::PARAM_PULSE_WIDTH : SugarParameters::PARAM_GAIN;
        return voice.applyParameter(mapped);
    }
    void render(SugarAudio::AudioBlock output) { voice.render(output); }
};
}

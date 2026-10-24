#pragma once
#include "../parameters/SynthParameter.h"
#include "InstrumentCapabilities.h"
namespace SugarAudio {
template<typename Instrument> constexpr const SugarParameters::Descriptor* descriptorFor(
    const Instrument&,SugarParameters::eParameterId id) {
    const auto local=SugarSynth::legacyLocal<Instrument>(id);
    return local<Instrument::parameterCount() ? Instrument::parameter(local)->legacy : nullptr;
}
template<typename Instrument> constexpr int32_t defaultFor(const Instrument&,SugarParameters::eParameterId id) {
    const auto local=SugarSynth::legacyLocal<Instrument>(id);
    return local<Instrument::parameterCount() ? Instrument::parameter(local)->defaultValue : 0;
}
template<typename Instrument> constexpr const char* nameFor(const Instrument&) { return Instrument::name(); }
template<typename Instrument> constexpr uint32_t capabilitiesFor(const Instrument&) { return Instrument::capabilities(); }
template<typename Instrument> constexpr uint32_t prepareFor(const Instrument&,SugarParameters::eParameterId id,
                                                           int32_t value,uint32_t sampleRate) {
    const auto local=SugarSynth::legacyLocal<Instrument>(id);
    return local<Instrument::parameterCount() ? Instrument::prepareLocalParameter(local,value,sampleRate) : 0;
}
template<typename Instrument> constexpr bool validProfile() {
    if(!SugarSynth::validParameters<Instrument>()) return false;
    using namespace SugarParameters;
    if(!Instrument::name() || !Instrument::name()[0]) return false;
    if(!Instrument::key() || !Instrument::key()[0]) return false;
    if(Instrument::capabilities()&~((uint32_t(1)<<INSTRUMENT_CAPABILITIES_COUNT)-1)) return false;
    return true;
}
}

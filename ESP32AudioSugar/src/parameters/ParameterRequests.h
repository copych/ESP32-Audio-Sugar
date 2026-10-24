#pragma once
#include "ParameterRegistry.h"
#include "../midi/MidiEvent.h"

namespace SugarParameters {
template<typename Queue>
constexpr bool submitPrepared(State& requested,Queue& queue,const Descriptor* d,const SugarMidi::Event& event) {
    const bool local=event.type==SugarMidi::EVENT_SYNTH_PARAMETER;
    const auto id=eParameterId(local ? event.data2 : event.data1);
    if((!local && event.type!=SugarMidi::EVENT_PARAMETER) || unsigned(event.part)>=SugarParts::PARTS_COUNT ||
       !validDescriptor(d,id,event.value,event.channel)) return false;
    if(!queue.push(event)) return false;
    return requested.setWithDescriptor(d,id,event.value,event.channel);
}
constexpr SugarMidi::Event makeEvent(eParameterId id,int32_t value,uint8_t channel,uint32_t sampleRate,SugarParts::ePartId part=SugarParts::PART_1) {
    return {SugarMidi::EVENT_PARAMETER,channel,uint8_t(id),0,value,
        id==PARAM_PORTAMENTO_MS ? glideReciprocal(value,sampleRate) :
        id==PARAM_PULSE_WIDTH ? uint32_t((uint64_t(uint32_t(value))<<32)/100U) : 0,part};
}
// Sole producer only. Failed validation or a full queue leaves requested state
// unchanged. Prepared portamento arithmetic runs on Core 1.
template<typename Queue>
constexpr bool request(State& requested,Queue& queue,eParameterId id,int32_t value,
                       uint8_t channel,uint32_t sampleRate,SugarParts::ePartId part=SugarParts::PART_1,
                       const Descriptor* specification=nullptr) {
    const auto* d=specification ? specification : descriptor(id);
    if(!validDescriptor(d,id,value,channel) || !sampleRate || unsigned(part)>=SugarParts::PARTS_COUNT) return false;
    return submitPrepared(requested,queue,d,makeEvent(id,value,channel,sampleRate,part));
}
}



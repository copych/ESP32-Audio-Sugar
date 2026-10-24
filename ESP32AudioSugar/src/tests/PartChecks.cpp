#include "../parts/PartRouting.h"
#include "../parameters/ParameterRequests.h"
#include "../midi/EventQueue.h"
namespace {
using namespace SugarParts;
constexpr bool routingChecks() {
    Routing<2> routing;
    const uint32_t both=partBit(PART_1)|partBit(PART_2);
    for(unsigned channel=0;channel<MIDI_CHANNELS_COUNT;++channel)
        if(routing.destinations(channel,both)!=partBit(PART_1)) return false;
    if(!routing.set(1,partBit(PART_2)) || routing.destinations(1,both)!=partBit(PART_2)) return false;
    if(routing.destinations(1,partBit(PART_1))) return false;
    if(!routing.set(2,both) || routing.destinations(2,both)!=both) return false;
    if(!routing.set(3,0) || routing.destinations(3,both)) return false;
    if(routing.set(MIDI_CHANNELS_COUNT,both) || routing.set(0,partBit(PART_3))) return false;
    if(routing.destinations(MIDI_CHANNELS_COUNT,both) || partBit(PARTS_COUNT)) return false;
    Routing<1> single;
    return !single.set(0,partBit(PART_2)) && single.destinations(0,both)==partBit(PART_1);
}
struct PartTestWord {
    uint32_t value=0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t v) { value=v; }
};
constexpr bool parameterIsolationChecks() {
    using namespace SugarParameters;
    State states[2];
    SugarMidi::EventQueue<4,PartTestWord> queue;
    if(!request(states[PART_2],queue,PARAM_VOICE_MODE,SugarAudio::VOICE_MODE_MONO_FULL,0,44100,PART_2)) return false;
    int32_t value=-1;
    if(!states[PART_1].get(PARAM_VOICE_MODE,value) || value!=SugarAudio::VOICE_MODE_POLY) return false;
    SugarMidi::Event e{};
    if(!queue.pop(e) || e.part!=PART_2 || e.channel!=0) return false;
    if(!request(states[PART_2],queue,PARAM_GAIN,90,5,44100,PART_2)) return false;
    if(!queue.pop(e) || e.part!=PART_2 || e.channel!=5 || e.value!=90) return false;
    if(!states[PART_1].get(PARAM_GAIN,value,5) || value!=MAX_GAIN) return false;
    return !request(states[PART_1],queue,PARAM_GAIN,10,0,44100,PARTS_COUNT);
}
constexpr bool queuedIdentityChecks() {
    SugarMidi::EventQueue<4,PartTestWord> queue;
    SugarMidi::Event first{SugarMidi::EVENT_NOTE_ON,7,60,100}; first.part=PART_1;
    auto second=first; second.part=PART_2;
    if(!queue.push(first) || !queue.push(second)) return false;
    SugarMidi::Event read{};
    if(!queue.pop(read) || read.part!=PART_1 || read.channel!=7) return false;
    if(!queue.pop(read) || read.part!=PART_2 || read.channel!=7 || read.data1!=60) return false;
    return true;
}
static_assert(routingChecks(),"Part routing/mute/layer/capacity contract failed");
static_assert(parameterIsolationChecks(),"Part parameter isolation contract failed");
static_assert(queuedIdentityChecks(),"Queued part/channel identity contract failed");
}

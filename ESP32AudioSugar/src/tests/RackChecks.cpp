#include "../audio/StaticRack.h"
#include "../audio/InstrumentProfile.h"
#include "../instruments/TestInstrument.h"
#include "../instruments/PulseInstrument.h"
#include "../parameters/ParameterRequests.h"
#include "../midi/EventQueue.h"
namespace {
// Distinct shapes verify dispatch without relying on a homogeneous base class.
struct RackFirst { int32_t note=-1; constexpr void set(int32_t n) { note=n; } };
struct RackSecond { uint8_t unrelated[7]={}; int32_t note=-2; constexpr void set(int32_t n) { note=n+12; } };
constexpr bool heterogeneousChecks() {
    SugarAudio::StaticRack<RackFirst,RackSecond> rack;
    if(rack.count!=SugarParts::PART_2+1) return false;
    if(!rack.visit(SugarParts::PART_2,[](auto& instrument) { instrument.set(60); })) return false;
    int32_t first=0,second=0;
    rack.visit(SugarParts::PART_1,[&](auto& instrument) { first=instrument.note; });
    rack.visit(SugarParts::PART_2,[&](auto& instrument) { second=instrument.note; });
    if(first!=-1 || second!=72) return false;
    unsigned visited=0;
    if(rack.visit(rack.count,[&](auto&) { ++visited; }) || visited) return false;
    rack.forEach([&](auto& instrument) { ++visited; instrument.set(40); });
    if(visited!=rack.count) return false;
    rack.visit(SugarParts::PART_1,[&](auto& instrument) { first=instrument.note; });
    rack.visit(SugarParts::PART_2,[&](auto& instrument) { second=instrument.note; });
    return first==40 && second==52;
}
constexpr bool profileChecks() {
    using namespace SugarAudio;
    using namespace SugarParameters;
    using Sine=TestInstrument::Instrument;
    using Pulse=PulseInstrument::Instrument;
    if(!validProfile<Sine>() || !validProfile<Pulse>()) return false;
    if(Sine::parameterDescriptor(PARAM_VOICE_MODE)->id!=PARAM_VOICE_MODE || Sine::parameterDescriptor(PARAM_PULSE_WIDTH)) return false;
    if(Pulse::parameterDescriptor(PARAM_VOICE_MODE) || Pulse::parameterDescriptor(PARAM_PORTAMENTO_MS)) return false;
    if(Pulse::capabilities()&capabilityBit(INSTRUMENT_CAP_POLYPHONY)) return false;
    const auto* width=Pulse::parameterDescriptor(PARAM_PULSE_WIDTH);
    return validDescriptor(width,PARAM_PULSE_WIDTH,50) && !validDescriptor(width,PARAM_PULSE_WIDTH,1) &&
        !validDescriptor(width,PARAM_PULSE_WIDTH,99) && !validDescriptor(width,PARAM_PULSE_WIDTH,50,1);
}
struct RackWord {
    uint32_t value=0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t v) { value=v; }
};
constexpr bool customParameterChecks() {
    using namespace SugarParameters;
    State sine,pulse;
    SugarMidi::EventQueue<4,RackWord> queue;
    const auto* width=PulseInstrument::Instrument::parameterDescriptor(PARAM_PULSE_WIDTH);
    const SugarMidi::Event prepared{SugarMidi::EVENT_PARAMETER,0,PARAM_PULSE_WIDTH,0,25,
        PulseInstrument::Instrument::prepareParameter(PARAM_PULSE_WIDTH,25,44100),SugarParts::PART_2};
    if(!submitPrepared(pulse,queue,width,prepared)) return false;
    int32_t value=0;
    if(!sine.get(PARAM_PULSE_WIDTH,value) || value!=50) return false;
    if(request(pulse,queue,PARAM_PULSE_WIDTH,1,0,44100,SugarParts::PART_2,width)) return false;
    SugarMidi::Event event{};
    if(!queue.pop(event) || event.part!=SugarParts::PART_2 || event.value!=25 || event.prepared!=0x40000000U) return false;
    if(queue.pop(event)) return false;
    if(submitPrepared(pulse,queue,nullptr,prepared)) return false;
    if(TestInstrument::Instrument::prepareParameter(PARAM_PORTAMENTO_MS,100,44100)!=glideReciprocal(100,44100)) return false;
    // A compatible instrument descriptor can also widen a catalog range.
    constexpr Descriptor custom{PARAM_GAIN,"Custom gain","MIDI",PARAM_SCOPE_PART_CHANNEL,PARAM_KIND_INTEGER,0,255,127};
    if(!request(pulse,queue,PARAM_GAIN,200,2,44100,SugarParts::PART_2,&custom)) return false;
    return pulse.get(PARAM_GAIN,value,2) && value==200;
}
static_assert(heterogeneousChecks(),"Static heterogeneous rack dispatch/isolation failed");
static_assert(profileChecks(),"Optional instrument capability/parameter contract failed");
static_assert(customParameterChecks(),"Custom range/prepared payload/part isolation failed");
}


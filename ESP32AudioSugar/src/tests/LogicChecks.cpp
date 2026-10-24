#include "../midi/EventQueue.h"
#include "../midi/EventDispatch.h"
#include "../audio/VoiceAllocator.h"

namespace {
// Compiler-evaluated PC checks: sequential logic, not Xtensa synchronization.
struct PlainWord {
    uint32_t value = 0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t v) { value = v; }
};
constexpr bool parserChecks() {
    using namespace SugarMidi;
    Event e{};
    if (!decode(9,0x92,69,100,e) || e.type != EVENT_NOTE_ON || e.channel != 2) return false;
    if (!decode(9,0x92,69,0,e) || e.type != EVENT_NOTE_OFF) return false;
    if (!decode(8,0x82,69,64,e) || e.type != EVENT_NOTE_OFF) return false;
    if (!decode(11,0xB2,7,99,e) || e.type != EVENT_CONTROL_CHANGE) return false;
    if (!decode(11,0xB2,120,0,e) || e.type != EVENT_ALL_OFF) return false;
    if (!decode(11,0xB2,123,0,e) || e.type != EVENT_ALL_OFF) return false;
    constexpr uint8_t values[]={0,127};
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) {
        for(uint8_t value:values) {
            if(!decode(11,uint8_t(0xB0|ch),74,value,e) || e.type!=EVENT_CONTROL_CHANGE ||
                e.channel!=ch || e.data1!=74 || e.data2!=value) return false;
            if(!decode(11,uint8_t(0xB0|ch),71,value,e) || e.type!=EVENT_CONTROL_CHANGE ||
                e.channel!=ch || e.data1!=71 || e.data2!=value) return false;
        }
    }
    return !decode(0x19,0x90,60,100,e) && !decode(8,0x90,60,100,e) &&
           !decode(9,0x90,128,1,e) && !decode(9,0x90,60,128,e) &&
           !decode(11,0xB0,74,128,e) && !decode(0x1B,0xB0,71,0,e) && !decode(9,0xB0,74,1,e) &&
           decode(11,0xB0,64,127,e) && e.type==EVENT_CONTROL_CHANGE && !decode(15,0xF8,0,0,e);
}
constexpr bool queueChecks() {
    using namespace SugarMidi;
    EventQueue<4,PlainWord> q;
    Event e{};
    if (q.pop(e)) return false;
    for (uint8_t i=0;i<4;++i) if (!q.push({EVENT_NOTE_ON,0,i,100})) return false;
    if (q.push({EVENT_NOTE_OFF,0,0,0}) || q.overflows()!=1) return false;
    for (uint8_t i=0;i<2;++i) if (!q.pop(e) || e.data1!=i) return false;
    for (uint8_t i=4;i<6;++i) if (!q.push({EVENT_NOTE_ON,0,i,100})) return false;
    for (uint8_t i=2;i<6;++i) if (!q.pop(e) || e.data1!=i) return false;
    if (q.pop(e)) return false;
    q.push({EVENT_NOTE_ON,0,42,100}); q.discard();
    if (q.pop(e)) return false;
    return q.push({EVENT_NOTE_OFF,0,42,0}) && q.pop(e) && e.type==EVENT_NOTE_OFF;
}
static_assert(parserChecks(), "MIDI parser contract failed");
static_assert(queueChecks(), "Queue ordering/wrap/overflow contract failed");

struct DispatchInstrument {
    SugarAudio::VoiceAllocator<1> voices;
    constexpr void silence() { voices.silence(); }
    constexpr void onEvent(const SugarMidi::Event& e) {
        if (e.type==SugarMidi::EVENT_NOTE_ON) voices.noteOn(e.channel,e.data1,e.data2);
        else if(e.type==SugarMidi::EVENT_NOTE_OFF) voices.noteOff(e.channel,e.data1);
    }
};
constexpr bool dispatchChecks() {
    using namespace SugarMidi;
    EventQueue<4,PlainWord> q;
    DispatchInstrument voice;
    uint32_t seen = 0;
    q.push({EVENT_NOTE_ON,0,60,127});
    q.push({EVENT_NOTE_ON,0,61,127});
    applyPending<1>(q,voice,seen);
    if (!voice.voices.slot(0).active || voice.voices.slot(0).note != 60) return false;
    applyPending<1>(q,voice,seen);
    if (voice.voices.slot(0).note != 61) return false;
    for (uint8_t i=0;i<4;++i) q.push({EVENT_NOTE_ON,0,i,127});
    if (q.push({EVENT_NOTE_OFF,0,61,0})) return false;
    applyPending<1>(q,voice,seen);
    if (voice.voices.slot(0).active || seen!=1) return false;
    Event e{};
    if (q.pop(e)) return false;
    q.push({EVENT_NOTE_ON,0,69,127});
    applyPending<1>(q,voice,seen);
    return voice.voices.slot(0).active && voice.voices.slot(0).note==69;
}
static_assert(dispatchChecks(), "Event budget/overflow recovery contract failed");
}




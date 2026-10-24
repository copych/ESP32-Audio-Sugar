#pragma once
#include "../midi/MidiEvent.h"
namespace TestInstrument {
// Latest note wins; no held-note fallback or sustain pedal yet.
struct VoiceState {
    bool active = false;
    uint8_t note = 0, channel = 0, velocity = 0;
    uint8_t gain[16] = {127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};
    constexpr void silence() { active = false; }
    constexpr void onEvent(const SugarMidi::Event& event) {
        using SugarMidi::Type;
        if (event.channel > 15 || event.data1 > 127 || event.data2 > 127) return;
        switch (event.type) {
        case Type::NoteOn:
            if (!event.data2) {
                if (event.channel == channel && event.data1 == note) active = false;
            } else {
                channel = event.channel; note = event.data1;
                velocity = event.data2; active = true;
            }
            break;
        case Type::NoteOff:
            if (event.channel == channel && event.data1 == note) active = false;
            break;
        case Type::Gain: gain[event.channel] = event.data2; break;
        case Type::AllOff:
            if (event.channel == channel) active = false;
            break;
        }
    }
    constexpr int32_t targetLevel() const {
        return active ? (int32_t(velocity) * gain[channel] * 4125 + 8064) / 16129 : 0;
    }
};
}

#pragma once
#include <stdint.h>
#include "../parts/PartRouting.h"
#include "../parameters/ParameterRegistry.h"

namespace SugarMidi {
enum eController : uint8_t {
    CONTROLLER_GAIN, CONTROLLER_RESONANCE, CONTROLLER_CUTOFF,
    CONTROLLER_ALL_SOUND_OFF, CONTROLLER_ALL_NOTES_OFF, CONTROLLERS_COUNT
};
constexpr uint8_t controllerNumbers[CONTROLLERS_COUNT]={7,71,74,120,123};
enum eEventType : uint8_t {
    EVENT_NOTE_ON,
    EVENT_NOTE_OFF,
    EVENT_GAIN,
    EVENT_ALL_OFF,
    EVENT_VOICE_MODE,
    EVENT_PARAMETER,
    EVENT_PART_ACTIVE,
    EVENT_ROUTING_RESET,
    EVENT_FX_SELECT, EVENT_FX_CONTROL, EVENT_FX_BUS_CONTROL,
    EVENT_SYNTH_PARAMETER, EVENT_CONTROL_CHANGE,
    EVENTS_COUNT
};
struct Event {
    eEventType type;
    uint8_t channel; // zero-based
    uint8_t data1;
    uint8_t data2;
    int32_t value = 0;
    uint32_t prepared = 0;
    SugarParts::ePartId part = SugarParts::PART_1;
    uint16_t parameterOffset=0;
};
static_assert(sizeof(Event) == 16, "Keep queued events compact");

// USB-MIDI 1.0 cable 0 only. Unsupported CINs, status/CIN mismatches,
// malformed data bytes and other cables produce no instrument event.
constexpr bool decode(uint8_t header, uint8_t status, uint8_t a, uint8_t b, Event& out) {
    if ((header >> 4) != 0 || a > 127 || b > 127) return false;
    const uint8_t kind = status >> 4;
    if ((header & 15) != kind) return false;
    const uint8_t channel = status & 15;
    if (kind == 9) {
        out = {b ? EVENT_NOTE_ON : EVENT_NOTE_OFF, channel, a, b};
        return true;
    }
    if (kind == 8) {
        out = {EVENT_NOTE_OFF, channel, a, b};
        return true;
    }
    if (kind == 11) {
        if(a==controllerNumbers[CONTROLLER_ALL_SOUND_OFF] || a==controllerNumbers[CONTROLLER_ALL_NOTES_OFF]) {
            out={EVENT_ALL_OFF,channel,a,b}; return true;
        }
        if(a<120) { out={EVENT_CONTROL_CHANGE,channel,a,b}; return true; }

    }
    return false;
}
}






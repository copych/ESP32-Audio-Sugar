#pragma once
#include <stdint.h>
namespace SugarAudio {
enum eInstrumentCapability : uint8_t {
    INSTRUMENT_CAP_NOTES,
    INSTRUMENT_CAP_POLYPHONY,
    INSTRUMENT_CAP_VOICE_MODES,
    INSTRUMENT_CAP_PORTAMENTO,
    INSTRUMENT_CAPABILITIES_COUNT
};
constexpr uint32_t capabilityBit(eInstrumentCapability capability) {
    return capability<INSTRUMENT_CAPABILITIES_COUNT ? uint32_t(1)<<capability : 0;
}
}

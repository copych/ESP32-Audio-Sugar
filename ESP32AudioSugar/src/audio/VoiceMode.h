#pragma once
#include <stdint.h>

namespace SugarAudio {
enum eVoiceMode : uint8_t {
    VOICE_MODE_POLY,
    VOICE_MODE_MONO_FULL,
    VOICE_MODE_MONO_LEGATO,
    VOICE_MODES_COUNT
};

constexpr bool isValidVoiceMode(uint8_t mode) { return mode < VOICE_MODES_COUNT; }
}



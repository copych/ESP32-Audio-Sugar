#pragma once
#include <stddef.h>
#include "PresetState.h"
namespace SugarPresets {
bool encode(const State& state,char* output,size_t capacity,size_t& length);
bool decode(const char* input,size_t length,State& state);
bool codecChecks(); // Core1 startup checks; no filesystem or engine mutations
const char* codecCheckDetail();
}

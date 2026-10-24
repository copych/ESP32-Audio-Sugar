#pragma once
#include "../../Config.h"
namespace SugarPresetChecks {
#if SUGAR_PRESET_BOARD_CHECKS
bool tick(); // Core1; true while the diagnostic owns controls
void report();
#else
inline bool tick() { return false; }
inline void report() {}
#endif
}

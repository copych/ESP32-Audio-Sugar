#pragma once
#include <Arduino.h>
#include "../../Config.h"
namespace SugarMixChecks {
#if SUGAR_MIX_BOARD_CHECKS
bool tick();
void report();
#else
inline bool tick() { return false; }
inline void report() {}
#endif
}

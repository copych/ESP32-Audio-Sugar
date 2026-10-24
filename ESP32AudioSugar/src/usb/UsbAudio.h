#pragma once
#include <Arduino.h>
#include "../../Config.h"

namespace SugarUSB {
#if SUGAR_USB_AUDIO
bool prepare();
bool start();
void reportIfDue();
void pushPcm16(const int16_t* interleaved, size_t frames);
#else
inline bool prepare() { return true; }
inline bool start() { return true; }
inline void reportIfDue() {}
inline void pushPcm16(const int16_t*, size_t) {}
#endif
}





#pragma once
#include <Arduino.h>

namespace SugarAudio {
bool begin();
void report();
bool pauseForStorage(); // Core1: wait for Core0 to stop I2S outside render
bool resumeAfterStorage();
}




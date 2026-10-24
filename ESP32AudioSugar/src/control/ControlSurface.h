#pragma once
namespace SugarControl {
void begin(); // Core 1, after engine/audio initialization
void tick();  // sole Core 1 loop producer, alongside MIDI
void report();
}

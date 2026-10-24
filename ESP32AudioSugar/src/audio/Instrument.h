#pragma once
#include <stdint.h>
#include "../midi/MidiEvent.h"

namespace SugarAudio {
struct AudioBlock {
    int16_t* interleaved;
};
// Compile-time instrument contract (no virtual dispatch):
// Static control-side profile: name(), capabilities(), parameterCount(), parameter(id),
// prepareLocalParameter(id,value,sampleRate). Preparation runs
// on Core 1 and returns the event's prepared coefficient. See InstrumentProfile.h.
//   prepare()                 Core 1, before audio task starts
//   onEvent(const Event&)     Core 0, bounded block-boundary update
//   applyLocalParameter(Event)     Core 0, validate/apply prepared value; return success
//   silence()                 Core 0, overflow recovery
//   render(AudioBlock)        Core 0, exactly SUGAR_BLOCK_FRAMES stereo frames
// prepare owns allocation/precomputation; the remaining methods must not
// allocate, log, wait, access files or call unexamined math functions.
// AudioBlock memory belongs to the transport and is valid only for render().
}




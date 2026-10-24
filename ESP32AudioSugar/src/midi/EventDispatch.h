#pragma once
#include "MidiEvent.h"
namespace SugarMidi {
template<typename Queue, typename Instrument>
inline __attribute__((always_inline)) constexpr bool recoverOverflow(Queue& queue, Instrument& instrument, uint32_t& seen) {
    const uint32_t overflow = queue.overflows();
    if (overflow == seen) return false;
    queue.discard();
    instrument.silence();
    seen = overflow;
    return true;
}
template<unsigned Limit, typename Queue, typename Instrument>
inline __attribute__((always_inline)) constexpr void applyPending(
    Queue& queue, Instrument& instrument, uint32_t& seen) {
    static_assert(Limit > 0, "Event budget must be positive");
    if (recoverOverflow(queue, instrument, seen)) return;
    Event event{};
    for (unsigned i = 0; i < Limit && queue.pop(event); ++i) instrument.onEvent(event);
    recoverOverflow(queue, instrument, seen);
}
}



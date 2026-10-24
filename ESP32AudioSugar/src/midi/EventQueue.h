#pragma once
#include "MidiEvent.h"
#include "../core/SharedWord.h"

namespace SugarMidi {
template<uint32_t Capacity, typename Word = SugarCore::SharedWord> class EventQueue {
    static_assert(Capacity && !(Capacity & (Capacity - 1)), "Power of two required");
    static_assert(Capacity < 0x80000000U, "Invalid capacity");
public:
    // Core1: publish the entire bounded command transaction with one release.
    constexpr bool pushBatch(const Event* events,unsigned count) {
        const uint32_t head=head_.load();
        if(count>Capacity || count>Capacity-uint32_t(head-tail_.load())) {
            overflow_.store(overflow_.load()+1); return false;
        }
        for(unsigned i=0;i<count;++i) events_[(head+i)&(Capacity-1)]=events[i];
        head_.store(head+count); return true;
    }
    // Exactly one producer, the Core 1 control loop. No allocation/retry.
    constexpr bool push(const Event& event) {
        const uint32_t head = head_.load();
        if (uint32_t(head - tail_.load()) >= Capacity) {
            overflow_.store(overflow_.load() + 1);
            return false;
        }
        events_[head & (Capacity - 1)] = event;
        head_.store(head + 1);
        return true;
    }
    // Exactly one consumer, Core 0 at block boundaries.
    constexpr __attribute__((always_inline)) bool pop(Event& event) {
        const uint32_t tail = tail_.load();
        if (tail == head_.load()) return false;
        event = events_[tail & (Capacity - 1)];
        tail_.store(tail + 1);
        return true;
    }
    // Consumer only; never resets the producer's index.
    constexpr void discard() { tail_.store(head_.load()); }
    constexpr uint32_t overflows() const { return overflow_.load(); }
private:
    Event events_[Capacity] = {};
    Word head_, tail_, overflow_;
};
}





#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../core/SharedWord.h"


namespace SugarUSB {

#ifndef SUGAR_USB_RING_FRAMES
#define SUGAR_USB_RING_FRAMES 1024
#endif

using SugarCore::SharedWord;

class PcmRing {
public:
    static constexpr uint32_t CAPACITY = SUGAR_USB_RING_FRAMES;
    static constexpr uint32_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & MASK) == 0, "Power-of-two capacity required");
    static_assert(CAPACITY > 0 && CAPACITY < 0x80000000U, "Invalid ring capacity");

    // Exactly one producer (Core 0). A full ring drops this USB block only.
    inline __attribute__((always_inline)) bool push(const int16_t* pcm, uint32_t frames) {
        const uint32_t head = head_.load();
        const uint32_t tail = tail_.load();
        if (frames > CAPACITY || frames > CAPACITY - uint32_t(head - tail))
            return false;
        const uint32_t offset = head & MASK;
        const uint32_t first = frames < CAPACITY - offset ? frames : CAPACITY - offset;
        memcpy(pcm_ + offset * 2, pcm, first * 4);
        if (frames > first)
            memcpy(pcm_, pcm + first * 2, (frames - first) * 4);
        head_.store(head + frames);
        return true;
    }
    // Exactly one consumer (Core 1). Removes whole stereo frames.
    uint32_t pop(int16_t* pcm, uint32_t maxFrames) {
        const uint32_t tail = tail_.load();
        const uint32_t available = uint32_t(head_.load() - tail);
        const uint32_t frames = available < maxFrames ? available : maxFrames;
        const uint32_t offset = tail & MASK;
        const uint32_t first = frames < CAPACITY - offset ? frames : CAPACITY - offset;
        memcpy(pcm, pcm_ + offset * 2, first * 4);
        if (frames > first)
            memcpy(pcm + first * 2, pcm_, (frames - first) * 4);
        tail_.store(tail + frames);
        return frames;
    }
    uint32_t available() const {
        const uint32_t tail = tail_.load();
        return uint32_t(head_.load() - tail);
    }
    // Consumer only. Never resets the producer's index, even across reconnects.
    void discard() { tail_.store(head_.load()); }
private:
    alignas(4) int16_t pcm_[CAPACITY * 2] = {};
    SharedWord head_;
    SharedWord tail_;
};
}






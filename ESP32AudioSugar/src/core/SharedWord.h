#pragma once
#include <stdint.h>
#if defined(SUGAR_HOST_TEST) || defined(SUGAR_RING_HOST_TEST)
#include <atomic>
#endif

namespace SugarCore {
// S3 internal DRAM only. Host atomics model publication for logic tests;
// they do not validate Xtensa memory ordering on hardware.
class alignas(4) SharedWord {
public:
    SharedWord() = default;
    SharedWord(const SharedWord&) = delete;
    SharedWord& operator=(const SharedWord&) = delete;
    inline __attribute__((always_inline)) uint32_t load() const {
#if defined(SUGAR_HOST_TEST) || defined(SUGAR_RING_HOST_TEST)
        return value_.load(std::memory_order_acquire);
#else
        uint32_t result;
        __asm__ __volatile__("l32i %0, %1, 0\n\tmemw"
                             : "=a"(result) : "a"(&value_) : "memory");
        return result;
#endif
    }
    inline __attribute__((always_inline)) void store(uint32_t value) {
#if defined(SUGAR_HOST_TEST) || defined(SUGAR_RING_HOST_TEST)
        value_.store(value, std::memory_order_release);
#else
        __asm__ __volatile__("memw\n\ts32i %0, %1, 0"
                             : : "a"(value), "a"(&value_) : "memory");
#endif
    }
private:
#if defined(SUGAR_HOST_TEST) || defined(SUGAR_RING_HOST_TEST)
    std::atomic<uint32_t> value_{0};
#else
    alignas(4) uint32_t value_ = 0;
#endif
};
}




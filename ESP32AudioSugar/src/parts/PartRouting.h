#pragma once
#include <stdint.h>

namespace SugarParts {
enum ePartId : uint8_t { PART_1, PART_2, PART_3, PART_4, PARTS_COUNT };
constexpr unsigned MIDI_CHANNELS_COUNT=16;
constexpr uint32_t partBit(ePartId part) {
    return unsigned(part)<PARTS_COUNT ? uint32_t(1)<<part : 0;
}
template<unsigned Capacity> class Routing {
    static_assert(Capacity>0 && Capacity<=PARTS_COUNT && PARTS_COUNT<=32,"Invalid part capacity");
public:
    static constexpr uint32_t availableMask=~uint32_t(0)>>(32-Capacity);
    constexpr Routing() {
        for(auto& mask:routes_) mask=partBit(PART_1);
    }
    constexpr bool set(unsigned channel,uint32_t mask) {
        if(channel>=MIDI_CHANNELS_COUNT || (mask & ~availableMask)) return false;
        routes_[channel]=mask;
        return true;
    }
    constexpr uint32_t destinations(unsigned channel,uint32_t activeMask) const {
        return channel<MIDI_CHANNELS_COUNT ? routes_[channel]&activeMask&availableMask : 0;
    }
private:
    uint32_t routes_[MIDI_CHANNELS_COUNT]={};
};
}


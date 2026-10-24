#pragma once
#include <stdint.h>
#include "VoiceMode.h"

namespace SugarAudio {

struct VoiceSlot {
    bool active = false, held = false;
    bool retrigger = false, glide = false;
    uint8_t channel = 0, note = 0, velocity = 0;
    uint32_t generation = 0, started = 0;
};

// Allocation owns note identity/gates; the instrument owns DSP/envelopes.
// All calls belong to Core 0. No allocation, clocks, logging or locks.
template<unsigned Voices, unsigned HeldCapacity = 32> class VoiceAllocator {
    static_assert(Voices > 0 && Voices <= 32, "Choose 1..32 voices");
    static_assert(HeldCapacity > 0 && HeldCapacity <= 128, "Choose 1..128 held notes");
public:
    constexpr explicit VoiceAllocator(eVoiceMode mode = VOICE_MODE_POLY) : mode_(mode) {}
    constexpr eVoiceMode mode() const { return mode_; }
    constexpr const VoiceSlot& slot(unsigned i) const { return slots_[i]; }
    constexpr void setMode(eVoiceMode mode) {
        if (mode != mode_ && isValidVoiceMode(uint8_t(mode))) { silence(); mode_ = mode; }
    }
    constexpr void silence() {
        for (auto& s : slots_) { s.active = s.held = false; ++s.generation; }
        heldCount_ = 0;
    }
    constexpr void allOff(uint8_t channel, bool immediate) {
        for (unsigned i = 0; i < heldCount_;) {
            if (held_[i].channel == channel) removeHeld(i);
            else ++i;
        }
        for (auto& s : slots_) if (s.active && s.channel == channel) {
            s.held = false;
            if (immediate) { s.active = false; ++s.generation; }
        }
        if (mode_ != VOICE_MODE_POLY && heldCount_ && slots_[0].channel == channel)
            resumeHeld();
    }
    constexpr void retire(unsigned index) {
        if (!slots_[index].held) slots_[index].active = false;
    }
    constexpr int noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        if (channel > 15 || note > 127 || velocity > 127) return -1;
        if (!velocity) { noteOff(channel,note); return -1; }
        if (mode_ != VOICE_MODE_POLY) {
            const bool overlap = heldCount_ != 0;
            // Repeated note-on replaces its existing stack entry.
            for (unsigned i=0;i<heldCount_;++i)
                if (held_[i].channel==channel && held_[i].note==note) { removeHeld(i); break; }
            if (heldCount_ == HeldCapacity) removeHeld(0);
            held_[heldCount_++] = {channel,note,velocity};
            start(0,channel,note,velocity,!overlap,mode_==VOICE_MODE_MONO_FULL || overlap);
            return 0;
        }
        // Retrigger an already held identity rather than leaking duplicate voices.
        for (unsigned i=0;i<Voices;++i)
            if (slots_[i].active && slots_[i].held && slots_[i].channel==channel && slots_[i].note==note) {
                start(i,channel,note,velocity,true,false); return int(i);
            }
        for (unsigned i=0;i<Voices;++i) if (!slots_[i].active) {
            start(i,channel,note,velocity,true,false); return int(i);
        }
        // Released voices before held voices; oldest within each category.
        unsigned victim = 0;
        for (unsigned i=1;i<Voices;++i) {
            const auto& s=slots_[i]; const auto& v=slots_[victim];
            if ((!s.held && v.held) || (s.held==v.held && uint32_t(clock_-s.started)>uint32_t(clock_-v.started)))
                victim=i;
        }
        start(victim,channel,note,velocity,true,false);
        return int(victim);
    }
    constexpr void noteOff(uint8_t channel, uint8_t note) {
        if (mode_==VOICE_MODE_POLY) {
            for (auto& s:slots_) if (s.active && s.channel==channel && s.note==note) s.held=false;
            return;
        }
        bool found=false;
        for (unsigned i=0;i<heldCount_;++i)
            if (held_[i].channel==channel && held_[i].note==note) { removeHeld(i); found=true; break; }
        if (!found || slots_[0].channel!=channel || slots_[0].note!=note) return;
        if (heldCount_) resumeHeld();
        else slots_[0].held=false;
    }
private:
    struct Held { uint8_t channel,note,velocity; };
    eVoiceMode mode_;
    VoiceSlot slots_[Voices] = {};
    Held held_[HeldCapacity] = {};
    unsigned heldCount_ = 0;
    uint32_t clock_ = 0;
    constexpr void removeHeld(unsigned index) {
        for (unsigned i=index+1;i<heldCount_;++i) held_[i-1]=held_[i];
        --heldCount_;
    }
    constexpr void start(unsigned index,uint8_t channel,uint8_t note,uint8_t velocity,bool retrigger,bool glide) {
        auto& s=slots_[index];
        s.active=s.held=true; s.channel=channel; s.note=note; s.velocity=velocity;
        s.retrigger=retrigger; s.glide=glide; s.started=++clock_; ++s.generation;
    }
    constexpr void resumeHeld() {
        const auto& h=held_[heldCount_-1];
        start(0,h.channel,h.note,h.velocity,false,true);
    }
};
}




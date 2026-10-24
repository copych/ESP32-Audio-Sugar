#include "../audio/VoiceAllocator.h"
namespace {
using namespace SugarAudio;
constexpr bool polyChecks() {
    VoiceAllocator<3> a;
    if(a.noteOn(0,60,100)!=0 || a.noteOn(0,64,90)!=1 || a.noteOn(1,67,80)!=2) return false;
    a.noteOff(1,64); if(!a.slot(1).held) return false;
    a.noteOff(0,64);
    if(a.noteOn(0,72,127)!=1) return false; // released preferred over oldest held
    if(a.noteOn(0,74,127)!=0) return false; // oldest held stolen
    a.noteOff(0,60); if(!a.slot(0).held) return false; // stale note-off
    if(a.noteOn(0,74,80)!=0 || a.slot(0).velocity!=80) return false;
    a.allOff(0,true);
    if(a.slot(0).active || a.slot(1).active || !a.slot(2).active) return false;
    a.noteOff(1,67); a.retire(2);
    return !a.slot(2).active;
}
constexpr bool monoChecks(eVoiceMode mode) {
    VoiceAllocator<3> a(mode);
    a.noteOn(0,60,100);
    if(!a.slot(0).retrigger || a.slot(0).glide!=(mode==VOICE_MODE_MONO_FULL)) return false;
    a.noteOn(1,64,77);
    if(a.slot(0).retrigger || !a.slot(0).glide) return false;
    a.noteOff(1,64);
    if(a.slot(0).note!=60 || a.slot(0).velocity!=100 || !a.slot(0).held) return false;
    const auto generation=a.slot(0).generation;
    a.noteOff(1,64); if(a.slot(0).generation!=generation) return false;
    a.noteOff(0,60); if(a.slot(0).held) return false;
    a.noteOn(0,67,127); if(!a.slot(0).retrigger) return false;
    a.setMode(VOICE_MODE_POLY);
    return !a.slot(0).active && a.mode()==VOICE_MODE_POLY;
}
constexpr bool stackChecks() {
    VoiceAllocator<1,2> a(VOICE_MODE_MONO_LEGATO);
    a.noteOn(0,60,100); a.noteOn(0,62,90); a.noteOn(0,64,80);
    a.noteOff(0,64); if(a.slot(0).note!=62) return false;
    a.noteOff(0,62); if(a.slot(0).held) return false; // 60 evicted from bounded stack
    a.noteOn(0,69,80); a.noteOn(0,69,100); a.noteOff(0,69);
    return !a.slot(0).held;
}
constexpr bool channelAndResetChecks() {
    VoiceAllocator<2> a(VOICE_MODE_MONO_FULL);
    a.noteOn(0,60,99); a.noteOn(1,64,80);
    a.allOff(1,true);
    if(!a.slot(0).held || a.slot(0).note!=60 || a.slot(0).velocity!=99) return false;
    a.allOff(0,false);
    if(a.slot(0).held || !a.slot(0).active) return false;
    a.retire(0); if(a.slot(0).active) return false;
    if(a.noteOn(16,60,100)!=-1 || a.noteOn(0,128,100)!=-1) return false;
    a.noteOn(0,69,100); a.noteOn(0,69,0);
    if(a.slot(0).held) return false;
    a.setMode(VOICE_MODES_COUNT);
    if(a.mode()!=VOICE_MODE_MONO_FULL) return false;
    a.silence();
    return !a.slot(0).active;
}
static_assert(polyChecks(),"Poly allocator contract failed");
static_assert(monoChecks(VOICE_MODE_MONO_FULL),"Mono Full contract failed");
static_assert(monoChecks(VOICE_MODE_MONO_LEGATO),"Mono Legato contract failed");
static_assert(stackChecks(),"Held stack/duplicate contract failed");
static_assert(channelAndResetChecks(),"Channel/panic/reset contract failed");
constexpr bool modeIterationChecks() {
    for (unsigned from=VOICE_MODE_POLY; from<VOICE_MODES_COUNT; ++from) {
        for (unsigned to=VOICE_MODE_POLY; to<VOICE_MODES_COUNT; ++to) {
            VoiceAllocator<2> allocator{eVoiceMode(from)};
            allocator.noteOn(0,60,100);
            allocator.setMode(eVoiceMode(to));
            if (allocator.mode()!=eVoiceMode(to)) return false;
            if (from!=to && allocator.slot(0).active) return false;
            if (from==to && !allocator.slot(0).active) return false;
        }
    }
    return !isValidVoiceMode(VOICE_MODES_COUNT);
}
static_assert(modeIterationChecks(),"Voice mode enumeration/transition contract failed");
}




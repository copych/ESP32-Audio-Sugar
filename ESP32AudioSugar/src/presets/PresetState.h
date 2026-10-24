#pragma once
#include <Arduino.h>
#include "../../Config.h"
#include "../parameters/ParameterRegistry.h"
#include "../parts/PartRouting.h"
#include "../fx/EffectTypes.h"
#include "../audio/PartTypes.h"
namespace SugarPresets {
enum eSlot : uint8_t { SLOT_1, SLOT_2, SLOT_3, SLOT_4, SLOT_5, SLOT_6, SLOT_7, SLOT_8, SLOTS_COUNT };
constexpr unsigned formatVersion=5;
constexpr unsigned maxFileBytes=8192;
constexpr const char* insertKey(SugarFX::eInsertType type) {
    return type==SugarFX::INSERT_BYPASS ? "bypass" : type==SugarFX::INSERT_EFFECT ? "enabled" : nullptr;
}
constexpr const char* voiceModeKey(SugarAudio::eVoiceMode mode) {
    switch(mode) {
    case SugarAudio::VOICE_MODE_POLY: return "poly";
    case SugarAudio::VOICE_MODE_MONO_FULL: return "mono_full";
    case SugarAudio::VOICE_MODE_MONO_LEGATO: return "mono_legato";
    default: return nullptr;
    }
}
struct State {
    int32_t synthValues[SugarAudio::SynthLayout::nativeWords ? SugarAudio::SynthLayout::nativeWords : 1]={};
    uint32_t active=0;
    uint32_t routes[SugarParts::MIDI_CHANNELS_COUNT]={};
    int32_t values[SUGAR_PART_CAPACITY][SugarParameters::PARAMETERS_COUNT][SugarParameters::MIDI_CHANNELS_COUNT]={};
    SugarFX::SlotState fx[SUGAR_PART_CAPACITY][SugarFX::FX_SLOTS_COUNT]={};
    int32_t busValues[SugarFX::SharedSendEffect::controlCount()]={};
};
// Compatibility scalar controls remain serialized for old API users. Slot zero
// is authoritative in v4; reject conflicting settings instead of guessing.
constexpr bool validEffects(const State& state) {
    using namespace SugarFX;
    using namespace SugarParameters;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        for(const auto& slot:state.fx[p]) if(!validSlot(slot)) return false;
        const auto& first=state.fx[p][FX_SLOT_FIRST];
        if(state.values[p][PARAM_FX_INSERT][0]!=(first.algorithm==ALGORITHM_BYPASS ? INSERT_BYPASS : INSERT_EFFECT)) return false;
        if(first.algorithm==ALGORITHM_DRIVE && first.values[DriveEffect::CONTROL_AMOUNT]!=state.values[p][PARAM_FX_DRIVE][0]) return false;
    }
    for(unsigned i=0;i<SharedSendEffect::controlCount();++i)
        if(!validControl(SharedSendEffect::control(i),state.busValues[i])) return false;
    if(busTimeControl<SharedSendEffect::controlCount() && state.busValues[busTimeControl]!=state.values[0][PARAM_DELAY_TIME_MS][0]) return false;
    if(busFeedbackControl<SharedSendEffect::controlCount() && state.busValues[busFeedbackControl]!=state.values[0][PARAM_DELAY_FEEDBACK][0]) return false;
    return true;
}
constexpr void migrateLegacyEffects(State& state) {
    using namespace SugarFX;
    using namespace SugarParameters;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        for(auto& slot:state.fx[p]) slot=defaultSlot(ALGORITHM_BYPASS);
        if(state.values[p][PARAM_FX_INSERT][0]==INSERT_EFFECT) {
            state.fx[p][FX_SLOT_FIRST]=defaultSlot(ALGORITHM_DRIVE);
            state.fx[p][FX_SLOT_FIRST].values[DriveEffect::CONTROL_AMOUNT]=state.values[p][PARAM_FX_DRIVE][0];
        }
    }
    for(unsigned i=0;i<SharedSendEffect::controlCount();++i) state.busValues[i]=SharedSendEffect::control(i)->defaultValue;
    if(busTimeControl<SharedSendEffect::controlCount()) state.busValues[busTimeControl]=state.values[0][PARAM_DELAY_TIME_MS][0];
    if(busFeedbackControl<SharedSendEffect::controlCount()) state.busValues[busFeedbackControl]=state.values[0][PARAM_DELAY_FEEDBACK][0];
}
constexpr const char* parameterKey(SugarParameters::eParameterId id) {
    using namespace SugarParameters;
    switch(id) {
    case PARAM_VOICE_MODE: return "voice_mode";
    case PARAM_PORTAMENTO_MS: return "portamento_ms";
    case PARAM_GAIN: return "gain";
    case PARAM_PULSE_WIDTH: return "pulse_width";
    case PARAM_WAVEFORM: return "waveform";
    case PARAM_CUTOFF: return "cutoff";
    case PARAM_RESONANCE: return "resonance";
    case PARAM_ENV_MOD: return "env_mod";
    case PARAM_DECAY_MS: return "decay_ms";
    case PARAM_PART_LEVEL: return "part_level";
    case PARAM_FX_INSERT: return "fx_insert";
    case PARAM_FX_DRIVE: return "fx_drive";
    case PARAM_FX_SEND: return "fx_send";
    case PARAM_DELAY_TIME_MS: return "delay_time_ms";
    case PARAM_DELAY_FEEDBACK: return "delay_feedback";
    case PARAM_DELAY_RETURN: return "delay_return";
    default: return nullptr;
    }
}
constexpr bool validMasks(const State& state) {
    constexpr auto available=SugarParts::Routing<SUGAR_PART_CAPACITY>::availableMask;
    if(state.active&~available) return false;
    for(auto mask:state.routes) if(mask&~available) return false;
    return true;
}
}

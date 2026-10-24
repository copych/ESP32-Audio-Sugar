#pragma once
#include <Arduino.h>
#include "../midi/MidiEvent.h"
#include "VoiceAllocator.h"
#include "../parameters/SynthParameter.h"
#include "../parameters/ParameterRegistry.h"
#include "../parts/PartRouting.h"
#include "../presets/PresetState.h"
namespace SugarEngine {
void prepare();
// Core1 discovery/control; all DSP updates cross the bounded event queue.
unsigned getSynthParameterCount(SugarParts::ePartId part);
const SugarSynth::Parameter* getSynthParameter(SugarParts::ePartId part,unsigned id);
bool setSynthParameter(SugarParts::ePartId part,unsigned id,int32_t value,uint8_t channel=0);
bool getSynthValue(SugarParts::ePartId part,unsigned id,int32_t& value,uint8_t channel=0,bool applied=false);
unsigned getSynthOffset(SugarParts::ePartId part,unsigned id); // Core1 native snapshot index
const char* getFxName(SugarFX::eAlgorithm type);
const SugarFX::Control* getFxControl(SugarParts::ePartId part,unsigned slot,unsigned control);
unsigned getFxControlCount(SugarParts::ePartId part,unsigned slot);
bool getFxAlgorithm(SugarParts::ePartId part,unsigned slot,SugarFX::eAlgorithm& type,bool applied=false);
bool selectFx(SugarParts::ePartId part,unsigned slot,SugarFX::eAlgorithm type);
bool setFxControl(SugarParts::ePartId part,unsigned slot,unsigned control,int32_t value);
bool getFxValue(SugarParts::ePartId part,unsigned slot,unsigned control,int32_t& value,bool applied=false);
bool setFxBusControl(unsigned control,int32_t value);
bool getFxBusValue(unsigned control,int32_t& value,bool applied=false);
bool enqueue(const SugarMidi::Event& event); // Core 1 MIDI routing entry
bool enqueueToPart(SugarParts::ePartId part,const SugarMidi::Event& event);
bool setPartActive(SugarParts::ePartId part,bool active);
bool isPartActive(SugarParts::ePartId part); // applied state
const char* getInstrumentName(SugarParts::ePartId part); // Core 1, static metadata
const char* getInstrumentKey(SugarParts::ePartId part); // stable preset identity
uint32_t getInstrumentCapabilities(SugarParts::ePartId part);
const SugarParameters::Descriptor* getParameterDescriptor(SugarParts::ePartId part,SugarParameters::eParameterId id);
const char* getParameterChoiceName(SugarParameters::eParameterId id,unsigned choice);
bool getDefaultParameter(SugarParts::ePartId part,SugarParameters::eParameterId id,int32_t& value);
bool setChannelRoute(uint8_t channel,uint32_t partMask); // Core 1; queued global silence first
bool getChannelRoute(uint8_t channel,uint32_t& partMask); // Core1 requested mask, including inactive parts
bool setBusParameter(SugarFX::eBus bus,SugarParameters::eParameterId id,int32_t value);
bool getRequestedBusParameter(SugarFX::eBus bus,SugarParameters::eParameterId id,int32_t& value);
bool getAppliedBusParameter(SugarFX::eBus bus,SugarParameters::eParameterId id,int32_t& value);
bool setParameter(SugarParts::ePartId part,SugarParameters::eParameterId id,int32_t value,uint8_t channel=0);
bool getRequestedParameter(SugarParts::ePartId part,SugarParameters::eParameterId id,int32_t& value,uint8_t channel=0);
bool getAppliedParameter(SugarParts::ePartId part,SugarParameters::eParameterId id,int32_t& value,uint8_t channel=0);
bool setVoiceMode(SugarParts::ePartId part,SugarAudio::eVoiceMode mode);
// Backward-compatible conveniences explicitly address PART_1, never a MIDI route.
bool setParameter(SugarParameters::eParameterId id,int32_t value,uint8_t channel=0);
bool getRequestedParameter(SugarParameters::eParameterId id,int32_t& value,uint8_t channel=0);
bool getAppliedParameter(SugarParameters::eParameterId id,int32_t& value,uint8_t channel=0);
bool setVoiceMode(SugarAudio::eVoiceMode mode);
void render(int16_t* pcm);
void report();
uint32_t getFxReturnPeak(); // Core1: last completed 1024-block wet-return peak window
bool capturePreset(SugarPresets::State& state); // Core1 requested state, no pending commit
bool submitPreset(const SugarPresets::State& state); // validated/prepared Core1 mailbox
bool presetBusy(); // all control submissions rejected during the muted commit
void quiesceForStorage(); // Core0 transport boundary only; drain queue then stop notes
}

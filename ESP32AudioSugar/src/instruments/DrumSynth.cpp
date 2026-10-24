#include "DrumSynth.h"
#include "../audio/MixMath.h"
#include <math.h>
namespace DrumSynth {
namespace {
DRAM_ATTR int16_t sine[256];
DRAM_ATTR uint32_t increments[DRUMS_COUNT],sweeps[DRUMS_COUNT];
constexpr unsigned frequencies[DRUMS_COUNT]={50,180,0,0,85};
constexpr unsigned sweepFrequencies[DRUMS_COUNT]={110,0,0,0,65};
}
void Instrument::prepare() {
    for(unsigned i=0;i<256;++i) sine[i]=int16_t(32767.0f*sinf(float(i)*(6.28318530718f/256.0f)));
    for(unsigned i=0;i<DRUMS_COUNT;++i) {
        increments[i]=uint32_t(uint64_t(frequencies[i])*(uint64_t(1)<<32)/SUGAR_SAMPLE_RATE);
        sweeps[i]=uint32_t(uint64_t(sweepFrequencies[i])*(uint64_t(1)<<32)/SUGAR_SAMPLE_RATE);
    }
    for(auto& gain:gain_) gain=SugarParameters::MAX_GAIN;
    applyParameter({SugarMidi::EVENT_PARAMETER,0,SugarParameters::PARAM_DECAY_MS,0,250,
        prepareParameter(SugarParameters::PARAM_DECAY_MS,250,SUGAR_SAMPLE_RATE)});
    silence();
}
void IRAM_ATTR Instrument::silence() {
    for(auto& voice:voices_) voice=Voice{};
    previousNoise_=0;
}
bool IRAM_ATTR Instrument::applyParameter(const SugarMidi::Event& e) {
    using namespace SugarParameters;
    if(e.channel>=MIDI_CHANNELS_COUNT) return false;
    if(e.data1==PARAM_GAIN && e.value>=0 && e.value<=MAX_GAIN) { gain_[e.channel]=uint8_t(e.value); return true; }
    if(e.data1!=PARAM_DECAY_MS || e.channel || e.value<30 || e.value>1000) return false;
    steps_[DRUM_KICK]=e.prepared;
    steps_[DRUM_SNARE]=e.prepared*3U/2U;
    steps_[DRUM_CLOSED_HAT]=e.prepared*8U;
    steps_[DRUM_OPEN_HAT]=e.prepared/2U+1U;
    steps_[DRUM_TOM]=e.prepared;
    return true;
}
void IRAM_ATTR Instrument::onEvent(const SugarMidi::Event& e) {
    if(e.channel>=SugarParameters::MIDI_CHANNELS_COUNT || e.data1>127 || e.data2>127) return;
    if(e.type==SugarMidi::EVENT_NOTE_ON && e.data2) {
        const auto drum=drumForNote(e.data1);
        if(drum==DRUMS_COUNT) return;
        if(drum==DRUM_CLOSED_HAT) voices_[DRUM_OPEN_HAT].envelope=0;
        if(drum==DRUM_OPEN_HAT) voices_[DRUM_CLOSED_HAT].envelope=0;
        voices_[drum]={0,SugarSynthDsp::envelopePeak,e.channel,e.data2};
    } else if(e.type==SugarMidi::EVENT_ALL_OFF) {
        for(auto& voice:voices_) if(voice.channel==e.channel) {
            // One-shot notes ignore key release; explicit CC120/123 stops them.
            voice.envelope=0;
        }
    }
}
void IRAM_ATTR Instrument::render(SugarAudio::AudioBlock output) {
    int32_t levels[DRUMS_COUNT];
    for(unsigned i=0;i<DRUMS_COUNT;++i)
        levels[i]=int32_t(voices_[i].velocity)*gain_[voices_[i].channel]*SugarMix::voicePeak/16129;
    for(unsigned frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
        random_=SugarSynthDsp::noise(random_);
        const int32_t noise=int32_t(random_>>16)-32768;
        const int32_t high=(noise-previousNoise_)/2;
        previousNoise_=noise;
        int32_t mix=0;
        for(unsigned i=0;i<DRUMS_COUNT;++i) {
            auto& voice=voices_[i];
            if(!voice.envelope) continue;
            const uint32_t envelope=voice.envelope>>9; // Q15, at most 32768.
            voice.phase+=increments[i]+uint32_t((uint64_t(sweeps[i])*envelope)>>15);
            const int32_t tone=sine[voice.phase>>24];
            const int32_t wave=i==DRUM_SNARE ? (tone+noise*3)/4 :
                (i==DRUM_CLOSED_HAT || i==DRUM_OPEN_HAT) ? high : tone;
            const int32_t shaped=(wave*int32_t(envelope))>>15;
            mix+=(shaped*levels[i])>>15;
            voice.envelope=SugarSynthDsp::decay(voice.envelope,steps_[i]);
        }
        // Protect the voice bus before its PCM16 boundary as well as the final part mix.
        const int16_t sample=SugarMix::softClip(SugarMix::normalize<DRUMS_COUNT>(mix));
        output.interleaved[frame*2]=sample; output.interleaved[frame*2+1]=sample;
    }
}
}

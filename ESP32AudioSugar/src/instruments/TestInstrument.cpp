#include "TestInstrument.h"
#include "../audio/MixMath.h"
#include <math.h>
namespace TestInstrument {
static_assert(SUGAR_VOICE_MODE >= SugarAudio::VOICE_MODE_POLY && SUGAR_VOICE_MODE < SugarAudio::VOICE_MODES_COUNT, "Invalid voice mode");
static_assert(SUGAR_PORTAMENTO_MS >= 0 && SUGAR_PORTAMENTO_MS <= SugarParameters::MAX_PORTAMENTO_MS, "Invalid portamento time");

#if SUGAR_TRANSPORT_TONE
static constexpr uint32_t period=2205;
static DRAM_ATTR int16_t wave[period*2];
#else
static DRAM_ATTR int16_t wave[256];
static DRAM_ATTR uint32_t increments[128];
static DRAM_ATTR int32_t mix[SUGAR_BLOCK_FRAMES];
#endif
void Instrument::prepare() {
#if SUGAR_TRANSPORT_TONE
    for(uint32_t i=0;i<period;++i) {
        const float phase=float(i)*(6.28318530718f/float(period));
        wave[i*2]=int16_t(4125.0f*sinf(22.0f*phase));
        wave[i*2+1]=int16_t(4125.0f*sinf(33.0f*phase));
    }
#else
    for(uint32_t i=0;i<256;++i) wave[i]=int16_t(32767.0f*sinf(float(i)*(6.28318530718f/256.0f)));
    for(unsigned note=0;note<128;++note) {
        const float hz=440.0f*powf(2.0f,(float(note)-69.0f)*(1.0f/12.0f));
        increments[note]=uint32_t(hz*(4294967296.0f/float(SUGAR_SAMPLE_RATE)));
    }
#endif
    for(auto& g:gain_) g=127;
    silence();
    tonePosition_=0;
    glideFrames_=0; glideReciprocal_=0;
}
void IRAM_ATTR Instrument::silence() {
    allocator_.silence();
    for(auto& v:voices_) v=DspVoice{};
}
bool IRAM_ATTR Instrument::applyParameter(const SugarMidi::Event& e) {
    using namespace SugarParameters;
    if(e.channel>=MIDI_CHANNELS_COUNT) return false;
    switch(eParameterId(e.data1)) {
    case PARAM_VOICE_MODE:
        if(e.channel || e.value<SugarAudio::VOICE_MODE_POLY || e.value>=SugarAudio::VOICE_MODES_COUNT) return false;
        if(allocator_.mode()!=SugarAudio::eVoiceMode(e.value)) {
            silence(); allocator_.setMode(SugarAudio::eVoiceMode(e.value));
        }
        return true;
    case PARAM_PORTAMENTO_MS:
        if(e.channel || e.value<0 || e.value>MAX_PORTAMENTO_MS) return false;
        glideFrames_=uint32_t(e.value)*SUGAR_SAMPLE_RATE/1000U;
        glideReciprocal_=e.prepared;
        return true;
    case PARAM_GAIN:
        if(e.value<0 || e.value>MAX_GAIN) return false;
        gain_[e.channel]=uint8_t(e.value);
        return true;
    default: return false;
    }
    return false;
}
void IRAM_ATTR Instrument::onEvent(const SugarMidi::Event& e) {
    using namespace SugarMidi;
    if(e.channel>15 || e.data1>127 || e.data2>127) return;
    switch(e.type) {
    case EVENT_PARAMETER: case EVENT_GAIN: case EVENT_VOICE_MODE: case EVENT_PART_ACTIVE: case EVENT_ROUTING_RESET:
    case EVENT_FX_SELECT: case EVENT_FX_CONTROL: case EVENT_FX_BUS_CONTROL: case EVENT_SYNTH_PARAMETER: case EVENT_CONTROL_CHANGE: case EVENTS_COUNT: break;
    case EVENT_NOTE_ON: allocator_.noteOn(e.channel,e.data1,e.data2); break;
    case EVENT_NOTE_OFF: allocator_.noteOff(e.channel,e.data1); break;
    case EVENT_ALL_OFF:
        if(e.data1==120) for(unsigned i=0;i<SUGAR_VOICES;++i)
            if(allocator_.slot(i).channel==e.channel) voices_[i].level=0;
        allocator_.allOff(e.channel,e.data1==120);
        break;

    }
}
void IRAM_ATTR Instrument::render(SugarAudio::AudioBlock output) {
#if SUGAR_TRANSPORT_TONE
    uint32_t pos=tonePosition_;
    for(uint32_t i=0;i<SUGAR_BLOCK_FRAMES;++i) {
        output.interleaved[i*2]=wave[pos*2]; output.interleaved[i*2+1]=wave[pos*2+1];
        if(++pos==period) pos=0;
    }
    tonePosition_=pos;
#else
    for(auto& sample:mix) sample=0;
    const bool poly=allocator_.mode()==SugarAudio::VOICE_MODE_POLY;
    const unsigned count=poly ? SUGAR_VOICES : 1;
    for(unsigned index=0;index<count;++index) {
        const auto& slot=allocator_.slot(index);
        auto& v=voices_[index];
        if(!slot.active) { v.level=0; continue; }
        if(slot.generation!=v.generation) {
            const uint32_t target= increments[slot.note];
            if(slot.retrigger) { v.phase=0; v.level=0; }
            v.targetIncrement=target;
            if(glideFrames_>1 && slot.glide && v.increment) {
                v.glideRemaining=glideFrames_;
                v.glideStep=int32_t((int64_t(int32_t(target)-int32_t(v.increment))*glideReciprocal_)>>32);
            } else { v.increment=target; v.glideRemaining=0; }
            v.generation=slot.generation;
        }
        const int32_t target=slot.held ? (int32_t(slot.velocity)*gain_[slot.channel]*4125+8064)/16129 : 0;
        for(uint32_t frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
            v.level+=int32_t(v.level<target)-int32_t(v.level>target);
            if(v.glideRemaining) {
                v.increment=uint32_t(int32_t(v.increment)+v.glideStep);
                if(--v.glideRemaining==0) v.increment=v.targetIncrement;
            }
            mix[frame]+=(int32_t(wave[v.phase>>24])*v.level)>>15;
            v.phase+=v.increment;
        }
        if(!slot.held && !v.level) allocator_.retire(index);
    }
    // Fixed equal-power scaling avoids gain pumping; shaping protects correlated peaks.
    for(uint32_t frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
        const int16_t sample=SugarMix::softClip(poly ? SugarMix::normalize<SUGAR_VOICES>(mix[frame]) : mix[frame]);
        output.interleaved[frame*2]=sample; output.interleaved[frame*2+1]=sample;
    }
#endif
}
}









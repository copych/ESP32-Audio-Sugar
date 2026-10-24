#include "AcidBass.h"
#include "SynthDsp.h"
#include "../audio/MixMath.h"
#include <math.h>
namespace AcidBass {
namespace {
DRAM_ATTR uint32_t increments[128];
DRAM_ATTR int32_t coefficients[128];
}
uint32_t Instrument::prepareParameter(SugarParameters::eParameterId id,int32_t value,uint32_t rate) {
    if(id==SugarParameters::PARAM_PORTAMENTO_MS) return SugarParameters::glideReciprocal(value,rate);
    if(id==SugarParameters::PARAM_DECAY_MS) return SugarSynthDsp::decayStep(value,rate);
    return 0;
}
void Instrument::prepare() {
    for(unsigned i=0;i<128;++i) {
        const float hz=440.0f*powf(2.0f,(float(i)-69.0f)*(1.0f/12.0f));
        increments[i]=uint32_t(hz*(4294967296.0f/float(SUGAR_SAMPLE_RATE)));
        const float cutoff=60.0f*powf(100.0f,float(i)*(1.0f/127.0f));
        const float coefficient=1.0f-expf(-6.28318530718f*cutoff/float(SUGAR_SAMPLE_RATE));
        coefficients[i]=int32_t((coefficient>0.5f ? 0.5f : coefficient)*32768.0f);
    }
    for(auto& gain:gain_) gain=SugarParameters::MAX_GAIN;
    decay_=SugarSynthDsp::decayStep(250,SUGAR_SAMPLE_RATE);
    silence();
}
void IRAM_ATTR Instrument::silence() {
    allocator_.silence(); generation_=allocator_.slot(0).generation;
    phase_=increment_=target_=remaining_=envelope_=0; level_=0;
    for(auto& pole:poles_) pole=0;
}
bool IRAM_ATTR Instrument::applyParameter(const SugarMidi::Event& e) {
    using namespace SugarParameters;
    if(e.channel>=MIDI_CHANNELS_COUNT) return false;
    if(e.data1==PARAM_GAIN) {
        if(e.value<0 || e.value>MAX_GAIN) return false;
        gain_[e.channel]=uint8_t(e.value); return true;
    }
    if(e.channel) return false;
    switch(eParameterId(e.data1)) {
    case PARAM_VOICE_MODE:
        if(e.value<SugarAudio::VOICE_MODE_MONO_FULL || e.value>SugarAudio::VOICE_MODE_MONO_LEGATO) return false;
        if(allocator_.mode()!=SugarAudio::eVoiceMode(e.value)) { silence(); allocator_.setMode(SugarAudio::eVoiceMode(e.value)); }
        return true;
    case PARAM_PORTAMENTO_MS:
        if(e.value<0 || e.value>MAX_PORTAMENTO_MS) return false;
        glideFrames_=portamentoFrames(e.value,SUGAR_SAMPLE_RATE); reciprocal_=e.prepared; return true;
    case PARAM_WAVEFORM:
        if(e.value<WAVEFORM_SAW || e.value>=WAVEFORMS_COUNT) return false;
        waveform_=eWaveform(e.value); return true;
    case PARAM_CUTOFF: case PARAM_RESONANCE: case PARAM_ENV_MOD:
        if(e.value<0 || e.value>127) return false;
        if(e.data1==PARAM_CUTOFF) cutoff_=e.value;
        else if(e.data1==PARAM_RESONANCE) resonance_=e.value;
        else envMod_=e.value;
        return true;
    case PARAM_DECAY_MS:
        if(e.value<30 || e.value>1000) return false;
        decay_=e.prepared; return true;
    default: return false;
    }
}
void IRAM_ATTR Instrument::onEvent(const SugarMidi::Event& e) {
    if(e.channel>=SugarParameters::MIDI_CHANNELS_COUNT || e.data1>127 || e.data2>127) return;
    switch(e.type) {
    case SugarMidi::EVENT_NOTE_ON: allocator_.noteOn(e.channel,e.data1,e.data2); break;
    case SugarMidi::EVENT_NOTE_OFF: allocator_.noteOff(e.channel,e.data1); break;
    case SugarMidi::EVENT_ALL_OFF:
        if(e.data1==120 && allocator_.slot(0).channel==e.channel) {
            level_=0; envelope_=0; for(auto& pole:poles_) pole=0;
        }
        allocator_.allOff(e.channel,e.data1==120); break;
    default: break;
    }
}
void IRAM_ATTR Instrument::render(SugarAudio::AudioBlock output) {
    using namespace SugarSynthDsp;
    const auto& slot=allocator_.slot(0);
    if(slot.active && generation_!=slot.generation) {
        target_=increments[slot.note];
        if(slot.retrigger || !increment_) { phase_=0; envelope_=envelopePeak; }
        if(slot.glide && increment_ && glideFrames_>1) {
            remaining_=glideFrames_;
            glideStep_=int32_t(((int64_t(target_)-increment_)*reciprocal_)>>32);
        } else { increment_=target_; remaining_=0; }
        generation_=slot.generation;
    }
    const bool held=slot.active && slot.held;
    const int32_t targetLevel=held ? (int32_t(slot.velocity)*gain_[slot.channel]*SugarMix::voicePeak)/16129 : 0;
    const int32_t accent=slot.velocity>=100 ? 12 : 0;
    const int32_t feedback=resonance_*384; // Q14, maximum just below 3.
    for(unsigned frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
        if(level_<targetLevel) { level_+=64; if(level_>targetLevel) level_=targetLevel; }
        else if(level_>targetLevel) { level_-=16; if(level_<targetLevel) level_=targetLevel; }
        if(remaining_) {
            increment_=uint32_t(int64_t(increment_)+glideStep_);
            if(!--remaining_) increment_=target_;
        }
        phase_+=increment_;
        const int32_t oscillator=waveform_==SugarParameters::WAVEFORM_SAW ? int32_t(phase_>>17)-16384 : (phase_&0x80000000U ? 16383 : -16384);
        const int32_t modulation=int32_t((envelope_>>16)*uint32_t(envMod_+accent)>>8);
        const unsigned index=unsigned(cutoff_+modulation)>127 ? 127U : unsigned(cutoff_+modulation);
        const int32_t filtered=filter(oscillator,coefficients[index],feedback,poles_);
        const int16_t sample=int16_t((filtered*level_)>>14);
        output.interleaved[frame*2]=sample; output.interleaved[frame*2+1]=sample;
        envelope_=decay(envelope_,decay_);
    }
    if(!held && !level_) { allocator_.retire(0); for(auto& pole:poles_) pole=0; }
}
}

#include "PulseInstrument.h"
#include <math.h>
namespace PulseInstrument {
static DRAM_ATTR uint32_t increments[128];
void Instrument::prepare() {
    for(unsigned note=0;note<128;++note) {
        const float hz=440.0f*powf(2.0f,(float(note)-69.0f)*(1.0f/12.0f));
        increments[note]=uint32_t(hz*(4294967296.0f/float(SUGAR_SAMPLE_RATE)));
    }
    for(auto& gain:gain_) gain=SugarParameters::MAX_GAIN;
    silence();
}
void IRAM_ATTR Instrument::silence() { active_=false; level_=0; phase_=0; }
bool IRAM_ATTR Instrument::applyParameter(const SugarMidi::Event& e) {
    using namespace SugarParameters;
    if(e.channel>=MIDI_CHANNELS_COUNT) return false;
    if(e.data1==PARAM_GAIN && e.value>=0 && e.value<=MAX_GAIN) {
        gain_[e.channel]=uint8_t(e.value); return true;
    }
    if(e.data1==PARAM_PULSE_WIDTH && !e.channel && e.value>=minWidth && e.value<=maxWidth) {
        threshold_=e.prepared;
        // Balance the pulse's ideal DC component. At 50%, both scales are unity.
        positiveScale_=(100-e.value)*65536/100;
        negativeScale_=e.value*65536/100;
        return true;
    }
    return false;
}
void IRAM_ATTR Instrument::onEvent(const SugarMidi::Event& e) {
    using namespace SugarMidi;
    if(e.channel>=SugarParameters::MIDI_CHANNELS_COUNT || e.data1>127 || e.data2>127) return;
    switch(e.type) {
    case EVENT_NOTE_ON:
        if(e.data2) {
            channel_=e.channel; note_=e.data1; velocity_=e.data2;
            increment_=increments[note_]; phase_=0; level_=0; active_=true;
        } else if(e.channel==channel_ && e.data1==note_) active_=false;
        break;
    case EVENT_NOTE_OFF:
        if(e.channel==channel_ && e.data1==note_) active_=false;
        break;
    case EVENT_ALL_OFF:
        if(e.channel==channel_) { active_=false; if(e.data1==120) level_=0; }
        break;
    case EVENT_GAIN: case EVENT_VOICE_MODE: case EVENT_PARAMETER:
    case EVENT_PART_ACTIVE: case EVENT_ROUTING_RESET:
    case EVENT_FX_SELECT: case EVENT_FX_CONTROL: case EVENT_FX_BUS_CONTROL: case EVENT_SYNTH_PARAMETER: case EVENT_CONTROL_CHANGE: case EVENTS_COUNT: break;
    }
}
void IRAM_ATTR Instrument::render(SugarAudio::AudioBlock output) {
    const int32_t target=active_ ? (int32_t(velocity_)*gain_[channel_]*4125+8064)/16129 : 0;
    uint32_t phase=phase_;
    int32_t level=level_;
    for(unsigned frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
        level+=int32_t(level<target)-int32_t(level>target);
        const int32_t scale=phase<threshold_ ? positiveScale_ : -negativeScale_;
        const int16_t sample=int16_t((level*scale)>>15);
        output.interleaved[frame*2]=sample; output.interleaved[frame*2+1]=sample;
        phase+=increment_;
    }
    phase_=phase; level_=level;
}
}

#include "../instruments/AcidBass.h"
#include "../instruments/DrumSynth.h"
#include "../audio/InstrumentProfile.h"
#include "../control/ControlLogic.h"
namespace {
constexpr bool synthProfiles() {
    using namespace SugarParameters;
    return SugarAudio::validProfile<AcidBass::Instrument>() && SugarAudio::validProfile<DrumSynth::Instrument>() &&
        !validDescriptor(AcidBass::Instrument::parameterDescriptor(PARAM_VOICE_MODE),PARAM_VOICE_MODE,SugarAudio::VOICE_MODE_POLY) &&
        !DrumSynth::Instrument::parameterDescriptor(PARAM_VOICE_MODE) &&
        !choiceName(PARAM_WAVEFORM,WAVEFORMS_COUNT);
}
constexpr bool drumMapping() {
    using namespace DrumSynth;
    bool found[DRUMS_COUNT]={};
    for(unsigned note=0;note<128;++note) {
        const auto drum=drumForNote(note);
        if(drum<DRUMS_COUNT) found[drum]=true;
    }
    for(bool mapped:found) if(!mapped) return false;
    return drumForNote(36)==DRUM_KICK && drumForNote(38)==DRUM_SNARE &&
        drumForNote(42)==DRUM_CLOSED_HAT && drumForNote(46)==DRUM_OPEN_HAT &&
        drumForNote(45)==DRUM_TOM && drumForNote(69)==DRUM_CLOSED_HAT &&
        drumForNote(60)==DRUMS_COUNT && drumForNote(128)==DRUMS_COUNT;
}
constexpr bool filterBounds() {
    using namespace SugarSynthDsp;
    constexpr int32_t coefficients[]={1,8192,16384};
    for(int32_t coefficient:coefficients) {
        int32_t poles[4]={};
        for(unsigned i=0;i<2048;++i) {
            const int32_t out=filter(i&1 ? 16384 : -16384,coefficient,127*384,poles);
            if(out< -16384 || out>16384) return false;
            for(auto pole:poles) if(pole< -16384 || pole>16384) return false;
        }
    }
    return true;
}
constexpr bool envelopeAndWindow() {
    using namespace SugarSynthDsp;
    const auto step=decayStep(1000,44100);
    if(!step || decay(0,step) || decay(step,step) || decay(envelopePeak,step)>=envelopePeak) return false;
    for(unsigned visible=3;visible<=4;++visible)
      for(unsigned field=0;field<unsigned(SugarControl::FIELDS_FIXED_COUNT)+unsigned(SugarParameters::PARAMETERS_COUNT);++field) {
        const auto first=SugarControl::parameterWindow(field,visible);
        if(field>=SugarControl::FIELD_PARAMETER_FIRST) {
            const auto parameter=field-SugarControl::FIELD_PARAMETER_FIRST;
            if(parameter<first || parameter>=first+visible) return false;
        }
    }
    return noise(0x739AC51U)!=0;
}
static_assert(synthProfiles(),"Synth profile contract");
static_assert(drumMapping(),"Drum note coverage and unmapped rejection");
static_assert(filterBounds(),"Resonant filter state bounds");
static_assert(envelopeAndWindow(),"Envelope termination and selected control visibility");
}

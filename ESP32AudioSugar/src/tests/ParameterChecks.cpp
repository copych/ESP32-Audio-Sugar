#include "../parameters/ParameterRequests.h"
#include "../midi/EventQueue.h"
namespace {
using namespace SugarParameters;
struct ParameterTestWord {
    uint32_t value=0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t v) { value=v; }
};
constexpr bool metadataChecks() {
    for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
        const auto id=eParameterId(raw);
        const auto* d=descriptor(id);
        if(!d || d->id!=id || !d->name[0] || d->scope>=PARAM_SCOPES_COUNT || d->kind>=PARAM_KINDS_COUNT) return false;
        if(!valid(id,d->minimum) || !valid(id,d->maximum) || !valid(id,d->defaultValue)) return false;
        if(valid(id,d->minimum-1) || valid(id,d->maximum+1)) return false;
        if(valid(id,d->defaultValue,MIDI_CHANNELS_COUNT)) return false;
    }
    for(unsigned mode=SugarAudio::VOICE_MODE_POLY;mode<SugarAudio::VOICE_MODES_COUNT;++mode)
        if(!choiceName(PARAM_VOICE_MODE,mode)) return false;
    return !descriptor(PARAMETERS_COUNT) && !choiceName(PARAM_VOICE_MODE,SugarAudio::VOICE_MODES_COUNT);
}
constexpr bool stateChecks() {
    State state;
    int32_t value=-1;
    if(!state.set(PARAM_GAIN,40,3) || !state.get(PARAM_GAIN,value,3) || value!=40) return false;
    if(!state.get(PARAM_GAIN,value,4) || value!=MAX_GAIN) return false;
    if(state.set(PARAM_GAIN,MAX_GAIN+1,3)) return false;
    if(!state.get(PARAM_GAIN,value,3) || value!=40) return false;
    if(state.set(PARAM_VOICE_MODE,SugarAudio::VOICE_MODE_MONO_FULL,1)) return false;
    if(state.get(PARAM_PORTAMENTO_MS,value,1) || state.get(PARAMETERS_COUNT,value)) return false;
    state.reset();
    return state.get(PARAM_GAIN,value,3) && value==MAX_GAIN;
}
constexpr bool requestChecks() {
    State state;
    SugarMidi::EventQueue<2,ParameterTestWord> queue;
    int32_t value=0;
    if(!request(state,queue,PARAM_PORTAMENTO_MS,10000,0,44100)) return false;
    SugarMidi::Event e{};
    if(!queue.pop(e) || e.type!=SugarMidi::EVENT_PARAMETER || e.data1!=PARAM_PORTAMENTO_MS || e.value!=10000) return false;
    if(e.prepared!=glideReciprocal(10000,44100)) return false;
    if(!request(state,queue,PARAM_GAIN,10,2,44100) || !request(state,queue,PARAM_GAIN,20,2,44100)) return false;
    if(request(state,queue,PARAM_GAIN,30,2,44100)) return false;
    if(!state.get(PARAM_GAIN,value,2) || value!=20) return false;
    if(request(state,queue,PARAM_VOICE_MODE,SugarAudio::VOICE_MODES_COUNT,0,44100)) return false;
    return !request(state,queue,PARAM_GAIN,1,0,0);
}
constexpr bool glideChecks() {
    if(portamentoFrames(0,44100) || glideReciprocal(0,44100)) return false;
    if(portamentoFrames(100,44100)!=4410 || portamentoFrames(10000,44100)!=441000) return false;
    const uint32_t frames=portamentoFrames(100,44100);
    const uint64_t reciprocal=glideReciprocal(100,44100);
    // Reciprocal is rounded down; multiplication must not exceed the ideal step.
    const int32_t delta=100000000;
    const int32_t step=int32_t((int64_t(delta)*reciprocal)>>32);
    return step<=delta/int32_t(frames) && step>=delta/int32_t(frames)-1;
}
static_assert(metadataChecks(),"Parameter descriptors/ranges/choice count failed");
static_assert(stateChecks(),"Parameter scope/value isolation failed");
static_assert(requestChecks(),"Parameter queue/full-width/failed-request contract failed");
static_assert(glideChecks(),"Control-side glide preparation failed");
}


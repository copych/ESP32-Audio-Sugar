#include "../instruments/ParameterExample.h"
#include "../audio/InstrumentProfile.h"
#include "../parameters/ParameterRequests.h"
#include "../midi/EventQueue.h"
namespace {
using Example=ParameterExample::Instrument;
static_assert(SugarAudio::validProfile<Example>(),"A custom engine needs no central parameter IDs or legacy profile");
static_assert(SugarSynth::nativeWords<Example>()==1+SugarParameters::MIDI_CHANNELS_COUNT,"Native storage follows scope");
static_assert(SugarSynth::nativeOffset<Example>(Example::PARAM_SHAPE)==0 && SugarSynth::nativeOffset<Example>(Example::PARAM_TRIM)==1,"Native packed offsets");
static_assert(SugarSynth::valid(Example::parameter(Example::PARAM_SHAPE),10) &&
    !SugarSynth::valid(Example::parameter(Example::PARAM_SHAPE),9) &&
    !SugarSynth::valid(Example::parameter(Example::PARAM_SHAPE),50,1) &&
    SugarSynth::valid(Example::parameter(Example::PARAM_TRIM),127,15),"Range and channel validation");
struct Duplicate : Example {
    static constexpr SugarSynth::Parameter duplicate[PARAMETERS_COUNT]={
        {"same","First","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1},
        {"same","Second","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1}};
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &duplicate[id] : nullptr; }
};
static_assert(!SugarSynth::validParameters<Duplicate>(),"Duplicate local keys must fail validation");
struct BadDefault : Example {
    static constexpr SugarSynth::Parameter bad[PARAMETERS_COUNT]={
        {"first","First","",SugarParameters::PARAM_SCOPE_PART,0,1,2,1},
        {"second","Second","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1}};
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &bad[id] : nullptr; }
};
static_assert(!SugarSynth::validParameters<BadDefault>(),"Invalid defaults must fail validation");
struct DuplicateCC : Example {
    static constexpr SugarSynth::Parameter bad[PARAMETERS_COUNT]={
        {"first","First","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1,nullptr,0,12},
        {"second","Second","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1,nullptr,0,12}};
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &bad[id] : nullptr; }
};
static_assert(!SugarSynth::validParameters<DuplicateCC>(),"Ambiguous CC mappings must fail validation");
struct ReservedKey : Example {
    static constexpr SugarSynth::Parameter bad[PARAMETERS_COUNT]={
        {"part_level","First","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1},
        {"second","Second","",SugarParameters::PARAM_SCOPE_PART,0,1,0,1}};
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &bad[id] : nullptr; }
};
static_assert(!SugarSynth::validParameters<ReservedKey>(),"Native keys cannot overwrite framework parameters");

struct ManyParameters : Example {
    enum eParameter : uint8_t { PARAM_0, PARAM_1, PARAM_2, PARAM_3, PARAM_4, PARAM_5, PARAM_6, PARAM_7, PARAM_8, PARAM_9, PARAM_10, PARAM_11, PARAM_12, PARAM_13, PARAM_14, PARAM_15, PARAM_16, PARAM_17, PARAM_18, PARAM_19, PARAMETERS_COUNT };
    static constexpr unsigned parameterCount() { return PARAMETERS_COUNT; }
    static constexpr SugarSynth::Parameter many[PARAMETERS_COUNT]={
        {"custom_0","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_1","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_2","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_3","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_4","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_5","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_6","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_7","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_8","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_9","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_10","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_11","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_12","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_13","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_14","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_15","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_16","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_17","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_18","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1},
        {"custom_19","Custom","",SugarParameters::PARAM_SCOPE_PART,0,127,64,1}};
    static constexpr const SugarSynth::Parameter* parameter(unsigned id) { return id<PARAMETERS_COUNT ? &many[id] : nullptr; }
};
static_assert(ManyParameters::PARAMETERS_COUNT>unsigned(SugarParameters::PARAMETERS_COUNT) &&
    SugarSynth::validParameters<ManyParameters>() && SugarSynth::nativeWords<ManyParameters>()==ManyParameters::PARAMETERS_COUNT,
    "Custom parameter count is independent of the legacy registry");

struct Word {
    uint32_t value=0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t next) { value=next; }
};
constexpr bool compatibilitySubmission() {
    SugarParameters::State state;
    SugarMidi::EventQueue<1,Word> queue;
    SugarMidi::Event local{SugarMidi::EVENT_SYNTH_PARAMETER,3,Example::PARAM_TRIM,SugarParameters::PARAM_GAIN,77,0};
    if(!SugarParameters::submitPrepared(state,queue,SugarParameters::descriptor(SugarParameters::PARAM_GAIN),local)) return false;
    int32_t value=0;
    if(!state.get(SugarParameters::PARAM_GAIN,value,3) || value!=77) return false;
    local.value=55;
    if(SugarParameters::submitPrepared(state,queue,SugarParameters::descriptor(SugarParameters::PARAM_GAIN),local)) return false;
    if(!state.get(SugarParameters::PARAM_GAIN,value,3) || value!=77) return false;
    SugarMidi::Event received{};
    return queue.pop(received) && received.type==SugarMidi::EVENT_SYNTH_PARAMETER &&
        received.data1==Example::PARAM_TRIM && received.data2==SugarParameters::PARAM_GAIN && received.value==77;
}
static_assert(compatibilitySubmission(),"Local IDs must publish through the compatibility mirror; queue failure leaves it unchanged");

}

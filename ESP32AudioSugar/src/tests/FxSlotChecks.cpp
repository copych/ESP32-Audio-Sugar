#include "../fx/FxRack.h"
#include "../midi/EventQueue.h"
namespace {
using namespace SugarFX;
constexpr bool serialChain() {
    Rack<1,4,44100> rack;
    rack.prepare();
    int32_t mix[8]={}; const int16_t input[8]={1000,-1000,1000,-1000,1000,-1000,1000,-1000};
    rack.beginBlock(mix); rack.addPart(0,input,mix);
    for(unsigned i=0;i<8;++i) if(mix[i]!=input[i]) return false;
    if(!rack.select(0,0,ALGORITHM_DRIVE) || !rack.control(0,0,ALGORITHM_DRIVE,DriveEffect::CONTROL_AMOUNT,8*unity)) return false;
    for(unsigned slot=1;slot<FX_SLOTS_COUNT;++slot) {
        if(!rack.control(0,slot,ALGORITHM_CRUSHER,CrusherEffect::CONTROL_BITS,8) || !rack.select(0,slot,ALGORITHM_CRUSHER)) return false;
    }
    for(unsigned block=0;block<rampFrames/4;++block) { rack.beginBlock(mix); rack.addPart(0,input,mix); }
    const int32_t driven=FX_SLOTS_COUNT==1 ? 8000 : 7936;
    if(mix[6]!=driven || mix[7]!=-driven) return false;
    // Select another algorithm through the complete fade-out/commit/fade-in.
    rack.control(0,0,ALGORITHM_CRUSHER,CrusherEffect::CONTROL_BITS,0);
    if(!rack.select(0,0,ALGORITHM_CRUSHER)) return false;
    for(unsigned block=0;block<rampFrames/2+1;++block) { rack.beginBlock(mix); rack.addPart(0,input,mix); }
    const int32_t crushed=FX_SLOTS_COUNT==1 ? 1000 : 768;
    if(mix[6]!=crushed || mix[7]!=-crushed) return false;
    for(unsigned slot=0;slot<FX_SLOTS_COUNT;++slot) rack.select(0,slot,ALGORITHM_BYPASS);
    for(unsigned block=0;block<rampFrames/4+1;++block) { rack.beginBlock(mix); rack.addPart(0,input,mix); }
    for(unsigned i=0;i<8;++i) if(mix[i]!=input[i]) return false;
    return !rack.select(0,FX_SLOTS_COUNT,ALGORITHM_DRIVE) && !rack.select(1,0,ALGORITHM_DRIVE) &&
        !rack.select(0,0,ALGORITHMS_COUNT) && !rack.control(0,0,ALGORITHM_CRUSHER,CrusherEffect::CONTROLS_COUNT,0);
}
constexpr bool crusher() {
    CrusherEffect effect;
    effect.prepare(44100);
    effect.applyControl(CrusherEffect::CONTROL_BITS,8);
    effect.applyControl(CrusherEffect::CONTROL_HOLD,3);
    const auto first=effect.process({1000,-1000});
    if(first.left!=768 || first.right!=-768) return false;
    for(unsigned i=0;i<2;++i) if(effect.process({2000,-2000}).left!=768) return false;
    if(effect.process({2000,-2000}).left!=1792) return false;
    effect.reset();
    if(effect.process({}).left) return false;
    effect.applyControl(CrusherEffect::CONTROL_OUTPUT,0); effect.advance(rampFrames);
    const auto muted=effect.process({32767,-32768});
    return !muted.left && !muted.right && !effect.applyControl(CrusherEffect::CONTROL_HOLD,0);
}
struct SixControlProfile {
    enum eControl { CONTROL_A,CONTROL_B,CONTROL_C,CONTROL_D,CONTROL_E,CONTROL_F,CONTROLS_COUNT };
    static constexpr Control controls[CONTROLS_COUNT]={
        {"a","A","",0,10,0,1},{"b","B","",0,10,0,1},{"c","C","",0,10,0,1},
        {"d","D","",0,10,0,1},{"e","E","",0,10,0,1},{"f","F","",0,10,0,1}};
    static constexpr const char* key() { return "test.six.v1"; }
    static constexpr const char* name() { return "Six controls"; }
    static constexpr unsigned controlCount() { return CONTROLS_COUNT; }
    static constexpr const Control* control(unsigned i) { return i<CONTROLS_COUNT ? &controls[i] : nullptr; }
};
struct BadProfile:SixControlProfile {
    static constexpr const Control* control(unsigned i) { return i<CONTROLS_COUNT ? &controls[0] : nullptr; }
};
struct Word {
    uint32_t value=0;
    constexpr uint32_t load() const { return value; }
    constexpr void store(uint32_t next) { value=next; }
};
constexpr bool batch() {
    SugarMidi::EventQueue<4,Word> queue;
    SugarMidi::Event commands[3]={{SugarMidi::EVENT_FX_CONTROL,ALGORITHM_CRUSHER,0,0,12},
        {SugarMidi::EVENT_FX_CONTROL,ALGORITHM_CRUSHER,0,1,3},
        {SugarMidi::EVENT_FX_SELECT,ALGORITHM_CRUSHER,0,3}};
    if(!queue.pushBatch(commands,3) || queue.pushBatch(commands,2) || queue.overflows()!=1) return false;
    SugarMidi::Event event{};
    for(unsigned i=0;i<3;++i) if(!queue.pop(event) || event.type!=commands[i].type || event.value!=commands[i].value) return false;
    if(queue.pop(event)) return false; // Failed batch published nothing.
    if(!queue.pushBatch(commands,3)) return false; // Ring wrap.
    for(unsigned i=0;i<3;++i) if(!queue.pop(event) || event.type!=commands[i].type) return false;
    return !queue.pop(event);
}
static_assert(validLocalProfile<SixControlProfile>() && !validLocalProfile<BadProfile>(),"Effect-owned arbitrary counts and unique parameter keys");
static_assert(EffectPool<BypassEffect,SixControlProfile>::maxControls==SixControlProfile::CONTROLS_COUNT,"Storage size derives from registered profiles");
static_assert(serialChain(),"Fixed serial slots, exact bypass, direct concrete processing, fades and bounds");
static_assert(crusher(),"Crusher signed quantization, hold, reset and output ramp");
static_assert(batch(),"Atomic command publication, no partial batch, FIFO and wrap");
}

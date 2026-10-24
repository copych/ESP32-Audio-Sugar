#include "../fx/FxRack.h"
#include "../fx/EffectSlot.h"
namespace {
using namespace SugarFX;
using namespace SugarParameters;
// A deliberately different algorithm proves that slot integration is generic.
template<eParameterId Id> class TestEffect {
    int32_t gain=unity;
public:
    static constexpr const char* key() { return Id==PARAM_FX_DRIVE ? "test.insert.v1" : "test.send.v1"; }
    static constexpr const char* name() { return "Test gain"; }
    static constexpr bool supportsRate(uint32_t rate) { return rate>0; }
    static constexpr const Descriptor* parameterDescriptor(eParameterId id) {
        return id==Id ? descriptor(id) : nullptr;
    }
    static constexpr uint32_t prepareParameter(eParameterId,int32_t value,uint32_t) { return uint32_t(value); }
    constexpr void prepare(uint32_t) { gain=unity; }
    constexpr void reset() {}
    constexpr void advance(unsigned) {}
    constexpr bool applyParameter(eParameterId id,uint32_t value) {
        if(id!=Id) return false;
        gain=int32_t(value); return true;
    }
    constexpr Frame process(Frame input) { return {scale(input.left,gain),scale(input.right,gain)}; }
};
using TestInsert=TestEffect<PARAM_FX_DRIVE>;
using TestSend=TestEffect<PARAM_DELAY_FEEDBACK>;
constexpr bool replacement() {
    PartSignal<TestInsert> first,second;
    first.prepare(44100); second.prepare(44100);
    first.wet.reset(unity); second.wet.reset(unity); first.send.reset(unity);
    first.effect.applyParameter(PARAM_FX_DRIVE,unity/2);
    Frame send;
    const auto a=first.process({1000,-1000},send);
    if(a.left!=500 || a.right!=-500 || send.left!=500) return false;
    first.reset();
    const auto b=second.process({1000,-1000},send);
    return b.left==1000 && b.right==-1000 && !send.left &&
        validEffectPair<TestInsert,StereoDelayEffect<delayFrames>>() && !validEffectPair<TestInsert,DriveEffect>();
}
static_assert(replacement(),"Concrete replacement, independent instances, profile collision rejection");
constexpr bool ramps() {
    Ramp value;
    value.set(unity);
    int32_t previous=0;
    for(unsigned f=0;f<rampFrames;++f) {
        const auto next=value.step();
        if(next<previous || next>unity) return false;
        previous=next;
    }
    if(value.step()!=unity) return false;
    value.set(0); value.skip(rampFrames/2);
    if(value.current!=unity/2) return false;
    value.set(unity); value.skip(rampFrames);
    return value.current==unity && value.position==rampFrames;
}
constexpr bool insert() {
    PartSignal<DriveEffect> part;
    part.prepare(44100);
    Frame send;
    constexpr int32_t values[]={-32768,-16384,0,16384,32767};
    for(auto value:values) {
        const auto out=part.process({value,-value},send);
        if(out.left!=value || out.right!=-value || send.left || send.right) return false;
    }
    part.wet.reset(unity); part.effect.applyParameter(PARAM_FX_DRIVE,8*unity); part.effect.advance(rampFrames); part.send.reset(unity);
    for(auto value:values) {
        const auto out=part.process({value,value},send);
        if(out.left!=SugarMix::softClip(value*8) || send.left!=out.left || send.right!=out.right) return false;
    }
    part.level.reset(0);
    const auto silent=part.process({32767,-32768},send);
    return !silent.left && !silent.right && !send.left && !send.right;
}
constexpr bool impulse() {
    StereoDelayEffect<16> delay;
    if(delay.setTime(0) || delay.setTime(16) || !delay.setTime(3)) return false;
    delay.reset(); delay.feedback.reset(unity/2);
    for(unsigned f=0;f<16;++f) {
        const auto out=delay.process({f ? 0 : 1000,0});
        if(f<3 && (out.left || out.right)) return false;
        if(f==3 && (out.left!=1000 || out.right)) return false;
        if(f==6 && (out.left || out.right!=500)) return false;
        if(f==9 && (out.left!=250 || out.right)) return false;
    }
    delay.reset();
    for(unsigned f=0;f<64;++f) {
        const auto out=delay.process({});
        if(out.left || out.right) return false; // No stale cells after invalidation.
    }
    delay.feedback.reset(0);
    for(unsigned f=0;f<64;++f) {
        const auto out=delay.process({f ? 0 : -1000,0});
        if(out.left!=(f==3 ? -1000 : 0) || out.right) return false; // Wraparound, no feedback.
    }
    return true;
}
constexpr bool boundedFeedback() {
    StereoDelayEffect<16> delay;
    delay.setTime(7); delay.reset();
    delay.feedback.reset(int32_t(prepare(PARAM_DELAY_FEEDBACK,90,44100)));
    for(unsigned f=0;f<512;++f) {
        if(f%9==0) delay.setTime(f%13+1); // Retarget while a crossfade is active.
        const auto out=delay.process({f&1 ? -32768 : 32767,32767});
        if(out.left< -32767 || out.left>32767 || out.right< -32767 || out.right>32767) return false;
    }
    delay.reset();
    for(unsigned f=0;f<128;++f) {
        const auto out=delay.process({});
        if(out.left || out.right) return false;
    }
    return scale(-1,unity-1)==0 && scale(1,unity-1)==0;
}
constexpr bool preparation() {
    for(unsigned type=0;type<INSERTS_COUNT;++type) if(!choiceName(PARAM_FX_INSERT,type)) return false;
    return !choiceName(PARAM_FX_INSERT,INSERTS_COUNT) &&
        prepare(PARAM_PART_LEVEL,127,44100)==unity && prepare(PARAM_FX_SEND,0,44100)==0 &&
        prepare(PARAM_FX_DRIVE,127,44100)==8*unity && StereoDelayEffect<delayFrames>::prepareControl(StereoDelayEffect<delayFrames>::CONTROL_TIME,350,44100)<delayFrames &&
        StereoDelayEffect<delayFrames>::prepareControl(StereoDelayEffect<delayFrames>::CONTROL_FEEDBACK,90,44100)<unity && sharedParameter(PARAM_DELAY_RETURN) &&
        !sharedParameter(PARAM_FX_INSERT) && !parameter(PARAMETERS_COUNT);
}
static_assert(ramps(),"FX smoothing endpoint, monotonicity and retargeting");
static_assert(insert(),"Bit-exact bypass, insert bounds, post-level/post-insert send and mute");
static_assert(impulse(),"Delay onset, stereo feedback, history invalidation and ring wrap");
static_assert(boundedFeedback(),"Maximum feedback, rapid delay retargets and unbiased tail termination");
static_assert(preparation(),"FX metadata and Core1 coefficient preparation");
}

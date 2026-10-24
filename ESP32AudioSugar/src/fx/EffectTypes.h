#pragma once
#include "DriveEffect.h"
#include "StereoDelayEffect.h"
#include "InsertCatalog.h"
namespace SugarFX {
// Shared bus choice; per-part insert algorithms are registered in InsertCatalog.h.
using SharedSendEffect=StereoDelayEffect<delayFrames>;
static_assert(validLocalProfile<SharedSendEffect>(),"Invalid shared effect profile");
template<class Effect> constexpr unsigned controlByKey(const char* key) {
    for(unsigned i=0;i<Effect::controlCount();++i) if(equalKey(key,Effect::control(i)->key)) return i;
    return Effect::controlCount();
}
constexpr unsigned busTimeControl=controlByKey<SharedSendEffect>("time_ms");
constexpr unsigned busFeedbackControl=controlByKey<SharedSendEffect>("feedback");
constexpr bool legacyEffectsCompatible() {
    return equalKey(InsertPool::key(ALGORITHM_DRIVE),"sugar.drive.v1") && equalKey(SharedSendEffect::key(),"sugar.delay.v1");
}
}

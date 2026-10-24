#pragma once
#include "FxTypes.h"
namespace SugarFX {
enum eControlKind : uint8_t { CONTROL_INTEGER, CONTROL_CHOICE, CONTROL_KINDS_COUNT };
struct Control {
    const char *key,*name,*unit;
    int32_t minimum,maximum,defaultValue,step;
    eControlKind kind=CONTROL_INTEGER;
    const char* const* choices=nullptr;
    unsigned choiceCount=0;
};
constexpr bool validControl(const Control* p,int32_t value) {
    return p && p->minimum<=p->maximum && value>=p->minimum && value<=p->maximum;
}
constexpr bool equalKey(const char* a,const char* b) {
    if(!a || !b) return false;
    while(*a && *b) if(*a++!=*b++) return false;
    return *a==*b;
}
template<class Effect> constexpr bool validLocalProfile() {
    if(!Effect::key() || !*Effect::key() || !Effect::name() || !*Effect::name() || Effect::controlCount()>255) return false;
    for(unsigned i=0;i<Effect::controlCount();++i) {
        const auto* p=Effect::control(i);
        if(!p || !p->key || !*p->key || !p->name || !*p->name || !p->unit || p->step<=0 ||
           !validControl(p,p->defaultValue) || p->kind>=CONTROL_KINDS_COUNT) return false;
        if(p->kind==CONTROL_CHOICE && (!p->choices || p->minimum!=0 || p->maximum!=int32_t(p->choiceCount)-1)) return false;
        if(p->kind==CONTROL_CHOICE) for(unsigned j=0;j<p->choiceCount;++j)
            if(!p->choices[j] || !*p->choices[j]) return false;
        for(unsigned j=0;j<i;++j) if(equalKey(p->key,Effect::control(j)->key)) return false;
    }
    return !Effect::control(Effect::controlCount());
}
}

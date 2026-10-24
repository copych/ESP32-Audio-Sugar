#include "InstrumentEngine.h"
#include "../../Config.h"
#include "PartTypes.h"
#include "OutputMeter.h"
#include "../fx/FxRack.h"
#include "../midi/EventQueue.h"
#include "../midi/EventDispatch.h"
#include "../parameters/ParameterRequests.h"
#include "esp_log.h"
namespace SugarEngine {

using namespace SugarParameters;
using namespace SugarParts;
static_assert(SUGAR_PART_CAPACITY>0 && SUGAR_PART_CAPACITY<=PARTS_COUNT,"Invalid part capacity");
static DRAM_ATTR SugarAudio::InstrumentRack rack;
static DRAM_ATTR SugarMix::OutputMeter outputMeter;
static DRAM_ATTR SugarMidi::EventQueue<SUGAR_MIDI_QUEUE_CAPACITY> queue;
static DRAM_ATTR uint32_t seenOverflow=0;
constexpr unsigned nativeStorage=SugarAudio::SynthLayout::nativeWords ? SugarAudio::SynthLayout::nativeWords : 1;
static int32_t requestedSynth[nativeStorage]={};
static DRAM_ATTR SugarCore::SharedWord appliedSynth[nativeStorage];
static DRAM_ATTR State requested[SUGAR_PART_CAPACITY];
static DRAM_ATTR SugarCore::SharedWord applied[SUGAR_PART_CAPACITY][PARAMETERS_COUNT][SugarParameters::MIDI_CHANNELS_COUNT];
static DRAM_ATTR SugarCore::SharedWord activeParts;
static uint32_t requestedActive=partBit(PART_1); // sole Core 1 writer
static Routing<SUGAR_PART_CAPACITY> routing;
static DRAM_ATTR int16_t staging[SUGAR_BLOCK_FRAMES*2];
static DRAM_ATTR int32_t mix[SUGAR_BLOCK_FRAMES*2];
static DRAM_ATTR SugarFX::Rack<SUGAR_PART_CAPACITY,SUGAR_BLOCK_FRAMES,SUGAR_SAMPLE_RATE> fx;
struct FxApplied {
    SugarCore::SharedWord algorithm,values[SugarFX::InsertPool::maxControls];
};
static SugarFX::SlotState requestedFx[SUGAR_PART_CAPACITY][SugarFX::FX_SLOTS_COUNT]; // Core1 only
static int32_t requestedBus[SugarFX::SharedSendEffect::controlCount()]={};
static DRAM_ATTR FxApplied appliedFx[SUGAR_PART_CAPACITY][SugarFX::FX_SLOTS_COUNT];
static DRAM_ATTR SugarCore::SharedWord appliedBus[SugarFX::SharedSendEffect::controlCount()];
static_assert(SugarFX::InsertPool::maxControls+1<=SUGAR_MIDI_QUEUE_CAPACITY,"FX selection transaction exceeds queue");
static DRAM_ATTR SugarCore::SharedWord fxReturnPeak;
static DRAM_ATTR unsigned fxMeterBlocks=0;
// One immutable transfer bank: Core1 fills only when ready==0; Core0 releases
// it after all bounded batches have been applied. No pointers enter the queue.
constexpr unsigned presetEventCapacity=SugarAudio::SynthLayout::nativeWords+SUGAR_PART_CAPACITY*(PARAMETERS_COUNT*SugarParameters::MIDI_CHANNELS_COUNT+SugarFX::FX_SLOTS_COUNT*(SugarFX::InsertPool::maxControls+1))+SugarFX::SharedSendEffect::controlCount();
static DRAM_ATTR SugarMidi::Event presetEvents[presetEventCapacity];
static DRAM_ATTR unsigned presetEventCount=0,presetPosition=0;
static DRAM_ATTR uint32_t presetActive=0;
static DRAM_ATTR SugarCore::SharedWord presetReady;
static uint32_t presetRejections=0; // Core1 only
constexpr bool compiledPart(ePartId part) { return unsigned(part)<SUGAR_PART_CAPACITY; }
struct EngineTarget {
    inline __attribute__((always_inline)) void silence() { rack.forEach([](auto& instrument) { instrument.silence(); }); fx.clearTail(); }
    void onEvent(const SugarMidi::Event& e);
};
void IRAM_ATTR __attribute__((optimize("-fno-jump-tables"))) EngineTarget::onEvent(const SugarMidi::Event& e) {
        if(e.type==SugarMidi::EVENT_ROUTING_RESET) { silence(); return; }
        if(!compiledPart(e.part)) return;
        if(e.type==SugarMidi::EVENT_FX_SELECT) {
            if(e.data1>=SugarFX::FX_SLOTS_COUNT || !fx.select(e.part,e.data1,SugarFX::eAlgorithm(e.channel))) return;
            auto& mirror=appliedFx[e.part][e.data1];
            mirror.algorithm.store(e.channel);
            // Counts travel with the validated command; no metadata reads on Core0.
            for(unsigned i=e.data2;i<SugarFX::InsertPool::maxControls;++i) mirror.values[i].store(0);
            if(e.data1==SugarFX::FX_SLOT_FIRST)
                applied[e.part][PARAM_FX_INSERT][0].store(e.channel==SugarFX::ALGORITHM_BYPASS ? SugarFX::INSERT_BYPASS : SugarFX::INSERT_EFFECT);
            return;
        }
        if(e.type==SugarMidi::EVENT_FX_CONTROL) {
            if(e.data1>=SugarFX::FX_SLOTS_COUNT || e.data2>=SugarFX::InsertPool::maxControls ||
               !fx.control(e.part,e.data1,SugarFX::eAlgorithm(e.channel),e.data2,e.prepared)) return;
            appliedFx[e.part][e.data1].values[e.data2].store(uint32_t(e.value));
            if(e.data1==SugarFX::FX_SLOT_FIRST && e.channel==SugarFX::ALGORITHM_DRIVE)
                applied[e.part][PARAM_FX_DRIVE][0].store(uint32_t(e.value));
            return;
        }
        if(e.type==SugarMidi::EVENT_FX_BUS_CONTROL) {
            if(e.data1>=SugarFX::SharedSendEffect::controlCount() || !fx.busControl(e.data1,e.prepared)) return;
            appliedBus[e.data1].store(uint32_t(e.value));
            if(e.data1==SugarFX::busTimeControl) applied[0][PARAM_DELAY_TIME_MS][0].store(uint32_t(e.value));
            if(e.data1==SugarFX::busFeedbackControl) applied[0][PARAM_DELAY_FEEDBACK][0].store(uint32_t(e.value));
            return;
        }
        if(e.type==SugarMidi::EVENT_SYNTH_PARAMETER) {
            if(e.channel>=SugarParameters::MIDI_CHANNELS_COUNT) return;
            if(e.data2>PARAMETERS_COUNT) return;
            if(e.data2==PARAMETERS_COUNT && e.parameterOffset>=SugarAudio::SynthLayout::nativeWords) return;
            rack.visit(e.part,[&](auto& instrument) {
                if(e.data1<instrument.parameterCount() && instrument.applyLocalParameter(e)) {
                    if(e.data2<PARAMETERS_COUNT) applied[e.part][e.data2][e.channel].store(uint32_t(e.value));
                    else appliedSynth[e.parameterOffset].store(uint32_t(e.value));
                }
            });
            return;
        }
        if(e.type==SugarMidi::EVENT_PARAMETER && SugarFX::parameter(eParameterId(e.data1))) {
            if(fx.apply(e.part,eParameterId(e.data1),e.prepared))
            {
                applied[e.part][e.data1][e.channel].store(uint32_t(e.value));
                if(e.data1==PARAM_FX_INSERT) {
                    auto& mirror=appliedFx[e.part][SugarFX::FX_SLOT_FIRST];
                    mirror.algorithm.store(e.value==SugarFX::INSERT_EFFECT ? SugarFX::ALGORITHM_DRIVE : SugarFX::ALGORITHM_BYPASS);
                    for(auto& v:mirror.values) v.store(0);
                    if(e.value==SugarFX::INSERT_EFFECT) mirror.values[SugarFX::DriveEffect::CONTROL_AMOUNT].store(applied[e.part][PARAM_FX_DRIVE][0].load());
                } else if(e.data1==PARAM_FX_DRIVE && appliedFx[e.part][0].algorithm.load()==SugarFX::ALGORITHM_DRIVE)
                    appliedFx[e.part][0].values[SugarFX::DriveEffect::CONTROL_AMOUNT].store(uint32_t(e.value));
                else if(e.data1==PARAM_DELAY_TIME_MS && SugarFX::busTimeControl<SugarFX::SharedSendEffect::controlCount())
                    appliedBus[SugarFX::busTimeControl].store(uint32_t(e.value));
                else if(e.data1==PARAM_DELAY_FEEDBACK && SugarFX::busFeedbackControl<SugarFX::SharedSendEffect::controlCount())
                    appliedBus[SugarFX::busFeedbackControl].store(uint32_t(e.value));
            }
            return;
        }

        rack.visit(e.part,[&](auto& instrument) {
            if(e.type==SugarMidi::EVENT_PART_ACTIVE) {
                const uint32_t bit=partBit(e.part), current=activeParts.load();
                if(bool(current&bit)!=bool(e.data1)) instrument.silence();
                activeParts.store(e.data1 ? current|bit : current&~bit);
            } else if(e.type==SugarMidi::EVENT_PARAMETER) {
                // Synth edits use EVENT_SYNTH_PARAMETER; this legacy event is framework-only.
            } else if(activeParts.load()&partBit(e.part)) instrument.onEvent(e);
        });
}
static EngineTarget target;
static SugarMidi::Event fxControlEvent(ePartId part,unsigned slot,SugarFX::eAlgorithm type,unsigned id,int32_t value) {
    return {SugarMidi::EVENT_FX_CONTROL,uint8_t(type),uint8_t(slot),uint8_t(id),value,
        SugarFX::InsertPool::prepareControl(type,id,value,SUGAR_SAMPLE_RATE),part};
}
static SugarMidi::Event fxSelectEvent(ePartId part,unsigned slot,SugarFX::eAlgorithm type) {
    return {SugarMidi::EVENT_FX_SELECT,uint8_t(type),uint8_t(slot),uint8_t(SugarFX::InsertPool::controlCount(type)),0,0,part};
}
static SugarMidi::Event fxBusEvent(unsigned id,int32_t value) {
    return {SugarMidi::EVENT_FX_BUS_CONTROL,0,uint8_t(id),0,value,
        SugarFX::SharedSendEffect::prepareControl(id,value,SUGAR_SAMPLE_RATE),PART_1};
}
const char* getFxName(SugarFX::eAlgorithm type) { return SugarFX::InsertPool::name(type); }
bool getFxAlgorithm(ePartId part,unsigned slot,SugarFX::eAlgorithm& type,bool audio) {
    if(!compiledPart(part) || slot>=SugarFX::FX_SLOTS_COUNT) return false;
    type=audio ? SugarFX::eAlgorithm(appliedFx[part][slot].algorithm.load()) : requestedFx[part][slot].algorithm;
    return true;
}
unsigned getFxControlCount(ePartId part,unsigned slot) {
    return compiledPart(part) && slot<SugarFX::FX_SLOTS_COUNT ? SugarFX::InsertPool::controlCount(requestedFx[part][slot].algorithm) : 0;
}
const SugarFX::Control* getFxControl(ePartId part,unsigned slot,unsigned id) {
    return compiledPart(part) && slot<SugarFX::FX_SLOTS_COUNT ? SugarFX::InsertPool::control(requestedFx[part][slot].algorithm,id) : nullptr;
}
bool getFxValue(ePartId part,unsigned slot,unsigned id,int32_t& value,bool audio) {
    if(!getFxControl(part,slot,id)) return false;
    value=audio ? int32_t(appliedFx[part][slot].values[id].load()) : requestedFx[part][slot].values[id]; return true;
}
bool selectFx(ePartId part,unsigned slot,SugarFX::eAlgorithm type) {
    using namespace SugarFX;
    if(presetBusy() || !compiledPart(part) || slot>=FX_SLOTS_COUNT || type>=ALGORITHMS_COUNT) return false;
    if(requestedFx[part][slot].algorithm==type) return true;
    const auto next=defaultSlot(type);
    SugarMidi::Event commands[InsertPool::maxControls+1]; unsigned count=0;
    for(unsigned i=0;i<InsertPool::controlCount(type);++i) commands[count++]=fxControlEvent(part,slot,type,i,next.values[i]);
    commands[count++]=fxSelectEvent(part,slot,type); // publish selection after all prepared defaults
    if(!queue.pushBatch(commands,count)) return false;
    requestedFx[part][slot]=next;
    if(slot==FX_SLOT_FIRST) {
        requested[part].set(PARAM_FX_INSERT,type==ALGORITHM_BYPASS ? INSERT_BYPASS : INSERT_EFFECT);
        if(type==ALGORITHM_DRIVE) requested[part].set(PARAM_FX_DRIVE,next.values[DriveEffect::CONTROL_AMOUNT]);
    }
    return true;
}
bool setFxControl(ePartId part,unsigned slot,unsigned id,int32_t value) {
    if(presetBusy() || !SugarFX::validControl(getFxControl(part,slot,id),value)) return false;
    const auto type=requestedFx[part][slot].algorithm;
    if(!queue.push(fxControlEvent(part,slot,type,id,value))) return false;
    requestedFx[part][slot].values[id]=value;
    if(slot==SugarFX::FX_SLOT_FIRST && type==SugarFX::ALGORITHM_DRIVE) requested[part].set(PARAM_FX_DRIVE,value);
    return true;
}
bool setFxBusControl(unsigned id,int32_t value) {
    if(presetBusy() || !SugarFX::validControl(SugarFX::SharedSendEffect::control(id),value)) return false;
    if(!queue.push(fxBusEvent(id,value))) return false;
    requestedBus[id]=value;
    if(id==SugarFX::busTimeControl) requested[0].setWithDescriptor(getParameterDescriptor(PART_1,PARAM_DELAY_TIME_MS),PARAM_DELAY_TIME_MS,value);
    if(id==SugarFX::busFeedbackControl) requested[0].setWithDescriptor(getParameterDescriptor(PART_1,PARAM_DELAY_FEEDBACK),PARAM_DELAY_FEEDBACK,value);
    return true;
}
bool getFxBusValue(unsigned id,int32_t& value,bool audio) {
    if(id>=SugarFX::SharedSendEffect::controlCount()) return false;
    value=audio ? int32_t(appliedBus[id].load()) : requestedBus[id]; return true;
}
static SugarMidi::Event prepareParameterEvent(ePartId part,eParameterId id,int32_t value,uint8_t channel) {
    SugarMidi::Event e{SugarMidi::EVENT_PARAMETER,channel,uint8_t(id),0,value,0,part};
    if(SugarFX::parameter(id)) { e.prepared=SugarFX::prepare(id,value,SUGAR_SAMPLE_RATE); return e; }
    rack.visit(part,[&](auto& instrument) {
        unsigned local=0;
        for(;local<instrument.parameterCount();++local) {
            const auto* p=instrument.parameter(local);
            if(p->legacy && p->legacy->id==id) break;
        }
        e.type=SugarMidi::EVENT_SYNTH_PARAMETER; e.data1=uint8_t(local); e.data2=uint8_t(id);
        e.prepared=instrument.prepareLocalParameter(local,value,SUGAR_SAMPLE_RATE);
    });
    return e;
}
unsigned getSynthParameterCount(ePartId part) {
    unsigned result=0;
    if(compiledPart(part)) rack.visit(part,[&](auto& instrument) { result=instrument.parameterCount(); });
    return result;
}
const SugarSynth::Parameter* getSynthParameter(ePartId part,unsigned id) {
    const SugarSynth::Parameter* result=nullptr;
    if(compiledPart(part)) rack.visit(part,[&](auto& instrument) { result=instrument.parameter(id); });
    return result;
}
unsigned getSynthOffset(ePartId part,unsigned id) {
    const auto* p=getSynthParameter(part,id);
    if(!p || p->legacy) return SugarAudio::SynthLayout::nativeWords;
    unsigned result=0,index=0;
    rack.forEach([&](auto& instrument) {
        if(index<unsigned(part)) result+=SugarSynth::nativeWordsFor(instrument);
        else if(index==unsigned(part)) result+=SugarSynth::nativeOffsetFor(instrument,id);
        ++index;
    });
    return result;
}
static SugarMidi::Event synthEvent(ePartId part,unsigned id,int32_t value,uint8_t channel) {
    SugarMidi::Event event{SugarMidi::EVENT_SYNTH_PARAMETER,channel,uint8_t(id),PARAMETERS_COUNT,value,0,part};
    event.parameterOffset=uint16_t(getSynthOffset(part,id)+channel);
    rack.visit(part,[&](auto& instrument) { event.prepared=instrument.prepareLocalParameter(id,value,SUGAR_SAMPLE_RATE); });
    return event;
}
bool setSynthParameter(ePartId part,unsigned id,int32_t value,uint8_t channel) {
    const auto* p=getSynthParameter(part,id);
    if(!SugarSynth::valid(p,value,channel)) return false;
    if(p->legacy) return setParameter(part,p->legacy->id,value,channel);
    if(presetBusy()) { ++presetRejections; return false; }
    auto event=synthEvent(part,id,value,channel);
    if(!queue.push(event)) return false;
    requestedSynth[event.parameterOffset]=value; return true;
}
bool getSynthValue(ePartId part,unsigned id,int32_t& value,uint8_t channel,bool audio) {
    const auto* p=getSynthParameter(part,id);
    if(!p || channel>=SugarParameters::MIDI_CHANNELS_COUNT || (p->scope==PARAM_SCOPE_PART && channel)) return false;
    if(p->legacy) return audio ? getAppliedParameter(part,p->legacy->id,value,channel) : getRequestedParameter(part,p->legacy->id,value,channel);
    const auto offset=getSynthOffset(part,id)+channel;
    value=audio ? int32_t(appliedSynth[offset].load()) : requestedSynth[offset]; return true;
}
const Descriptor* getParameterDescriptor(ePartId part,eParameterId id) {
    if(!compiledPart(part) || !validId(id)) return nullptr;
    if(SugarFX::parameter(id)) return SugarFX::parameterDescriptor(id,part==PART_1);
    const Descriptor* result=nullptr;
    rack.visit(part,[&](auto& instrument) { result=SugarAudio::descriptorFor(instrument,id); });
    return result;
}
const char* getInstrumentName(ePartId part) {
    const char* result=nullptr;
    if(compiledPart(part)) rack.visit(part,[&](auto& instrument) { result=SugarAudio::nameFor(instrument); });
    return result;
}
const char* getInstrumentKey(ePartId part) {
    const char* result=nullptr;
    if(compiledPart(part)) rack.visit(part,[&](auto& instrument) { result=instrument.key(); });
    return result;
}
bool getDefaultParameter(ePartId part,eParameterId id,int32_t& value) {
    if(!getParameterDescriptor(part,id)) return false;
    if(SugarFX::parameter(id)) { value=getParameterDescriptor(part,id)->defaultValue; return true; }
    rack.visit(part,[&](auto& instrument) { value=SugarAudio::defaultFor(instrument,id); });
    return true;
}
uint32_t getInstrumentCapabilities(ePartId part) {
    uint32_t result=0;
    if(compiledPart(part)) rack.visit(part,[&](auto& instrument) { result=SugarAudio::capabilitiesFor(instrument); });
    return result;
}
void prepare() {
    fx.prepare();
    for(auto& part:requestedFx) for(auto& slot:part) slot=SugarFX::defaultSlot(SugarFX::ALGORITHM_BYPASS);
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) {
        requestedBus[i]=SugarFX::SharedSendEffect::control(i)->defaultValue; appliedBus[i].store(uint32_t(requestedBus[i]));
    }
    activeParts.store(partBit(PART_1));
    requestedActive=partBit(PART_1);
    routing=Routing<SUGAR_PART_CAPACITY>{};
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        requested[p].reset();
        rack.visit(p,[&](auto& instrument) {
            instrument.prepare();
            for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
                const auto id=eParameterId(raw);
                const auto* d=getParameterDescriptor(ePartId(p),id);
                if(!d) continue;
                int32_t value=0; getDefaultParameter(ePartId(p),id,value);
                const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
                for(unsigned ch=0;ch<count;++ch) {
                    requested[p].setWithDescriptor(d,id,value,ch);
                    target.onEvent(prepareParameterEvent(ePartId(p),id,value,uint8_t(ch)));
                }
            }
        });
        for(unsigned id=0;id<getSynthParameterCount(ePartId(p));++id) {
            const auto* d=getSynthParameter(ePartId(p),id);
            if(d->legacy) continue;
            for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) {
                const auto e=synthEvent(ePartId(p),id,d->defaultValue,uint8_t(ch));
                requestedSynth[e.parameterOffset]=d->defaultValue; target.onEvent(e);
            }
        }
    }
}
bool setParameter(ePartId part,eParameterId id,int32_t value,uint8_t channel) {
    if(presetBusy()) { ++presetRejections; return false; }
    const auto* d=getParameterDescriptor(part,id);
    if(!validDescriptor(d,id,value,channel)) return false;
    if(!submitPrepared(requested[part],queue,d,prepareParameterEvent(part,id,value,channel))) return false;
    if(id==PARAM_FX_INSERT) {
        auto& slot=requestedFx[part][SugarFX::FX_SLOT_FIRST];
        slot=SugarFX::defaultSlot(value==SugarFX::INSERT_EFFECT ? SugarFX::ALGORITHM_DRIVE : SugarFX::ALGORITHM_BYPASS);
        if(slot.algorithm==SugarFX::ALGORITHM_DRIVE) requested[part].get(PARAM_FX_DRIVE,slot.values[SugarFX::DriveEffect::CONTROL_AMOUNT]);
    } else if(id==PARAM_FX_DRIVE && requestedFx[part][0].algorithm==SugarFX::ALGORITHM_DRIVE)
        requestedFx[part][0].values[SugarFX::DriveEffect::CONTROL_AMOUNT]=value;
    else if(id==PARAM_DELAY_TIME_MS && SugarFX::busTimeControl<SugarFX::SharedSendEffect::controlCount()) requestedBus[SugarFX::busTimeControl]=value;
    else if(id==PARAM_DELAY_FEEDBACK && SugarFX::busFeedbackControl<SugarFX::SharedSendEffect::controlCount()) requestedBus[SugarFX::busFeedbackControl]=value;
    return true;
}
bool getRequestedParameter(ePartId part,eParameterId id,int32_t& value,uint8_t channel) {
    return getParameterDescriptor(part,id) && requested[part].get(id,value,channel);
}
bool getAppliedParameter(ePartId part,eParameterId id,int32_t& value,uint8_t channel) {
    const auto* d=getParameterDescriptor(part,id);
    if(!compiledPart(part) || !d || channel>=SugarParameters::MIDI_CHANNELS_COUNT || (d->scope==PARAM_SCOPE_PART && channel)) return false;
    value=int32_t(applied[part][id][channel].load()); return true;
}
bool setVoiceMode(ePartId part,SugarAudio::eVoiceMode mode) { return setParameter(part,PARAM_VOICE_MODE,mode); }
bool setPartActive(ePartId part,bool active) {
    if(presetBusy()) { ++presetRejections; return false; }
    if(!compiledPart(part)) return false;
    SugarMidi::Event e{SugarMidi::EVENT_PART_ACTIVE,0,uint8_t(active),0}; e.part=part;
    if(!queue.push(e)) return false;
    const uint32_t bit=partBit(part);
    requestedActive=active ? requestedActive|bit : requestedActive&~bit;
    return true;
}
bool isPartActive(ePartId part) { return compiledPart(part) && (activeParts.load()&partBit(part)); }
bool setChannelRoute(uint8_t channel,uint32_t mask) {
    if(presetBusy()) { ++presetRejections; return false; }
    if(channel>=SugarParts::MIDI_CHANNELS_COUNT || (mask&~Routing<SUGAR_PART_CAPACITY>::availableMask)) return false;
    if(routing.destinations(channel,Routing<SUGAR_PART_CAPACITY>::availableMask)==mask) return true;
    if(!queue.push({SugarMidi::EVENT_ROUTING_RESET,0,0,0})) return false;
    return routing.set(channel,mask);
}
bool getChannelRoute(uint8_t channel,uint32_t& mask) {
    if(channel>=SugarParts::MIDI_CHANNELS_COUNT) return false;
    mask=routing.destinations(channel,Routing<SUGAR_PART_CAPACITY>::availableMask);
    return true;
}
bool setBusParameter(SugarFX::eBus bus,eParameterId id,int32_t value) {
    return bus==SugarFX::BUS_DELAY && SugarFX::sharedParameter(id) && setParameter(PART_1,id,value);
}
bool getRequestedBusParameter(SugarFX::eBus bus,eParameterId id,int32_t& value) {
    return bus==SugarFX::BUS_DELAY && SugarFX::sharedParameter(id) && getRequestedParameter(PART_1,id,value);
}
bool getAppliedBusParameter(SugarFX::eBus bus,eParameterId id,int32_t& value) {
    return bus==SugarFX::BUS_DELAY && SugarFX::sharedParameter(id) && getAppliedParameter(PART_1,id,value);
}
bool enqueueToPart(ePartId part,const SugarMidi::Event& input) {
    if(presetBusy()) { ++presetRejections; return false; }
    if(!compiledPart(part) || input.channel>=SugarParts::MIDI_CHANNELS_COUNT) return false;
    if(input.type==SugarMidi::EVENT_CONTROL_CHANGE) {
        if(input.data1>=120 || input.data2>127) return false;
        for(unsigned id=0;id<getSynthParameterCount(part);++id) {
            const auto* d=getSynthParameter(part,id);
            if(d->midiCC!=input.data1) continue;
            const int64_t span=int64_t(d->maximum)-d->minimum;
            const int32_t value=int32_t(d->minimum+(span*input.data2+63)/127);
            return setSynthParameter(part,id,value,d->scope==PARAM_SCOPE_PART_CHANNEL ? input.channel : 0);
        }
        return true; // Unmapped CC is a valid no-op.
    }
    if(input.type==SugarMidi::EVENT_GAIN) return setParameter(part,PARAM_GAIN,input.data2,input.channel);
    if(input.type==SugarMidi::EVENT_PARAMETER) {
        const auto id=eParameterId(input.data1);
        if(!validId(id)) return false;
        const auto* d=getParameterDescriptor(part,id);
        // A routed CC for an unsupported control (e.g. drums) is a no-op.
        if(!d) return true;
        return setParameter(part,id,input.value,d->scope==PARAM_SCOPE_PART_CHANNEL ? input.channel : 0);
    }
    if(input.type==SugarMidi::EVENT_VOICE_MODE) return setParameter(part,PARAM_VOICE_MODE,input.data1);
    if(input.type!=SugarMidi::EVENT_NOTE_ON && input.type!=SugarMidi::EVENT_NOTE_OFF && input.type!=SugarMidi::EVENT_ALL_OFF) return false;
    if(input.data1>127 || input.data2>127) return false;
    auto routed=input; routed.part=part; return queue.push(routed);
}
bool enqueue(const SugarMidi::Event& input) {
    if(presetBusy()) { ++presetRejections; return false; }
    if(input.channel>=SugarParts::MIDI_CHANNELS_COUNT) return false;
    const uint32_t mask=routing.destinations(input.channel,requestedActive);
    bool success=true;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        if(mask&partBit(ePartId(p))) { if(!enqueueToPart(ePartId(p),input)) success=false; }
    return success;
}
bool setParameter(eParameterId id,int32_t value,uint8_t channel) { return setParameter(PART_1,id,value,channel); }
bool getRequestedParameter(eParameterId id,int32_t& value,uint8_t channel) { return getRequestedParameter(PART_1,id,value,channel); }
bool getAppliedParameter(eParameterId id,int32_t& value,uint8_t channel) { return getAppliedParameter(PART_1,id,value,channel); }
bool setVoiceMode(SugarAudio::eVoiceMode mode) { return setVoiceMode(PART_1,mode); }
bool presetBusy() { return presetReady.load()!=0; }
bool capturePreset(SugarPresets::State& state) {
    if(presetBusy()) return false;
    for(unsigned i=0;i<SugarAudio::SynthLayout::nativeWords;++i) state.synthValues[i]=requestedSynth[i];
    state.active=requestedActive;
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch)
        state.routes[ch]=routing.destinations(ch,Routing<SUGAR_PART_CAPACITY>::availableMask);
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch) requested[p].get(id,state.values[p][id][ch],ch);
        }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) state.fx[p][slot]=requestedFx[p][slot];
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) state.busValues[i]=requestedBus[i];
    return true;
}
bool submitPreset(const SugarPresets::State& state) {
    if(presetBusy() || !SugarPresets::validMasks(state) || !SugarPresets::validEffects(state)) return false;
    // Validate everything before changing requested state, routing or publication.
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch)
                if(!validDescriptor(d,id,state.values[p][id][ch],ch)) return false;
        }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned id=0;id<getSynthParameterCount(ePartId(p));++id) {
        const auto* d=getSynthParameter(ePartId(p),id);
        if(d->legacy) continue;
        for(unsigned ch=0;ch<SugarSynth::words(*d);++ch)
            if(!SugarSynth::valid(d,state.synthValues[getSynthOffset(ePartId(p),id)+ch],ch)) return false;
    }
    presetEventCount=0;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch) {
                const int32_t value=state.values[p][id][ch];
                presetEvents[presetEventCount++]=prepareParameterEvent(ePartId(p),id,value,uint8_t(ch));
                requested[p].setWithDescriptor(d,id,value,ch);
            }
        }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned id=0;id<getSynthParameterCount(ePartId(p));++id) {
        const auto* d=getSynthParameter(ePartId(p),id);
        if(d->legacy) continue;
        for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) {
            auto e=synthEvent(ePartId(p),id,state.synthValues[getSynthOffset(ePartId(p),id)+ch],uint8_t(ch));
            presetEvents[presetEventCount++]=e; requestedSynth[e.parameterOffset]=e.value;
        }
    }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
            const auto& setting=state.fx[p][slot];
            for(unsigned i=0;i<SugarFX::InsertPool::controlCount(setting.algorithm);++i)
                presetEvents[presetEventCount++]=fxControlEvent(ePartId(p),slot,setting.algorithm,i,setting.values[i]);
            presetEvents[presetEventCount++]=fxSelectEvent(ePartId(p),slot,setting.algorithm);
            requestedFx[p][slot]=setting;
        }
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) {
        presetEvents[presetEventCount++]=fxBusEvent(i,state.busValues[i]); requestedBus[i]=state.busValues[i];
    }
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) routing.set(ch,state.routes[ch]);
    requestedActive=state.active; presetActive=state.active; presetPosition=0;
    presetReady.store(1);
    return true;
}
void quiesceForStorage() {
    SugarMidi::applyPending<SUGAR_MIDI_QUEUE_CAPACITY>(queue,target,seenOverflow);
    target.silence();
}
void IRAM_ATTR render(int16_t* pcm) {
    if(presetReady.load()) {
        if(presetPosition==0) {
            target.silence(); activeParts.store(0); queue.discard(); seenOverflow=queue.overflows();
        }
        const unsigned end=presetPosition+SUGAR_EVENTS_PER_BLOCK<presetEventCount ?
            presetPosition+SUGAR_EVENTS_PER_BLOCK : presetEventCount;
        while(presetPosition<end) target.onEvent(presetEvents[presetPosition++]);
        if(presetPosition==presetEventCount) { fx.clearTail(); activeParts.store(presetActive); presetReady.store(0); }
    } else SugarMidi::applyPending<SUGAR_EVENTS_PER_BLOCK>(queue,target,seenOverflow);
    const uint32_t active=activeParts.load();
    fx.beginBlock(mix);
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        if(active&partBit(ePartId(p))) {
            rack.visit(p,[&](auto& instrument) { instrument.render({staging}); });
            fx.addPart(p,staging,mix);
        } else fx.inactivePart(p);
    }
    for(unsigned frame=0;frame<SUGAR_BLOCK_FRAMES;++frame) {
        const auto output=fx.outputFrame(frame,mix);
        pcm[frame*2]=SugarMix::softClip(output.left);
        pcm[frame*2+1]=SugarMix::softClip(output.right);
        outputMeter.add(output.left,pcm[frame*2]); outputMeter.add(output.right,pcm[frame*2+1]);
    }
    if(++fxMeterBlocks==1024) { fxMeterBlocks=0; fxReturnPeak.store(fx.takePeak()); }
    outputMeter.finishBlock();
}
uint32_t getFxReturnPeak() { return fxReturnPeak.load(); }
void report() {
    outputMeter.report();
    ESP_LOGI("SugarFX","returnPeak=%lu windowBlocks=1024",(unsigned long)getFxReturnPeak());
    unsigned differing=0;
    for(unsigned i=0;i<SugarAudio::SynthLayout::nativeWords;++i) if(requestedSynth[i]!=int32_t(appliedSynth[i].load())) ++differing;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        if(bool(requestedActive&partBit(ePartId(p)))!=bool(activeParts.load()&partBit(ePartId(p)))) ++differing;
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch) {
                int32_t value=0; requested[p].get(id,value,ch);
                if(value!=int32_t(applied[p][id][ch].load())) ++differing;
            }
        }
    }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
        const auto& expected=requestedFx[p][slot]; const auto& actual=appliedFx[p][slot];
        if(unsigned(expected.algorithm)!=actual.algorithm.load()) ++differing;
        for(unsigned i=0;i<SugarFX::InsertPool::controlCount(expected.algorithm);++i)
            if(expected.values[i]!=int32_t(actual.values[i].load())) ++differing;
    }
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i)
        if(requestedBus[i]!=int32_t(appliedBus[i].load())) ++differing;
    ESP_LOGI("SugarEngine","eventQueueDrops=%lu parameterDifferences=%u activeParts=0x%lx capacity=%u presetBusy=%u presetRejected=%lu",
             (unsigned long)queue.overflows(),differing,(unsigned long)activeParts.load(),unsigned(SUGAR_PART_CAPACITY),unsigned(presetBusy()),(unsigned long)presetRejections);
}
const char* getParameterChoiceName(SugarParameters::eParameterId id,unsigned choice) {
    if(id==SugarParameters::PARAM_FX_INSERT && choice==SugarFX::INSERT_EFFECT) return SugarFX::InsertPool::name(SugarFX::ALGORITHM_DRIVE);
    return SugarParameters::choiceName(id,choice);
}
}

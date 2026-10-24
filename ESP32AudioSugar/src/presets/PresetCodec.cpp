// JSON is control-side only. No ArduinoJson type crosses the audio boundary.
#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include "PresetCodec.h"
#include "../audio/InstrumentEngine.h"
#include "../fx/FxRack.h"
#if ARDUINOJSON_VERSION_MAJOR != 7 || ARDUINOJSON_VERSION_MINOR != 4 || ARDUINOJSON_VERSION_REVISION != 3
#error "Install the verified ArduinoJson 7.4.3 version using Library Manager."
#endif
namespace SugarPresets {
namespace {
const char* checkDetail="not run";
const char* decodeDetail="not run";
using namespace SugarParameters;
using namespace SugarParts;
// Bounded heap use on Core1. Allocator virtual calls are confined to the parser;
// instrument dispatch and Core0 remain concrete and allocation-free.
class BudgetAllocator final : public ArduinoJson::Allocator {
    struct alignas(8) Header { size_t size; };
    static constexpr size_t limit=24576;
    size_t used=0;
public:
    void* allocate(size_t size) override {
        if(size>limit-used) return nullptr;
        auto* h=static_cast<Header*>(heap_caps_malloc(size+sizeof(Header),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!h) return nullptr;
        h->size=size; used+=size; return h+1;
    }
    void deallocate(void* pointer) override {
        if(!pointer) return;
        auto* h=static_cast<Header*>(pointer)-1;
        used-=h->size; heap_caps_free(h);
    }
    void* reallocate(void* pointer,size_t size) override {
        if(!pointer) return allocate(size);
        if(!size) { deallocate(pointer); return nullptr; }
        auto* h=static_cast<Header*>(pointer)-1;
        const size_t previous=h->size;
        if(size>limit-(used-previous)) return nullptr;
        auto* next=static_cast<Header*>(heap_caps_realloc(h,size+sizeof(Header),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!next) return nullptr;
        used=used-previous+size; next->size=size; return next+1;
    }
};
bool textEquals(JsonVariantConst value,const char* expected) {
    if(!value.is<JsonString>() || !expected) return false;
    const auto string=value.as<JsonString>();
    return string.size()==strlen(expected) && memcmp(string.c_str(),expected,string.size())==0;
}
bool validValues(const State& state) {
    if(!validMasks(state) || !validEffects(state)) return false;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch)
                if(!validDescriptor(d,id,state.values[p][id][ch],ch)) return false;
        }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned id=0;id<SugarEngine::getSynthParameterCount(ePartId(p));++id) {
        const auto* d=SugarEngine::getSynthParameter(ePartId(p),id);
        if(d->legacy) continue;
        for(unsigned ch=0;ch<SugarSynth::words(*d);++ch)
            if(!SugarSynth::valid(d,state.synthValues[SugarEngine::getSynthOffset(ePartId(p),id)+ch],ch)) return false;
    }
    return true;
}
struct Input {
    const char* bytes;
    size_t length,position=0;
    int read() { return position<length ? uint8_t(bytes[position++]) : -1; }
};
}
bool encode(const State& state,char* output,size_t capacity,size_t& length) {
    length=0;
    if(!validValues(state)) return false;
    BudgetAllocator allocator;
    JsonDocument doc(&allocator);
    doc["format"]="esp32-audio-sugar"; doc["version"]=formatVersion;
    doc["effects"]["slots"]=SugarFX::FX_SLOTS_COUNT;
    doc["effects"]["send"]=SugarFX::SharedSendEffect::key();
    auto bus=doc["effects"]["parameters"].to<JsonObject>();
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i)
        bus[SugarFX::SharedSendEffect::control(i)->key]=state.busValues[i];
    auto parts=doc["parts"].to<JsonArray>();
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        auto part=parts.add<JsonObject>();
        part["part"]=p+1; part["instrument"]=SugarEngine::getInstrumentKey(ePartId(p));
        part["active"]=bool(state.active&partBit(ePartId(p)));
        auto slots=part["fx"].to<JsonArray>();
        for(const auto& setting:state.fx[p]) {
            auto slot=slots.add<JsonObject>();
            slot["effect"]=SugarFX::InsertPool::key(setting.algorithm);
            auto controls=slot["parameters"].to<JsonObject>();
            for(unsigned i=0;i<SugarFX::InsertPool::controlCount(setting.algorithm);++i)
                controls[SugarFX::InsertPool::control(setting.algorithm,i)->key]=setting.values[i];
        }
        auto parameters=part["parameters"].to<JsonObject>();
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            const char* key=parameterKey(id);
            if(!key) return false;
            if(d->scope==PARAM_SCOPE_PART_CHANNEL) {
                auto values=parameters[key].to<JsonArray>();
                for(auto value:state.values[p][id]) values.add(value);
            } else if(id==PARAM_VOICE_MODE) parameters[key]=voiceModeKey(SugarAudio::eVoiceMode(state.values[p][id][0]));
            else if(id==PARAM_FX_INSERT) parameters[key]=insertKey(SugarFX::eInsertType(state.values[p][id][0]));
            else parameters[key]=state.values[p][id][0];
        }
        for(unsigned id=0;id<SugarEngine::getSynthParameterCount(ePartId(p));++id) {
            const auto* d=SugarEngine::getSynthParameter(ePartId(p),id);
            if(d->legacy) continue;
            const auto offset=SugarEngine::getSynthOffset(ePartId(p),id);
            if(d->scope==PARAM_SCOPE_PART_CHANNEL) {
                auto array=parameters[d->key].to<JsonArray>();
                for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) array.add(state.synthValues[offset+ch]);
            } else parameters[d->key]=state.synthValues[offset];
        }
    }
    auto routes=doc["routes"].to<JsonArray>();
    for(auto mask:state.routes) {
        auto route=routes.add<JsonArray>();
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) if(mask&partBit(ePartId(p))) route.add(p+1);
    }
    if(doc.overflowed()) return false;
    const size_t needed=measureJson(doc);
    if(needed>maxFileBytes || needed>=capacity) return false;
    length=serializeJson(doc,output,capacity);
    return length==needed;
}
bool decode(const char* input,size_t length,State& state) {
    decodeDetail="input size";
    if(!length || length>maxFileBytes) return false;
    BudgetAllocator allocator;
    JsonDocument doc(&allocator);
    Input reader{input,length};
    const auto error=deserializeJson(doc,reader,DeserializationOption::NestingLimit(6));
    if(error) { decodeDetail=error.c_str(); return false; }
    decodeDetail="trailing data";
    // ArduinoJson intentionally stops after one document. Reject trailing data.
    while(reader.position<length) {
        const char ch=input[reader.position++];
        if(ch!=' ' && ch!='\t' && ch!='\r' && ch!='\n') return false;
    }
    decodeDetail="root shape/format";
    if(!doc.is<JsonObjectConst>() || doc.size()!=(doc["version"].as<unsigned>()>=3 ? 5U : 4U) || !textEquals(doc["format"],"esp32-audio-sugar") ||
        !doc["version"].is<unsigned>() || (doc["version"].as<unsigned>()!=formatVersion && doc["version"].as<unsigned>()!=4 && doc["version"].as<unsigned>()!=3 && doc["version"].as<unsigned>()!=2 && doc["version"].as<unsigned>()!=1) ||
        !doc["parts"].is<JsonArrayConst>() || doc["parts"].size()!=SUGAR_PART_CAPACITY ||
        !doc["routes"].is<JsonArrayConst>() || doc["routes"].size()!=SugarParts::MIDI_CHANNELS_COUNT) return false;
    const unsigned version=doc["version"].as<unsigned>();
    decodeDetail="effect identities";
    if(version>=4) {
        const auto effects=doc["effects"];
        if(!effects.is<JsonObjectConst>() || effects.size()!=3 || !effects["slots"].is<unsigned>() ||
           effects["slots"].as<unsigned>()!=SugarFX::FX_SLOTS_COUNT ||
           !textEquals(effects["send"],SugarFX::SharedSendEffect::key()) ||
           !effects["parameters"].is<JsonObjectConst>() || effects["parameters"].size()!=SugarFX::SharedSendEffect::controlCount()) return false;
        for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) {
            auto value=effects["parameters"][SugarFX::SharedSendEffect::control(i)->key];
            if(!value.is<int32_t>()) return false;
            state.busValues[i]=value.as<int32_t>();
        }
    } else if(version==3) {
        if(!doc["effects"].is<JsonObjectConst>() || doc["effects"].size()!=2 ||
           !textEquals(doc["effects"]["insert"],"sugar.drive.v1") ||
           !textEquals(doc["effects"]["send"],SugarFX::SharedSendEffect::key()) || !SugarFX::legacyEffectsCompatible()) return false;
    } else if(version==2 && !SugarFX::legacyEffectsCompatible()) return false;
    state.active=0;
    const bool legacy=doc["version"].as<unsigned>()==1;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        decodeDetail="part identity/shape";
        JsonObjectConst part=doc["parts"][p].as<JsonObjectConst>();
        if(part.size()!=(version>=4 ? 5U : 4U) || !part["part"].is<unsigned>() || part["part"].as<unsigned>()!=p+1 ||
            !textEquals(part["instrument"],SugarEngine::getInstrumentKey(ePartId(p))) ||
            !part["active"].is<bool>() || !part["parameters"].is<JsonObjectConst>()) return false;
        if(part["active"].as<bool>()) state.active|=partBit(ePartId(p));
        auto parameters=part["parameters"].as<JsonObjectConst>();
        unsigned supported=0;
        for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
            const auto id=eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(ePartId(p),id);
            if(!d) continue;
            if(legacy && SugarFX::parameter(id)) {
                state.values[p][id][0]=d->defaultValue;
                continue;
            }
            ++supported;
            auto value=parameters[parameterKey(id)];
            const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            decodeDetail="channel shape";
            if(d->scope==PARAM_SCOPE_PART_CHANNEL && (!value.is<JsonArrayConst>() || value.size()!=count)) return false;
            for(unsigned ch=0;ch<count;++ch) {
                auto element=d->scope==PARAM_SCOPE_PART_CHANNEL ? value[ch] : value;
                int32_t number=0;
                if(id==PARAM_VOICE_MODE) {
                    decodeDetail="voice mode";
                    unsigned mode=0;
                    for(;mode<SugarAudio::VOICE_MODES_COUNT;++mode)
                        if(textEquals(element,voiceModeKey(SugarAudio::eVoiceMode(mode)))) break;
                    if(mode==SugarAudio::VOICE_MODES_COUNT) return false;
                    number=int32_t(mode);
                } else if(id==PARAM_FX_INSERT) {
                    unsigned type=0;
                    for(;type<SugarFX::INSERTS_COUNT;++type)
                        if(textEquals(element,version==2 && type==SugarFX::INSERT_EFFECT ? "drive" : insertKey(SugarFX::eInsertType(type)))) break;
                    if(type==SugarFX::INSERTS_COUNT) return false;
                    number=int32_t(type);
                } else {
                    decodeDetail="parameter type";
                    if(!element.is<int32_t>()) return false;
                    number=element.as<int32_t>();
                }
                decodeDetail="parameter range";
                if(!validDescriptor(d,id,number,ch)) return false;
                state.values[p][id][ch]=number;
            }
        }
        for(unsigned id=0;id<SugarEngine::getSynthParameterCount(ePartId(p));++id) {
            const auto* d=SugarEngine::getSynthParameter(ePartId(p),id);
            if(d->legacy) continue;
            const auto offset=SugarEngine::getSynthOffset(ePartId(p),id);
            const bool present=!parameters[d->key].isNull();
            if(present) ++supported;
            auto value=parameters[d->key];
            if(present && d->scope==PARAM_SCOPE_PART_CHANNEL && (!value.is<JsonArrayConst>() || value.size()!=SugarSynth::words(*d))) return false;
            for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) {
                auto element=d->scope==PARAM_SCOPE_PART_CHANNEL ? value[ch] : value;
                if(present && !element.is<int32_t>()) return false;
                const int32_t number=present ? element.as<int32_t>() : d->defaultValue;
                if(!SugarSynth::valid(d,number,ch)) return false;
                state.synthValues[offset+ch]=number;
            }
        }
        decodeDetail="parameter shape";
        if(parameters.size()!=supported) return false;
        if(version>=4) {
            decodeDetail="effect slots/controls";
            if(!part["fx"].is<JsonArrayConst>() || part["fx"].size()!=SugarFX::FX_SLOTS_COUNT) return false;
            for(unsigned index=0;index<SugarFX::FX_SLOTS_COUNT;++index) {
                auto slot=part["fx"][index];
                if(!slot.is<JsonObjectConst>() || slot.size()!=2 || !slot["parameters"].is<JsonObjectConst>()) return false;
                unsigned type=0;
                for(;type<SugarFX::ALGORITHMS_COUNT;++type)
                    if(textEquals(slot["effect"],SugarFX::InsertPool::key(type))) break;
                if(type==SugarFX::ALGORITHMS_COUNT || slot["parameters"].size()!=SugarFX::InsertPool::controlCount(type)) return false;
                auto& target=state.fx[p][index]; target=SugarFX::defaultSlot(SugarFX::eAlgorithm(type));
                for(unsigned id=0;id<SugarFX::InsertPool::controlCount(type);++id) {
                    auto value=slot["parameters"][SugarFX::InsertPool::control(type,id)->key];
                    if(!value.is<int32_t>()) return false;
                    target.values[id]=value.as<int32_t>();
                }
            }
        }
    }
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) {
        decodeDetail="routing shape/value";
        auto route=doc["routes"][ch];
        if(!route.is<JsonArrayConst>() || route.size()>SUGAR_PART_CAPACITY) return false;
        uint32_t mask=0;
        for(JsonVariantConst destination:route.as<JsonArrayConst>()) {
            if(!destination.is<unsigned>()) return false;
            const unsigned p=destination.as<unsigned>();
            if(p<1 || p>SUGAR_PART_CAPACITY || (mask&partBit(ePartId(p-1)))) return false;
            mask|=partBit(ePartId(p-1));
        }
        state.routes[ch]=mask;
    }
    if(version<4) migrateLegacyEffects(state);
    decodeDetail="snapshot values";
    return validValues(state);
}
bool codecChecks() {
    // Fixed test storage: never consume an 8 KiB loop-task stack with documents.
    static State original,restored;
    static char text[maxFileBytes+1];
    original.active=SugarParts::Routing<SUGAR_PART_CAPACITY>::availableMask;
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) original.routes[ch]=ch%2 ? 0 : original.active;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw) {
            const auto id=SugarParameters::eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(SugarParts::ePartId(p),id);
            if(!d) continue;
            const unsigned count=d->scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch) original.values[p][id][ch]=ch%2 ? d->minimum : d->maximum;
        }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned id=0;id<SugarEngine::getSynthParameterCount(ePartId(p));++id) {
        const auto* d=SugarEngine::getSynthParameter(ePartId(p),id);
        if(d->legacy) continue;
        for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) original.synthValues[SugarEngine::getSynthOffset(ePartId(p),id)+ch]=ch%2 ? d->minimum : d->maximum;
    }
    migrateLegacyEffects(original);
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned slot=1;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
            original.fx[p][slot]=SugarFX::defaultSlot(SugarFX::ALGORITHM_CRUSHER);
            for(unsigned i=0;i<SugarFX::InsertPool::controlCount(SugarFX::ALGORITHM_CRUSHER);++i)
                original.fx[p][slot].values[i]=SugarFX::InsertPool::control(SugarFX::ALGORITHM_CRUSHER,i)->maximum;
        }
    size_t length=0;
    checkDetail="encode";
    if(!encode(original,text,sizeof(text),length)) return false;
    checkDetail="roundtrip decode";
    if(!decode(text,length,restored)) return false;
    checkDetail="roundtrip compare";
    if(original.active!=restored.active) return false;
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) if(original.routes[ch]!=restored.routes[ch]) return false;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw)
            for(unsigned ch=0;ch<SugarParameters::MIDI_CHANNELS_COUNT;++ch)
                if(original.values[p][raw][ch]!=restored.values[p][raw][ch]) return false;
    for(unsigned i=0;i<SugarAudio::SynthLayout::nativeWords;++i) if(original.synthValues[i]!=restored.synthValues[i]) return false;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
        if(original.fx[p][slot].algorithm!=restored.fx[p][slot].algorithm) return false;
        for(unsigned i=0;i<SugarFX::InsertPool::maxControls;++i)
            if(original.fx[p][slot].values[i]!=restored.fx[p][slot].values[i]) return false;
    }
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) if(original.busValues[i]!=restored.busValues[i]) return false;
    checkDetail="truncation/size";
    if(decode(text,length-1,restored) || decode(text,maxFileBytes+1,restored)) return false;
    checkDetail="trailing data";
    text[length]='X';
    if(decode(text,length+1,restored)) return false;
    text[length]=0;
    checkDetail="version";
    char versionTag[24]; snprintf(versionTag,sizeof(versionTag),"\"version\":%u",formatVersion);
    char* version=strstr(text,versionTag);
    if(!version) return false;
    version[10]='9';
    if(decode(text,length,restored)) return false;
    version[10]=char('0'+formatVersion);
    checkDetail="instrument identity";
    char* instrument=strstr(text,SugarEngine::getInstrumentKey(SugarParts::PART_1));
    if(!instrument) return false;
    const char saved=instrument[0];
    instrument[0]=saved=='X' ? 'Y' : 'X';
    if(decode(text,length,restored)) return false;
    instrument[0]=saved;
    checkDetail="effect identity rejection";
    for(const char* key:{SugarFX::InsertPool::key(SugarFX::ALGORITHM_DRIVE),SugarFX::SharedSendEffect::key()}) {
        char* identity=strstr(text,key);
        if(!identity) return false;
        const char savedIdentity=*identity; *identity='X';
        if(decode(text,length,restored)) return false;
        *identity=savedIdentity;
    }
    {
        BudgetAllocator allocator;
        JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        document.remove("effects");
        size_t altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
        document["version"]=2;
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) document["parts"][p].remove("fx");
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
            if(original.values[p][PARAM_FX_INSERT][0]==SugarFX::INSERT_EFFECT)
                document["parts"][p]["parameters"][parameterKey(PARAM_FX_INSERT)]="drive";
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)!=SugarFX::legacyEffectsCompatible()) return false;
    }
    checkDetail="v3 migration";
    if(!encode(original,text,sizeof(text),length)) return false;
    {
        BudgetAllocator allocator; JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        document["version"]=3;
        document["effects"].clear();
        document["effects"]["insert"]="sugar.drive.v1";
        document["effects"]["send"]="sugar.delay.v1";
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) document["parts"][p].remove("fx");
        const auto altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)!=SugarFX::legacyEffectsCompatible()) return false;
        if(SugarFX::legacyEffectsCompatible()) for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
            for(unsigned slot=1;slot<SugarFX::FX_SLOTS_COUNT;++slot)
                if(restored.fx[p][slot].algorithm!=SugarFX::ALGORITHM_BYPASS) return false;
    }
    checkDetail="v4 migration";
    if(!encode(original,text,sizeof(text),length)) return false;
    {
        BudgetAllocator allocator; JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        document["version"]=4;
        const auto altered=serializeJson(document,text,sizeof(text));
        if(!decode(text,altered,restored)) return false;
    }
    checkDetail="native defaults/type/range/unknown key";
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned id=0;id<SugarEngine::getSynthParameterCount(ePartId(p));++id) {
        const auto* d=SugarEngine::getSynthParameter(ePartId(p),id);
        if(d->legacy) continue;
        if(!encode(original,text,sizeof(text),length)) return false;
        BudgetAllocator allocator; JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        auto parameters=document["parts"][p]["parameters"].as<JsonObject>();
        parameters.remove(d->key);
        auto altered=serializeJson(document,text,sizeof(text));
        if(!decode(text,altered,restored)) return false;
        for(unsigned ch=0;ch<SugarSynth::words(*d);++ch)
            if(restored.synthValues[SugarEngine::getSynthOffset(ePartId(p),id)+ch]!=d->defaultValue) return false;
        parameters[d->key]="invalid";
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
        parameters.remove(d->key);
        parameters["unknown_native_parameter"]=0;
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
        parameters.remove("unknown_native_parameter");
        if(d->scope==PARAM_SCOPE_PART_CHANNEL) {
            auto values=parameters[d->key].to<JsonArray>();
            for(unsigned ch=0;ch<SugarSynth::words(*d);++ch) values.add(d->defaultValue);
            if(d->maximum<INT32_MAX) values[0]=d->maximum+1;
            else if(d->minimum>INT32_MIN) values[0]=d->minimum-1;
            else values[0]=false;
        } else {
            if(d->maximum<INT32_MAX) parameters[d->key]=d->maximum+1;
            else if(d->minimum>INT32_MIN) parameters[d->key]=d->minimum-1;
            else parameters[d->key]=false;
        }
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
    }
    checkDetail="slot capacity/control schema";
    if(!encode(original,text,sizeof(text),length)) return false;
    {
        BudgetAllocator allocator; JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        document["effects"]["slots"]=SugarFX::FX_SLOTS_COUNT+1;
        size_t altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
        document["effects"]["slots"]=SugarFX::FX_SLOTS_COUNT;
        const int32_t drive=original.values[0][PARAM_FX_DRIVE][0];
        document["parts"][0]["parameters"][parameterKey(PARAM_FX_DRIVE)]=drive ? drive-1 : drive+1;
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false; // Valid scalar range, conflicting slot control.
        document["parts"][0]["parameters"][parameterKey(PARAM_FX_DRIVE)]=drive;
        document["parts"][0]["fx"][0]["parameters"].remove("amount");
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
    }
    if(SugarFX::FX_SLOTS_COUNT>1) for(unsigned id=0;id<SugarFX::CrusherEffect::controlCount();++id) {
        if(!encode(original,text,sizeof(text),length)) return false;
        BudgetAllocator allocator; JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        const auto* control=SugarFX::CrusherEffect::control(id);
        document["parts"][0]["fx"][1]["parameters"][control->key]=control->maximum+1;
        const auto altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false;
    }
    // Exercise a supported parameter without imposing gain/allocator support on
    // custom instruments. All edits below affect only the temporary JSON.
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw) {
            const auto id=SugarParameters::eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(SugarParts::ePartId(p),id);
            if(!d) continue;
            checkDetail=parameterKey(id);
            if(!encode(original,text,sizeof(text),length)) return false;
            BudgetAllocator allocator;
            JsonDocument document(&allocator);
            if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
            JsonVariant parameter=document["parts"][p]["parameters"][parameterKey(id)];
            if(id==SugarParameters::PARAM_VOICE_MODE) parameter.set("invalid_mode");
            else {
                JsonVariant element=d->scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL ? parameter[0].as<JsonVariant>() : parameter;
                if(d->maximum<INT32_MAX) element.set(d->maximum+1);
                else if(d->minimum>INT32_MIN) element.set(d->minimum-1);
                else element.set(false); // wrong type if the full integer range is valid
            }
            if(document.overflowed() || measureJson(document)>=sizeof(text)) return false;
            const size_t altered=serializeJson(document,text,sizeof(text));
            if(decode(text,altered,restored)) return false;
        }
    checkDetail="final roundtrip/depth";
    if(!encode(original,text,sizeof(text),length) || !decode(text,length,restored) ||
        decode("[[[[[[[0]]]]]]]",15,restored)) return false;
    checkDetail="v1 migration and strict FX schema";
    {
        BudgetAllocator allocator;
        JsonDocument document(&allocator);
        if(deserializeJson(document,static_cast<const char*>(text),length)) return false;
        document["parts"][0]["parameters"].remove(parameterKey(PARAM_FX_SEND));
        size_t altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false; // v2 requires every supported key.
        document["version"]=1;
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) document["parts"][p].remove("fx");
        document.remove("effects");
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
            for(unsigned raw=PARAM_PART_LEVEL;raw<PARAMETERS_COUNT;++raw)
                document["parts"][p]["parameters"].remove(parameterKey(eParameterId(raw)));
        if(document.overflowed() || measureJson(document)>=sizeof(text)) return false;
        altered=serializeJson(document,text,sizeof(text));
        if(!decode(text,altered,restored)) return false;
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
            for(unsigned raw=0;raw<PARAMETERS_COUNT;++raw) {
                const auto id=eParameterId(raw);
                const auto* d=SugarEngine::getParameterDescriptor(ePartId(p),id);
                if(!d) continue;
                const unsigned count=d->scope==PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
                for(unsigned ch=0;ch<count;++ch)
                    if(restored.values[p][id][ch]!=(SugarFX::parameter(id) ? d->defaultValue : original.values[p][id][ch])) return false;
            }
        document["parts"][0]["parameters"][parameterKey(PARAM_FX_SEND)]=0;
        altered=serializeJson(document,text,sizeof(text));
        if(decode(text,altered,restored)) return false; // v1 must contain only the original keys.
    }
    checkDetail="PASS";
    return true;
}
const char* codecCheckDetail() {
    static char detail[80];
    if(strcmp(checkDetail,"PASS")==0) return checkDetail;
    snprintf(detail,sizeof(detail),"%s: %s",checkDetail,decodeDetail);
    return detail;
}
}

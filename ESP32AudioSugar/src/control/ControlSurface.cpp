#include <Arduino.h>
#include <Wire.h>
#include "esp_log.h"
#include "../../Config.h"
#include "../../BoardConfig.h"
#include "../audio/InstrumentEngine.h"
#include "ControlLogic.h"
#include "ControlSurface.h"
#include "../presets/PresetStore.h"
#include "../tests/RoutingBoardChecks.h"
namespace SugarControl {
namespace {
using namespace SugarParameters;
using namespace SugarParts;
constexpr unsigned columns=21, rows=8, pageBytes=128, chunkBytes=16;
const int pins[BUTTONS_COUNT]={SugarBoard::previousPart,SugarBoard::nextPart,
    SugarBoard::nextField,SugarBoard::audition,SugarBoard::encoderSwitch};
// Project-owned five-column font with distinct upper/lowercase glyphs.
constexpr uint8_t letters[26][5]={
 {126,9,9,9,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {65,65,127,65,65},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{127,32,24,32,127},{99,20,8,20,99},
 {3,4,120,4,3},{97,81,73,69,67}};
constexpr uint8_t lowercase[26][5]={
 {32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},
 {56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},
 {0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},
 {124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},
 {8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},
 {60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},
 {12,80,80,80,60},{68,100,84,76,68}};
constexpr uint8_t digits[10][5]={
 {62,81,73,69,62},{0,66,127,64,0},{98,81,73,73,70},{34,65,73,73,54},
 {24,20,18,127,16},{39,69,69,69,57},{62,73,73,73,50},{1,113,9,5,3},
 {54,73,73,73,54},{38,73,73,73,62}};
char screen[rows][columns+1];
enum eTextScale : uint8_t { TEXT_NORMAL, TEXT_DOUBLE, TEXT_SCALES_COUNT };
eTextScale valueScale=TEXT_NORMAL;
bool spacedRows=true;
Button buttons[BUTTONS_COUNT];
Encoder encoder;
eInteraction interaction=INTERACTION_BROWSE;
unsigned part=0,channel=0,field=FIELD_HOME,parameterCount=0;
enum eControlEntry : uint8_t { ENTRY_SYNTH, ENTRY_LOCAL, ENTRY_FX_TYPE, ENTRY_FX_VALUE, ENTRY_BUS_VALUE, ENTRY_BUS_NAME, ENTRIES_COUNT };
struct ControlEntry { eControlEntry kind; unsigned id,slot; };
constexpr unsigned entryCapacity=SugarAudio::SynthLayout::maxCount+PARAMETERS_COUNT+SugarFX::FX_SLOTS_COUNT*(SugarFX::InsertPool::maxControls+1)+SugarFX::SharedSendEffect::controlCount()+1;
ControlEntry parameters[entryCapacity];
uint8_t address=0,transferPage=rows,transferColumn=0;
uint32_t lastFrame=0,lastScan=0,errors=0,rejected=0,maxTickUs=0;
bool playing=false;
uint32_t playingMask=0;
uint8_t playingChannel=0,playingKey=69;
ePage page=PAGE_MENU;
unsigned menuItem=MENU_SYNTH_FIRST;
unsigned routingField=ROUTING_FIELD_CHANNEL;
ePresetField presetField=PRESET_FIELD_SLOT;
SugarPresets::eSlot presetSlot=SugarPresets::SLOT_1;
bool confirming=false,slotEditing=false,longPress=false;
uint32_t encoderPressedAt=0;
ePartId selectedPart() { return ePartId(part); }
bool controlsBusy() { return SugarPresets::busy() || SugarEngine::presetBusy(); }
uint32_t currentRoute() {
    uint32_t mask=0;
    SugarEngine::getChannelRoute(uint8_t(channel),mask);
    return mask;
}
bool transmit(const uint8_t* bytes,unsigned count) {
    Wire.beginTransmission(address);
    Wire.write(bytes,count);
    if(Wire.endTransmission()==0) return true;
    ++errors; address=0; transferPage=rows;
    ESP_LOGE("SugarUI","OLED disconnected/transfer failed; controls remain enabled");
    return false;
}
void discover() {
    parameterCount=0;
    for(unsigned id=0;id<PARAMETERS_COUNT;++id) {
        const bool visible=page==PAGE_FX ? id==PARAM_FX_SEND || id==PARAM_DELAY_RETURN :
            page==PAGE_SYNTH && id==PARAM_PART_LEVEL;
        if(!visible) continue;
        if(SugarEngine::getParameterDescriptor(selectedPart(),eParameterId(id))) parameters[parameterCount++]={ENTRY_SYNTH,id,0};
    }
    if(page==PAGE_SYNTH) for(unsigned id=0;id<SugarEngine::getSynthParameterCount(selectedPart());++id) parameters[parameterCount++]={ENTRY_LOCAL,id,0};
    if(page==PAGE_FX) for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
        parameters[parameterCount++]={ENTRY_FX_TYPE,0,slot};
        for(unsigned id=0;id<SugarEngine::getFxControlCount(selectedPart(),slot);++id)
            parameters[parameterCount++]={ENTRY_FX_VALUE,id,slot};
    }
    if(page==PAGE_FX) parameters[parameterCount++]={ENTRY_BUS_NAME,0,0};
    if(page==PAGE_FX) for(unsigned id=0;id<SugarFX::SharedSendEffect::controlCount();++id)
        parameters[parameterCount++]={ENTRY_BUS_VALUE,id,0};
    if(page==PAGE_FX && selectedPart()!=PART_1) parameters[parameterCount++]={ENTRY_SYNTH,PARAM_DELAY_RETURN,0};
    if(field>=FIELDS_FIXED_COUNT+parameterCount) field=FIELD_CONTEXT;
}
void stopNote() {
    if(!playing) return;
    const SugarMidi::Event event{SugarMidi::EVENT_NOTE_OFF,playingChannel,playingKey,0};
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
        if((playingMask&partBit(ePartId(p))) && !SugarEngine::enqueueToPart(ePartId(p),event)) ++rejected;
    playing=false; // Queue overflow uses the existing engine-wide panic recovery.
}
void selectPart(int delta) {
    stopNote(); part=wrap(part,delta,SUGAR_PART_CAPACITY); discover();
}
void home() {
    stopNote(); page=PAGE_MENU; interaction=INTERACTION_BROWSE;
    confirming=false; slotEditing=false;
}
void openMenuItem() {
    stopNote(); page=menuPage(menuItem,SUGAR_PART_CAPACITY); field=FIELD_CONTEXT;
    routingField=ROUTING_FIELD_CHANNEL; presetField=PRESET_FIELD_SLOT;
    interaction=INTERACTION_BROWSE; confirming=false; slotEditing=false;
    if(page==PAGE_SYNTH) part=menuItem-MENU_SYNTH_FIRST;
    discover();
}
void nextField(int delta) {
    const auto count=FIELDS_FIXED_COUNT+parameterCount;
    for(unsigned tries=0;tries<count;++tries) {
        field=wrap(field,delta,count);
        if(field!=FIELD_HOME && (field<FIELD_PARAMETER_FIRST || parameters[field-FIELD_PARAMETER_FIRST].kind!=ENTRY_BUS_NAME)) return;
    }
}
void rotate(int delta) {
    if(controlsBusy()) return;
    if(page==PAGE_MENU) { menuItem=wrap(menuItem,delta,menuItemCount(SUGAR_PART_CAPACITY)); return; }
    if(page==PAGE_ROUTING) {
        if(interaction==INTERACTION_BROWSE) { routingField=wrap(routingField,delta,routingFieldCount(SUGAR_PART_CAPACITY)); return; }
        const auto action=routingAction(routingField,SUGAR_PART_CAPACITY);
        if(action==ROUTING_FIELD_CHANNEL) { stopNote(); channel=wrap(channel,delta,SugarParameters::MIDI_CHANNELS_COUNT); }
        else if(action==ROUTING_FIELD_PART) {
            const auto mask=currentRoute(),next=editedRoute(mask,routingPart(routingField,SUGAR_PART_CAPACITY),delta>0);
            if(next!=mask) {
                stopNote();
                if(!SugarEngine::setChannelRoute(uint8_t(channel),next)) ++rejected;
            }
        }
        return;
    }
    if(page==PAGE_PRESETS) {
        confirming=false;
        if(slotEditing && presetField==PRESET_FIELD_STORAGE) {
            const auto next=SugarPresets::eStorage(wrap(SugarPresets::selectedStorage(),delta,SugarPresets::STORAGES_COUNT));
            if(!SugarPresets::selectStorage(next)) ++rejected;
        } else if(slotEditing) presetSlot=SugarPresets::eSlot(wrap(presetSlot,delta,SugarPresets::SLOTS_COUNT));
        else presetField=ePresetField(wrap(presetField,delta,PRESET_FIELDS_COUNT));
        return;
    }
    if(interaction==INTERACTION_BROWSE) { nextField(delta); return; }
    if(field==FIELD_HOME) return;
    if(field==FIELD_CONTEXT) {
        if(page==PAGE_FX) { selectPart(delta); return; }
        stopNote();
        if(!SugarEngine::setPartActive(selectedPart(),delta>0)) ++rejected;
        return;
    }
    const auto entry=parameters[field-FIELD_PARAMETER_FIRST];
    if(entry.kind==ENTRY_LOCAL) {
        const auto* p=SugarEngine::getSynthParameter(selectedPart(),entry.id);
        const unsigned ch=p->scope==PARAM_SCOPE_PART_CHANNEL ? channel : 0;
        int32_t value=0;
        if(SugarEngine::getSynthValue(selectedPart(),entry.id,value,ch) &&
           !SugarEngine::setSynthParameter(selectedPart(),entry.id,adjusted(value,delta,p->step,p->minimum,p->maximum),uint8_t(ch))) ++rejected;
        return;
    }
    if(entry.kind==ENTRY_BUS_NAME) return;
    if(entry.kind==ENTRY_FX_TYPE) {
        SugarFX::eAlgorithm type;
        if(!SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,type)) return;
        if(!SugarEngine::selectFx(selectedPart(),entry.slot,SugarFX::eAlgorithm(wrap(type,delta,SugarFX::ALGORITHMS_COUNT)))) ++rejected;
        discover(); return;
    }
    if(entry.kind==ENTRY_FX_VALUE || entry.kind==ENTRY_BUS_VALUE) {
        const bool bus=entry.kind==ENTRY_BUS_VALUE;
        const auto* p=bus ? SugarFX::SharedSendEffect::control(entry.id) : SugarEngine::getFxControl(selectedPart(),entry.slot,entry.id);
        int32_t value=0;
        if(!p || !(bus ? SugarEngine::getFxBusValue(entry.id,value) : SugarEngine::getFxValue(selectedPart(),entry.slot,entry.id,value))) return;
        value=adjusted(value,delta,p->step,p->minimum,p->maximum);
        if(!(bus ? SugarEngine::setFxBusControl(entry.id,value) : SugarEngine::setFxControl(selectedPart(),entry.slot,entry.id,value))) ++rejected;
        return;
    }
    const auto id=eParameterId(entry.id);
    const auto owner=id==PARAM_DELAY_RETURN ? PART_1 : selectedPart();
    const auto* d=SugarEngine::getParameterDescriptor(owner,id);
    const unsigned ch=d->scope==PARAM_SCOPE_PART_CHANNEL ? channel : 0;
    int32_t value=0;
    if(!SugarEngine::getRequestedParameter(owner,id,value,ch)) return;
    const int32_t step=id==PARAM_PORTAMENTO_MS || id==PARAM_DECAY_MS || id==PARAM_DELAY_TIME_MS ? 10 : 1;
    if(id==PARAM_PART_LEVEL || id==PARAM_FX_SEND || id==PARAM_DELAY_RETURN)
        value=gainFromPercent(adjusted(percent(value),delta,1,0,100));
    else value=adjusted(value,delta,step,d->minimum,d->maximum);
    if(!SugarEngine::setParameter(owner,id,value,ch)) ++rejected;
}
void press(eButton button,bool down) {
    if(button==BUTTON_ENCODER) {
        if(down) { encoderPressedAt=millis(); longPress=false; return; }
        if(longPress || controlsBusy()) return;
        if(page==PAGE_MENU) { openMenuItem(); return; }
        if(page==PAGE_ROUTING) {
            if(routingAction(routingField,SUGAR_PART_CAPACITY)==ROUTING_FIELD_BACK) {
                home();
            } else interaction=interaction==INTERACTION_BROWSE ? INTERACTION_EDIT : INTERACTION_BROWSE;
            return;
        }
        if(page==PAGE_SYNTH || page==PAGE_FX) {
            if(field==FIELD_HOME) { home(); return; }
            if(field>=FIELD_PARAMETER_FIRST && parameters[field-FIELD_PARAMETER_FIRST].kind==ENTRY_BUS_NAME) return;
            interaction=interaction==INTERACTION_BROWSE ? INTERACTION_EDIT : INTERACTION_BROWSE;
            return;
        }
        if(presetField==PRESET_FIELD_SLOT || presetField==PRESET_FIELD_STORAGE) { slotEditing=!slotEditing; return; }
        if(presetField==PRESET_FIELD_BACK) { home(); return; }
        if(!confirming) { confirming=true; return; }
        SugarPresets::eCommand command=SugarPresets::COMMAND_NONE;
        switch(presetField) {
        case PRESET_FIELD_SAVE: command=SugarPresets::COMMAND_SAVE; break;
        case PRESET_FIELD_LOAD: command=SugarPresets::COMMAND_LOAD; break;
        case PRESET_FIELD_MEDIA_ACTION: command=SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? SugarPresets::COMMAND_FORMAT : SugarPresets::COMMAND_REMOUNT; break;
        default: break;
        }
        stopNote();
        if(!SugarPresets::request(command,presetSlot)) ++rejected;
        confirming=false;
        return;
    }
    if(down && !controlsBusy() && button==BUTTON_BACK) {
        if(interaction==INTERACTION_EDIT || slotEditing || confirming) {
            interaction=INTERACTION_BROWSE; slotEditing=false; confirming=false;
        } else home();
        return;
    }
    if(down && !controlsBusy() && button==BUTTON_NEXT_PAGE && page!=PAGE_MENU) {
        home(); menuItem=wrap(menuItem,1,menuItemCount(SUGAR_PART_CAPACITY)); openMenuItem();
        return;
    }
    if(page==PAGE_MENU) {
        if(button==BUTTON_AUDITION && !down) stopNote();
        if(down && !controlsBusy()) {
            if(button==BUTTON_NEXT_PAGE || button==BUTTON_NEXT_FIELD) rotate(1);
            else if(button==BUTTON_BACK) rotate(-1);
        }
        return;
    }
    if(page==PAGE_PRESETS || controlsBusy()) {
        if(button==BUTTON_AUDITION && !down) stopNote();
        if(down && button==BUTTON_NEXT_FIELD && !controlsBusy()) {
            confirming=false; slotEditing=false;
            presetField=ePresetField(wrap(presetField,1,PRESET_FIELDS_COUNT));
        }
        return;
    }
    if(page==PAGE_ROUTING) {
        if(button==BUTTON_AUDITION) {
            if(!down) { stopNote(); return; }
            playingMask=currentRoute(); playingChannel=uint8_t(channel); playingKey=36;
            bool active=false;
            for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
                if((playingMask&partBit(ePartId(p))) && SugarEngine::isPartActive(ePartId(p))) active=true;
            if(!active) return;
            playing=SugarEngine::enqueue({SugarMidi::EVENT_NOTE_ON,playingChannel,playingKey,100});
            if(!playing) ++rejected;
        } else if(down) {
            if(button==BUTTON_NEXT_FIELD) routingField=wrap(routingField,1,routingFieldCount(SUGAR_PART_CAPACITY));
            interaction=INTERACTION_BROWSE;
        }
        return;
    }
    if(button==BUTTON_AUDITION) {
        if(!down) { stopNote(); return; }
        if(!SugarEngine::isPartActive(selectedPart())) return;
        playingMask=partBit(selectedPart()); playingChannel=uint8_t(channel); playingKey=69;
        const SugarMidi::Event event{SugarMidi::EVENT_NOTE_ON,playingChannel,69,100};
        playing=SugarEngine::enqueueToPart(selectedPart(),event);
        if(!playing) ++rejected;
        return;
    }
    if(!down) return;
    switch(button) {
    case BUTTON_BACK: home(); menuItem=wrap(menuItem,-1,menuItemCount(SUGAR_PART_CAPACITY)); break;
    case BUTTON_NEXT_PAGE: home(); menuItem=wrap(menuItem,1,menuItemCount(SUGAR_PART_CAPACITY)); break;
    case BUTTON_NEXT_FIELD: interaction=INTERACTION_BROWSE; nextField(1); break;
    default: break;
    }
}
uint32_t noticedRejection=0,rejectionAt=0;
void footer(const char* hint) {
    const uint32_t now=millis();
    if(noticedRejection!=rejected) { noticedRejection=rejected; rejectionAt=now; }
    const char* message=controlsBusy() ? "Please wait..." :
        noticedRejection && uint32_t(now-rejectionAt)<2500 ? "Busy - try again" : hint;
    snprintf(screen[7],sizeof(screen[7]),"%.21s",message);
    // Preserve MIDI/SD acronyms in UI-owned copy; store statuses are plain prose.
    if(message==SugarPresets::statusText()) {
        for(unsigned i=1;screen[7][i];++i) if(screen[7][i]>='A' && screen[7][i]<='Z') screen[7][i]+=32;
    }
}
bool composeEditor() {
    if(interaction!=INTERACTION_EDIT || (page!=PAGE_SYNTH && page!=PAGE_FX)) return false;
    char label[43]={},display[32]={};
    int32_t value=0,audio=0,low=0,high=0;
    bool numeric=false,pending=false;
    snprintf(screen[0],sizeof(screen[0]),"%.21s",SugarEngine::getInstrumentName(selectedPart()));
    if(field==FIELD_CONTEXT) {
        snprintf(label,sizeof(label),"%s",page==PAGE_FX ? "Sound to process" : "Sound enabled");
        if(page==PAGE_FX) snprintf(display,sizeof(display),"%.31s",SugarEngine::getInstrumentName(selectedPart()));
        else snprintf(display,sizeof(display),"%s",SugarEngine::isPartActive(selectedPart()) ? "On" : "Off");
    } else if(field>=FIELD_PARAMETER_FIRST && field-FIELD_PARAMETER_FIRST<parameterCount) {
        const auto entry=parameters[field-FIELD_PARAMETER_FIRST];
        if(entry.kind==ENTRY_LOCAL) {
            const auto* p=SugarEngine::getSynthParameter(selectedPart(),entry.id);
            const auto ch=p->scope==PARAM_SCOPE_PART_CHANNEL ? channel : 0;
            SugarEngine::getSynthValue(selectedPart(),entry.id,value,ch);
            SugarEngine::getSynthValue(selectedPart(),entry.id,audio,ch,true);
            snprintf(label,sizeof(label),"%.42s",p->name);
            if(p->scope==PARAM_SCOPE_PART_CHANNEL) snprintf(screen[2],sizeof(screen[2]),"MIDI channel %u",channel+1);
            if(p->choices && value>=0 && unsigned(value)<p->choiceCount) snprintf(display,sizeof(display),"%.31s",p->choices[value]);
            else { numeric=true; snprintf(display,sizeof(display),"%ld%.8s",long(value),p->unit); }
            low=p->minimum; high=p->maximum; pending=value!=audio;
        } else if(entry.kind==ENTRY_SYNTH) {
            const auto id=eParameterId(entry.id);
            const auto owner=id==PARAM_DELAY_RETURN ? PART_1 : selectedPart();
            SugarEngine::getRequestedParameter(owner,id,value);
            SugarEngine::getAppliedParameter(owner,id,audio);
            snprintf(label,sizeof(label),"%s",id==PARAM_PART_LEVEL ? "Volume" : id==PARAM_FX_SEND ? "Delay send" : "Shared delay level");
            pending=value!=audio; numeric=true; low=0; high=100; value=percent(value);
            snprintf(display,sizeof(display),"%ld%%",long(value));
        } else if(entry.kind==ENTRY_FX_TYPE) {
            SugarFX::eAlgorithm type=SugarFX::ALGORITHM_BYPASS,appliedType=type;
            SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,type);
            SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,appliedType,true);
            snprintf(label,sizeof(label),"Insert %u effect",entry.slot+1);
            snprintf(display,sizeof(display),"%.31s",type==SugarFX::ALGORITHM_BYPASS ? "Off" : SugarEngine::getFxName(type));
            pending=type!=appliedType;
        } else if(entry.kind==ENTRY_FX_VALUE || entry.kind==ENTRY_BUS_VALUE) {
            const bool bus=entry.kind==ENTRY_BUS_VALUE;
            const auto* p=bus ? SugarFX::SharedSendEffect::control(entry.id) : SugarEngine::getFxControl(selectedPart(),entry.slot,entry.id);
            if(!p) return false;
            if(bus) {
                SugarEngine::getFxBusValue(entry.id,value); SugarEngine::getFxBusValue(entry.id,audio,true);
                snprintf(screen[0],sizeof(screen[0]),"Shared %.13s",SugarFX::SharedSendEffect::name());
            } else {
                SugarFX::eAlgorithm type=SugarFX::ALGORITHM_BYPASS;
                SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,type);
                SugarEngine::getFxValue(selectedPart(),entry.slot,entry.id,value);
                SugarEngine::getFxValue(selectedPart(),entry.slot,entry.id,audio,true);
                snprintf(screen[0],sizeof(screen[0]),"Insert%u %.12s",entry.slot+1,SugarEngine::getFxName(type));
            }
            snprintf(label,sizeof(label),"%.42s",p->name);
            if(p->kind==SugarFX::CONTROL_CHOICE && value>=0 && unsigned(value)<p->choiceCount) snprintf(display,sizeof(display),"%.31s",p->choices[value]);
            else { numeric=true; snprintf(display,sizeof(display),"%ld%.8s",long(value),p->unit); }
            low=p->minimum; high=p->maximum; pending=value!=audio;
        } else return false;
    } else return false;
    snprintf(screen[1],sizeof(screen[1]),"%.21s",label);
    if(strlen(label)>columns) snprintf(screen[2],sizeof(screen[2]),"%.21s",label+columns);
    const unsigned length=strlen(display);
    if(length<=columns/2) {
        valueScale=TEXT_DOUBLE;
        const unsigned padding=(columns/2-length)/2;
        memset(screen[3],' ',padding); snprintf(screen[3]+padding,sizeof(screen[3])-padding,"%.10s",display);
    } else snprintf(screen[3],sizeof(screen[3]),"%.21s",display);
    if(numeric) {
        const unsigned fill=rangePosition(value,low,high,columns-2);
        screen[5][0]=screen[5][columns-1]='|';
        for(unsigned i=0;i<columns-2;++i) screen[5][i+1]=i<fill ? '=' : '-';
        char limits[32]; snprintf(limits,sizeof(limits),"%ld to %ld",long(low),long(high));
        snprintf(screen[6],sizeof(screen[6]),"%.21s",strlen(limits)<=columns ? limits : "Full integer range");
    } else snprintf(screen[5],sizeof(screen[5]),"Turn to choose");
    footer(pending ? "Applying..." : "Click:Done Back:List");
    return true;
}
void compose() {
    if(page==PAGE_SYNTH || page==PAGE_FX) discover();
    memset(screen,0,sizeof(screen)); valueScale=TEXT_NORMAL;
    spacedRows=!(interaction==INTERACTION_EDIT && (page==PAGE_SYNTH || page==PAGE_FX));
    if(composeEditor()) return;
    if(page==PAGE_MENU) {
        const unsigned first=menuItem>=rows-3 ? menuItem-(rows-3)+1 : 0;
        snprintf(screen[0],sizeof(screen[0]),"Sugar");
        for(unsigned item=first;item<menuItemCount(SUGAR_PART_CAPACITY) && item-first<rows-3;++item) {
            const char selected=item==menuItem ? '>' : ' ';
            if(menuPage(item,SUGAR_PART_CAPACITY)==PAGE_SYNTH) snprintf(screen[item-first+1],sizeof(screen[item-first+1]),"%c%u %.17s",selected,item-MENU_SYNTH_FIRST+1,SugarEngine::getInstrumentName(ePartId(item-MENU_SYNTH_FIRST)));
            else snprintf(screen[item-first+1],sizeof(screen[item-first+1]),"%c%s",selected,item==MENU_MIDI ? "MIDI routing" : item==MENU_FX ? "Effects" : "Presets");
        }
        footer("Turn / click to open");
        return;
    }
    if(page==PAGE_ROUTING) {
        static_assert(PARTS_COUNT<=rows-4,"Routing page needs scrolling for more compiled parts");
        const uint32_t mask=currentRoute();
        bool inactive=false;
        snprintf(screen[0],sizeof(screen[0]),"MIDI routing");
        snprintf(screen[1],sizeof(screen[1]),"%cChannel %u",routingAction(routingField,SUGAR_PART_CAPACITY)==ROUTING_FIELD_CHANNEL ? '>' : ' ',channel+1);
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
            const auto id=ePartId(p);
            const bool enabled=bool(mask&partBit(id)),active=SugarEngine::isPartActive(id);
            inactive|=enabled && !active;
            snprintf(screen[p+2],sizeof(screen[p+2]),"%cP%u %.11s %s%s",routingPart(routingField,SUGAR_PART_CAPACITY)==id ? '>' : ' ',p+1,
                SugarEngine::getInstrumentName(id),enabled ? "On" : "Off",active ? "" : "*");
        }

        footer(interaction==INTERACTION_EDIT ? "Turn / click to finish" :
            routingAction(routingField,SUGAR_PART_CAPACITY)==ROUTING_FIELD_BACK ? ">Back" :
            !mask ? "No sound: muted" : inactive ? "Routed sound is off" : "Click:Edit Hold:Home");
        return;
    }
    if(page==PAGE_PRESETS) {
        if(confirming) {
            snprintf(screen[0],sizeof(screen[0]),"%s",presetField==PRESET_FIELD_SAVE ? "Save preset?" : presetField==PRESET_FIELD_LOAD ? "Load preset?" : SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? "Erase all files?" : "Reconnect SD card?");
            snprintf(screen[2],sizeof(screen[2]),"%s",SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? "Internal storage" : "SD card");
            if(presetField==PRESET_FIELD_SAVE || presetField==PRESET_FIELD_LOAD) snprintf(screen[3],sizeof(screen[3]),"Preset slot %u",unsigned(presetSlot)+1);
            else if(SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH) snprintf(screen[3],sizeof(screen[3]),"All files are erased");
            snprintf(screen[5],sizeof(screen[5]),"Click to confirm");
            snprintf(screen[6],sizeof(screen[6]),"Turn or Back:Cancel");
            footer("Turn or Back:Cancel");
            return;
        }
        snprintf(screen[0],sizeof(screen[0]),"Presets");
        snprintf(screen[1],sizeof(screen[1]),"%c%s %cSlot %u",presetField==PRESET_FIELD_STORAGE ? '>' : ' ',SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? "Internal" : "SD card",presetField==PRESET_FIELD_SLOT ? '>' : ' ',unsigned(presetSlot)+1);
        snprintf(screen[2],sizeof(screen[2]),"%cSave slot",presetField==PRESET_FIELD_SAVE ? '>' : ' ');
        snprintf(screen[3],sizeof(screen[3]),"%cLoad slot",presetField==PRESET_FIELD_LOAD ? '>' : ' ');
        snprintf(screen[4],sizeof(screen[4]),"%c%s",presetField==PRESET_FIELD_MEDIA_ACTION ? '>' : ' ',SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? "Erase internal files" : "Reconnect SD card");
        snprintf(screen[5],sizeof(screen[5]),"%cBack",presetField==PRESET_FIELD_BACK ? '>' : ' ');

        footer(confirming ? (presetField==PRESET_FIELD_MEDIA_ACTION && SugarPresets::selectedStorage()==SugarPresets::STORAGE_FLASH ? "Erase all? Click yes" : "Click yes / turn no") : slotEditing ? "Turn / click to finish" : SugarPresets::statusText());
        return;
    }
    snprintf(screen[0],sizeof(screen[0]),"%c%u %.17s",field==FIELD_HOME ? '>' : ' ',part+1,SugarEngine::getInstrumentName(selectedPart()));
    snprintf(screen[1],sizeof(screen[1]),"%cSound %s",field==FIELD_CONTEXT ? '>' : ' ',SugarEngine::isPartActive(selectedPart()) ? "On" : "Off");
    if(page==PAGE_FX) {
        snprintf(screen[0],sizeof(screen[0]),"%cEffects",field==FIELD_HOME ? '>' : ' ');
        snprintf(screen[1],sizeof(screen[1]),"%cFor %.16s",field==FIELD_CONTEXT ? '>' : ' ',SugarEngine::getInstrumentName(selectedPart()));
    }
    const unsigned first=parameterWindow(field,rows-4);
    for(unsigned row=0;row<rows-4 && first+row<parameterCount;++row) {
        const unsigned i=first+row;
        const auto entry=parameters[i];
        if(entry.kind==ENTRY_LOCAL) {
            const auto* p=SugarEngine::getSynthParameter(selectedPart(),entry.id);
            const unsigned ch=p->scope==PARAM_SCOPE_PART_CHANNEL ? channel : 0;
            int32_t value=0,audio=0;
            SugarEngine::getSynthValue(selectedPart(),entry.id,value,ch);
            SugarEngine::getSynthValue(selectedPart(),entry.id,audio,ch,true);
            char display[16];
            if(p->choices && audio>=0 && unsigned(audio)<p->choiceCount) snprintf(display,sizeof(display),"%.15s",p->choices[audio]);
            else if(p->scope==PARAM_SCOPE_PART_CHANNEL) snprintf(display,sizeof(display),"%ld Ch%u",long(audio),ch+1);
            else snprintf(display,sizeof(display),"%ld%.4s",long(audio),p->unit);
            snprintf(screen[row+2],sizeof(screen[row+2]),"%c%-11.11s %7.7s%s",field==FIELD_PARAMETER_FIRST+i ? '>' : ' ',p->name,display,value!=audio ? "*" : "");
            continue;
        }
        if(entry.kind!=ENTRY_SYNTH) {
            const char selected=field==FIELD_PARAMETER_FIRST+i ? '>' : ' ';
            if(entry.kind==ENTRY_BUS_NAME) {
                snprintf(screen[row+2],sizeof(screen[row+2]),"%cShared %.13s",selected,SugarFX::SharedSendEffect::name());
                continue;
            }
            if(entry.kind==ENTRY_FX_TYPE) {
                SugarFX::eAlgorithm type=SugarFX::ALGORITHM_BYPASS,audio=type;
                SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,type);
                SugarEngine::getFxAlgorithm(selectedPart(),entry.slot,audio,true);
                snprintf(screen[row+2],sizeof(screen[row+2]),"%cInsert %u %.9s%s",selected,entry.slot+1,type==SugarFX::ALGORITHM_BYPASS ? "Off" : SugarEngine::getFxName(type),type!=audio ? "*" : "");
            } else {
                const bool bus=entry.kind==ENTRY_BUS_VALUE;
                const auto* p=bus ? SugarFX::SharedSendEffect::control(entry.id) : SugarEngine::getFxControl(selectedPart(),entry.slot,entry.id);
                int32_t value=0,audio=0;
                if(bus) { SugarEngine::getFxBusValue(entry.id,value); SugarEngine::getFxBusValue(entry.id,audio,true); }
                else { SugarEngine::getFxValue(selectedPart(),entry.slot,entry.id,value); SugarEngine::getFxValue(selectedPart(),entry.slot,entry.id,audio,true); }
                const char* choice=p && p->kind==SugarFX::CONTROL_CHOICE && value>=0 && unsigned(value)<p->choiceCount ? p->choices[value] : nullptr;
                char display[14];
                if(choice) snprintf(display,sizeof(display),"%.13s",choice);
                else snprintf(display,sizeof(display),"%ld%s",long(value),p ? p->unit : "");
                if(bus) snprintf(screen[row+2],sizeof(screen[row+2]),"%c%-11.11s %7.7s%s",selected,p ? p->name : "?",display,value!=audio ? "*" : "");
                else snprintf(screen[row+2],sizeof(screen[row+2]),"%c%u %-9.9s %7.7s%s",selected,entry.slot+1,p ? p->name : "?",display,value!=audio ? "*" : "");
            }
            continue;
        }
        const auto id=eParameterId(entry.id);
        const auto owner=id==PARAM_DELAY_RETURN ? PART_1 : selectedPart();
        const auto* d=SugarEngine::getParameterDescriptor(owner,id);
        const unsigned ch=d->scope==PARAM_SCOPE_PART_CHANNEL ? channel : 0;
        int32_t requested=0,applied=0;
        SugarEngine::getRequestedParameter(owner,id,requested,ch);
        SugarEngine::getAppliedParameter(owner,id,applied,ch);
        const char* choice=SugarEngine::getParameterChoiceName(id,unsigned(applied));
        char value[16];
        if(choice) snprintf(value,sizeof(value),"%s",choice);
        else if(id==PARAM_PART_LEVEL || id==PARAM_FX_SEND || id==PARAM_DELAY_RETURN) snprintf(value,sizeof(value),"%ld%%",long(percent(applied)));
        else if(id==PARAM_GAIN) snprintf(value,sizeof(value),"%ld Ch%u",long(applied),channel+1);
        else snprintf(value,sizeof(value),"%ld%s",long(applied),d->unit);
        // Compact labels keep values visible in 21 columns.
        const char* label=id==PARAM_PART_LEVEL ? "Volume" : id==PARAM_FX_SEND ? "Delay send" : id==PARAM_DELAY_RETURN ? "Delay level" : id==PARAM_VOICE_MODE ? "Mode" : id==PARAM_PORTAMENTO_MS ? "Glide" : id==PARAM_GAIN ? "Gain" : id==PARAM_PULSE_WIDTH ? "Width" : d->name;
        snprintf(screen[row+2],sizeof(screen[row+2]),"%c%-11.11s %7.7s%s",field==FIELD_PARAMETER_FIRST+i ? '>' : ' ',label,value,requested!=applied ? "*" : "");
    }
    int32_t send=0,level=0;
    if(page==PAGE_FX) {
        SugarEngine::getRequestedParameter(selectedPart(),PARAM_FX_SEND,send);
        SugarEngine::getRequestedParameter(PART_1,PARAM_DELAY_RETURN,level);
    }
    footer(page==PAGE_FX && !send ? "Delay: send is zero" : page==PAGE_FX && !level ? "Delay: level is zero" :
        playing ? "Playing / release" : "Click:Edit Hold:Home");
}
uint8_t glyph(char ch,unsigned column) {
    if(column==5) return 0;
    if(ch>='a' && ch<='z') return lowercase[ch-'a'][column];
    if(ch>='A' && ch<='Z') return letters[ch-'A'][column];
    if(ch>='0' && ch<='9') return digits[ch-'0'][column];
    if(ch=='>') return column==1 ? 65 : column==2 ? 34 : column==3 ? 20 : column==4 ? 8 : 0;
    if(ch=='*') return column==2 ? 28 : 8;
    if(ch=='%') return column==0||column==4 ? 99 : uint8_t(32>>(column-1));
    if(ch==':') return column==2 ? 36 : 0;
    if(ch=='.') return column==2 ? 64 : 0;
    if(ch=='-') return 8;
    if(ch=='=') return 20;
    if(ch=='|') return column==2 ? 127 : 0;
    if(ch=='/') return uint8_t(64>>column);
    if(ch=='?') { constexpr uint8_t question[]={2,1,81,9,6}; return question[column]; }
    return 0;
}
void displayTick(uint32_t now) {
    if(!address) return;
    if(transferPage==rows) {
        if(uint32_t(now-lastFrame)<100) return;
        lastFrame=now; compose(); transferPage=0; transferColumn=0;
    }
    // One bounded I2C transaction per loop: page address OR 16 pixel columns.
    if(transferColumn==0) {
        const uint8_t commands[]={0x00,uint8_t(0xB0|transferPage),0x02,0x10};
        if(transmit(commands,sizeof(commands))) transferColumn=1;
        return;
    }
    const unsigned offset=transferColumn-1;
    uint8_t bytes[chunkBytes+1]; bytes[0]=0x40;
    for(unsigned i=0;i<chunkBytes;++i) {
        const unsigned x=offset+i;
        uint8_t pixels=0;
        if(spacedRows) {
            const unsigned y=transferPage*8,first=y/textLinePitch;
            for(unsigned row=first;row<first+2 && row<textBodyRows;++row) {
                const auto ch=x/6<columns ? screen[row][x/6] : ' ';
                uint8_t source=glyph(ch,x%6);
                if(screen[row][0]=='>') {
                    source^=127;
                    pixels|=selectionPadding(int(row*textLinePitch)-int(y));
                }
                pixels|=placeGlyph(source,int(row*textLinePitch)-int(y));
            }
            uint8_t source=x/6<columns ? glyph(screen[7][x/6],x%6) : 0;
            if(screen[7][0]=='>') {
                source^=127;
                pixels|=selectionPadding(int(textFooterY)-int(y));
            }
            pixels|=placeGlyph(source,int(textFooterY)-int(y));
        } else if(valueScale==TEXT_DOUBLE && (transferPage==3 || transferPage==4)) {
            const uint8_t source=x/12<columns ? glyph(screen[3][x/12],(x%12)/2) : 0;
            pixels=doubleGlyphHalf(source,transferPage==4);
        } else pixels=x/6<columns ? glyph(screen[transferPage][x/6],x%6) : 0;
        bytes[i+1]=!spacedRows && screen[transferPage][0]=='>' ? uint8_t(~pixels) : pixels;
    }
    if(!transmit(bytes,sizeof(bytes))) return;
    if(offset+chunkBytes==pageBytes) { ++transferPage; transferColumn=0; }
    else transferColumn+=chunkBytes;
}
}
#if SUGAR_ROUTING_BOARD_CHECKS
namespace Testing {
void click(eButton button) { press(button,true); press(button,false); }
void button(eButton button,bool down) { press(button,down); }
void turn(int delta) { rotate(delta); }
ePage currentPage() { return page; }
unsigned currentChannel() { return channel; }
bool auditioning() { return playing; }
void frame(char (&lines)[8][22]) { compose(); memcpy(lines,screen,sizeof(screen)); }
}
#endif
void begin() {
    for(unsigned i=0;i<BUTTONS_COUNT;++i) pinMode(pins[i],INPUT_PULLUP);
    pinMode(SugarBoard::encoderA,INPUT_PULLUP); pinMode(SugarBoard::encoderB,INPUT_PULLUP);
    encoder.previous=(digitalRead(SugarBoard::encoderA)<<1)|digitalRead(SugarBoard::encoderB);
    discover();
    if(Wire.begin(SugarBoard::oledSda,SugarBoard::oledScl,400000)) {
        Wire.setTimeOut(2);
        for(uint8_t candidate=0x3C;candidate<=0x3D;++candidate) {
            Wire.beginTransmission(candidate);
            if(Wire.endTransmission()==0) { address=candidate; break; }
        }
    }
    if(address) {
        const uint8_t init[]={0x00,0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,
            0xAD,0x8B,0xA1,0xC8,0xDA,0x12,0x81,0x7F,0xD9,0x22,0xDB,0x40,0xA4,0xA6,0xAF};
        transmit(init,sizeof(init));
    }
    ESP_LOGI("SugarUI","SH1106 address=0x%02x Core%u; buttons=41,42,2,1 encoder=15,16,14",address,unsigned(xPortGetCoreID()));
}
void tick() {
    const uint32_t start=micros(),now=millis();
    if(now!=lastScan) {
        lastScan=now;
        for(unsigned i=0;i<BUTTONS_COUNT;++i)
            if(buttons[i].update(digitalRead(pins[i])==LOW,now)) press(eButton(i),buttons[i].pressed);
        if(buttons[BUTTON_ENCODER].pressed && !longPress && uint32_t(now-encoderPressedAt)>=700 && !controlsBusy()) {
            longPress=true; home();
        }
        const int delta=encoder.update((digitalRead(SugarBoard::encoderA)<<1)|digitalRead(SugarBoard::encoderB));
        if(delta) rotate(delta);
    }
    displayTick(now);
    const uint32_t elapsed=micros()-start;
    if(elapsed>maxTickUs) maxTickUs=elapsed;
}
void report() {
    ESP_LOGI("SugarUI","oled=0x%02x ioErrors=%lu rejected=%lu tickMaxUs=%lu part=%u channel=%u",address,
        (unsigned long)errors,(unsigned long)rejected,(unsigned long)maxTickUs,part+1,channel+1);
}
}

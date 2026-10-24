#include "RoutingBoardChecks.h"
#if SUGAR_ROUTING_BOARD_CHECKS
#include "../audio/InstrumentEngine.h"
#include "../presets/PresetCodec.h"
#include "esp_log.h"
static_assert(SUGAR_PART_CAPACITY>=2,"Routing diagnostic requires bass and drums");
namespace SugarRoutingChecks {
namespace {
using namespace SugarParts;
using namespace SugarControl;
namespace UI=SugarControl::Testing;
enum eStage { STAGE_START, STAGE_HELD_NOTE, STAGE_INACTIVE, STAGE_CAPTURE,
    STAGE_RESET, STAGE_RELOAD, STAGE_RESTORE, STAGE_DONE, STAGES_COUNT };
eStage stage=STAGE_START;
SugarPresets::State original,edited,decoded;
char json[SugarPresets::maxFileBytes+1];
unsigned cursor=ROUTING_FIELD_CHANNEL;
uint32_t since=0;
bool passed=true;
void check(bool result,const char* name) {
    if(!result) { passed=false; ESP_LOGE("SugarRoutingTest","FAIL: %s",name); }
}
void advance(eStage next) { stage=next; since=millis(); }
void field(unsigned target) {
    while(cursor!=target) {
        UI::click(BUTTON_NEXT_FIELD);
        cursor=wrap(cursor,1,routingFieldCount(SUGAR_PART_CAPACITY));
    }
}
void route(unsigned channel,uint32_t mask) {
    field(ROUTING_FIELD_CHANNEL);
    UI::click(BUTTON_ENCODER);
    for(unsigned tries=0;tries<SugarParts::MIDI_CHANNELS_COUNT && UI::currentChannel()!=channel;++tries) UI::turn(1);
    UI::click(BUTTON_ENCODER);
    check(UI::currentChannel()==channel,"channel navigation");
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        field(unsigned(ROUTING_FIELD_PART)+p);
        UI::click(BUTTON_ENCODER);
        UI::turn(mask&partBit(ePartId(p)) ? 1 : -1);
        UI::click(BUTTON_ENCODER);
    }
    uint32_t actual=0;
    check(SugarEngine::getChannelRoute(uint8_t(channel),actual) && actual==mask,"GUI route edit");
}
void matches(const SugarPresets::State& expected) {
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch) {
        uint32_t actual=0;
        check(SugarEngine::getChannelRoute(uint8_t(ch),actual) && actual==expected.routes[ch],"all channel masks");
    }
}
void finish() {
    field(routingFieldCount(SUGAR_PART_CAPACITY)-1);
    UI::click(BUTTON_ENCODER);
    check(UI::currentPage()==PAGE_MENU && !UI::auditioning(),"BACK and note release");
    advance(STAGE_DONE);
    report();
}
}
bool tick() {
    if(stage==STAGE_DONE) return false;
    const auto now=millis();
    if(stage!=STAGE_START && now-since>30000) {
        check(false,"stage timeout");
        UI::button(BUTTON_AUDITION,false);
        if(!SugarEngine::presetBusy() && SugarEngine::submitPreset(original)) advance(STAGE_RESTORE);
        return true;
    }
    switch(stage) {
    case STAGE_START: {
        if(now<3000 || !SugarEngine::capturePreset(original)) return true;
        char menu[8][22]; UI::frame(menu);
        check(UI::currentPage()==PAGE_MENU && strstr(menu[1],"MIDI") && strstr(menu[2],"Effects") && strstr(menu[3],"Acid"),"grouped home pages");
        UI::turn(-1); UI::click(BUTTON_ENCODER);
        UI::frame(menu);
        check(UI::currentPage()==PAGE_FX && strstr(menu[0],"Effects") && strstr(menu[1],"Acid"),"FX page and target");
        UI::click(BUTTON_ENCODER); UI::frame(menu);
        check(strstr(menu[1],"Sound to process") && strstr(menu[3],"Acid"),"focused FX target editor");
        UI::turn(1); UI::click(BUTTON_ENCODER);
        UI::frame(menu);
        check(strstr(menu[1],"Drum")!=nullptr,"FX target editing");
        UI::button(BUTTON_AUDITION,true);
        UI::click(BUTTON_NEXT_PAGE); UI::frame(menu);
        check(UI::currentPage()==PAGE_SYNTH && !UI::auditioning() && strstr(menu[0],"Acid"),"next page opens and releases audition");
        UI::button(BUTTON_AUDITION,false);
        UI::click(BUTTON_BACK); // Home, selection Sound1.
        UI::turn(1); // Sound2.
        UI::click(BUTTON_ENCODER); UI::frame(menu);
        check(UI::currentPage()==PAGE_SYNTH && strstr(menu[0],"Drum") && strstr(menu[1],"Sound"),"per-synth page");
        UI::click(BUTTON_ENCODER); UI::click(BUTTON_BACK);
        check(UI::currentPage()==PAGE_SYNTH,"Back closes editor before leaving page");
        UI::click(BUTTON_NEXT_FIELD); UI::click(BUTTON_ENCODER); UI::frame(menu);
        check(strstr(menu[1],"Volume") && strstr(menu[3],"100%"),"percentage volume editor");
        UI::turn(-1);
        int32_t level=0;
        check(SugarEngine::getRequestedParameter(PART_2,SugarParameters::PARAM_PART_LEVEL,level) && level==gainFromPercent(99),"percentage edit queues stored gain");
        UI::click(BUTTON_BACK);
        UI::click(BUTTON_BACK); // Back returns home.
        for(unsigned f=MENU_SYNTH_FIRST+1;f>MENU_MIDI;--f) UI::turn(-1);
        UI::click(BUTTON_ENCODER);
        check(UI::currentPage()==PAGE_ROUTING,"open MIDI ROUTING");
        if(UI::currentPage()!=PAGE_ROUTING) { advance(STAGE_DONE); return false; }
        for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch)
            route(ch,partBit(ch==9 ? PART_2 : PART_1));
        // Unchanged masks must not fill the event queue with global-silence events.
        for(unsigned i=0;i<512;++i) check(SugarEngine::setChannelRoute(0,partBit(PART_1)),"no-op route requests");
        uint32_t guard=0xDEADBEEF;
        check(!SugarEngine::getChannelRoute(SugarParts::MIDI_CHANNELS_COUNT,guard) && guard==0xDEADBEEF,"invalid getter");
        route(2,0);
        char lines[8][22]; UI::frame(lines);
        check(strstr(lines[7],"muted")!=nullptr,"muted status");
        UI::button(BUTTON_AUDITION,true);
        check(!UI::auditioning(),"muted audition");
        UI::button(BUTTON_AUDITION,false);
        route(3,Routing<SUGAR_PART_CAPACITY>::availableMask);
        UI::frame(lines);
        check(strstr(lines[2],"On") && strstr(lines[3],"On"),"layer display");
        UI::button(BUTTON_AUDITION,true);
        check(UI::auditioning(),"routed C2 audition");
        advance(STAGE_HELD_NOTE);
        break;
    }
    case STAGE_HELD_NOTE:
        if(now-since<100) break;
        field(ROUTING_FIELD_CHANNEL); UI::click(BUTTON_ENCODER); UI::turn(1); UI::click(BUTTON_ENCODER);
        check(!UI::auditioning() && UI::currentChannel()==4,"channel change releases original destinations");
        route(3,Routing<SUGAR_PART_CAPACITY>::availableMask);
        check(SugarEngine::setPartActive(PART_2,false),"deactivate routed part");
        advance(STAGE_INACTIVE);
        break;
    case STAGE_INACTIVE: {
        if(now-since<100) break;
        check(!SugarEngine::isPartActive(PART_2),"applied inactive part");
        uint32_t mask=0;
        check(SugarEngine::getChannelRoute(3,mask) && mask==Routing<SUGAR_PART_CAPACITY>::availableMask,"inactive destination preserved");
        char lines[8][22]; UI::frame(lines);
        check(strstr(lines[3],"On*") && strstr(lines[7],"sound is off"),"inactive route display");
        check(SugarEngine::setPartActive(PART_2,true),"reactivate part");
        advance(STAGE_CAPTURE);
        break;
    }
    case STAGE_CAPTURE: {
        if(now-since<100) break;
        size_t length=0;
        check(SugarEngine::capturePreset(edited),"capture edited routing");
        check(SugarPresets::encode(edited,json,sizeof(json),length) && SugarPresets::decode(json,length,decoded),"JSON round trip");
        for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch)
            check(decoded.routes[ch]==edited.routes[ch],"JSON mute/layer/channel masks");
        check(SugarEngine::submitPreset(original),"reset before reload");
        advance(STAGE_RESET);
        break;
    }
    case STAGE_RESET:
        if(SugarEngine::presetBusy()) break;
        matches(original);
        check(SugarEngine::submitPreset(decoded),"submit decoded preset");
        advance(STAGE_RELOAD);
        break;
    case STAGE_RELOAD:
        if(SugarEngine::presetBusy()) break;
        matches(edited);
        { char lines[8][22]; UI::frame(lines);
          check(strstr(lines[2],"On") && strstr(lines[3],"On"),"loaded masks reflected in GUI"); }
        check(SugarEngine::submitPreset(original),"restore initial settings");
        advance(STAGE_RESTORE);
        break;
    case STAGE_RESTORE:
        if(SugarEngine::presetBusy()) break;
        matches(original);
        finish();
        break;
    default: break;
    }
    return stage!=STAGE_DONE;
}
void report() {
    ESP_LOGI("SugarRoutingTest","checks=%s stage=%u; GUI handlers/masks/JSON/handoff; physical controls and listening separate",
        passed ? (stage==STAGE_DONE ? "PASS" : "RUNNING") : "FAIL",unsigned(stage));
}
}
#endif

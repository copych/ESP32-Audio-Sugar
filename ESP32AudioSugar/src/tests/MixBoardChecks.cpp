#include "MixBoardChecks.h"
#if SUGAR_MIX_BOARD_CHECKS
#include "../audio/InstrumentEngine.h"
#include "esp_log.h"
static_assert(SUGAR_PART_CAPACITY>=2,"Mix diagnostic requires bass and drums");
namespace SugarMixChecks {
namespace {
using namespace SugarParts;
using namespace SugarParameters;
enum eStage { STAGE_START, STAGE_OPEN_BASS, STAGE_RESONANT_BASS, STAGE_COMBINED, STAGE_FX_WAIT, STAGE_FX,
    STAGE_TAIL, STAGE_RESTORING, STAGE_DONE, STAGES_COUNT };
eStage stage=STAGE_START;
SugarPresets::State original,fxSetup;
uint32_t started=0,lastHit=0;
bool accepted=true;
void note(ePartId part,uint8_t channel,uint8_t key,uint8_t velocity) {
    accepted=SugarEngine::enqueueToPart(part,{velocity ? SugarMidi::EVENT_NOTE_ON : SugarMidi::EVENT_NOTE_OFF,channel,key,velocity}) && accepted;
}
void parameter(ePartId part,eParameterId id,int32_t value,unsigned channel=0) {
    accepted=SugarEngine::setParameter(part,id,value,uint8_t(channel)) && accepted;
}
}
bool tick() {
    if(stage==STAGE_DONE) return false;
    const auto now=millis();
    if(stage==STAGE_START) {
        if(now<3000) return true;
        if(!SugarEngine::capturePreset(original)) return true;
        accepted=SugarEngine::setPartActive(PART_1,true) && SugarEngine::setPartActive(PART_2,true);
        parameter(PART_1,PARAM_VOICE_MODE,SugarAudio::VOICE_MODE_MONO_LEGATO);
        parameter(PART_1,PARAM_GAIN,127); parameter(PART_2,PARAM_GAIN,127,9);
        parameter(PART_1,PARAM_CUTOFF,127); parameter(PART_1,PARAM_RESONANCE,0);
        note(PART_1,0,36,127); started=now; stage=STAGE_OPEN_BASS;
        ESP_LOGI("SugarMixTest","OPEN_BASS: note36 velocity127 cutoff127 reso0");
    } else if(stage==STAGE_OPEN_BASS && now-started>=8000) {
        note(PART_1,0,36,0); parameter(PART_1,PARAM_CUTOFF,60); parameter(PART_1,PARAM_RESONANCE,127);
        note(PART_1,0,36,127); started=now; stage=STAGE_RESONANT_BASS;
        ESP_LOGI("SugarMixTest","RESONANT_BASS: note36 velocity127 cutoff60 reso127");
    } else if(stage==STAGE_RESONANT_BASS && now-started>=8000) {
        parameter(PART_1,PARAM_CUTOFF,127); parameter(PART_1,PARAM_RESONANCE,0);
        stage=STAGE_COMBINED; started=now; lastHit=now-100;
        ESP_LOGI("SugarMixTest","COMBINED: bass plus kick/snare/closed hat/tom at 10 triggers/sec each");
    } else if(stage==STAGE_COMBINED || stage==STAGE_FX) {
        if(now-lastHit>=100) {
            lastHit=now;
            constexpr uint8_t notes[]={36,38,42,45};
            for(auto key:notes) note(PART_2,9,key,127);
        }
        if(now-started>=8000) {
            if(stage==STAGE_COMBINED) {
                // Invalid requests must fail without publishing commands/state.
                accepted=!SugarEngine::setBusParameter(SugarFX::BUSES_COUNT,PARAM_DELAY_TIME_MS,120) && accepted;
                accepted=!SugarEngine::setBusParameter(SugarFX::BUS_DELAY,PARAM_CUTOFF,60) && accepted;
                accepted=!SugarEngine::selectFx(PART_1,SugarFX::FX_SLOTS_COUNT,SugarFX::ALGORITHM_DRIVE) && accepted;
                accepted=!SugarEngine::setFxBusControl(SugarFX::SharedSendEffect::controlCount(),0) && accepted;
                if(!SugarEngine::capturePreset(fxSetup)) accepted=false;
                for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
                    fxSetup.values[p][PARAM_PART_LEVEL][0]=127;
                    fxSetup.values[p][PARAM_FX_INSERT][0]=SugarFX::INSERT_EFFECT;
                    fxSetup.values[p][PARAM_FX_DRIVE][0]=80;
                    fxSetup.values[p][PARAM_FX_SEND][0]=100;
                }
                fxSetup.values[0][PARAM_DELAY_TIME_MS][0]=120;
                fxSetup.values[0][PARAM_DELAY_FEEDBACK][0]=70;
                fxSetup.values[0][PARAM_DELAY_RETURN][0]=100;
                SugarPresets::migrateLegacyEffects(fxSetup);
                for(auto& part:fxSetup.fx) for(unsigned slot=1;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
                    part[slot]=SugarFX::defaultSlot(SugarFX::ALGORITHM_CRUSHER);
                    part[slot].values[SugarFX::CrusherEffect::CONTROL_BITS]=10;
                    part[slot].values[SugarFX::CrusherEffect::CONTROL_HOLD]=3;
                }
                if(SugarEngine::submitPreset(fxSetup)) stage=STAGE_FX_WAIT;
                else { accepted=false; stage=STAGE_FX; }
                started=now;
            } else {
                const auto peak=SugarEngine::getFxReturnPeak();
                accepted=peak>0 && accepted;
                ESP_LOGI("SugarMixTest","FX wet return peak=%lu expected nonzero",(unsigned long)peak);
                note(PART_1,0,36,0);
                for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) parameter(ePartId(p),PARAM_FX_SEND,0);
                stage=STAGE_TAIL; started=now;
                ESP_LOGI("SugarMixTest","TAIL: sends ramp to zero; delay feedback continues for8s");
            }
        }
    } else if(stage==STAGE_FX_WAIT && !SugarEngine::presetBusy()) {
        note(PART_1,0,36,127); stage=STAGE_FX; started=now; lastHit=now-100;
        ESP_LOGI("SugarMixTest","FX: %u slots/part, Drive80 then crushers10bit hold3; delay120ms feedback70 send/return100",unsigned(SugarFX::FX_SLOTS_COUNT));
    } else if(stage==STAGE_TAIL && now-started>=8000) {
            const auto peak=SugarEngine::getFxReturnPeak();
            accepted=peak==0 && accepted;
            ESP_LOGI("SugarMixTest","FX tail final wet peak=%lu expected zero",(unsigned long)peak);
            if(SugarEngine::submitPreset(original)) { stage=STAGE_RESTORING; started=now; }
            else accepted=false;
    } else if(stage==STAGE_RESTORING && !SugarEngine::presetBusy()) {
        stage=STAGE_DONE;
        ESP_LOGI("SugarMixTest","Sequence complete: requests=%s settings restored; inspect meters/timing, listen separately",accepted ? "PASS" : "FAIL");
    }
    return stage!=STAGE_DONE;
}
void report() {
    ESP_LOGI("SugarMixTest","requestsAndFx=%s stage=%u",accepted ? (stage==STAGE_DONE ? "PASS" : "RUNNING") : "FAIL",unsigned(stage));
}
}
#endif

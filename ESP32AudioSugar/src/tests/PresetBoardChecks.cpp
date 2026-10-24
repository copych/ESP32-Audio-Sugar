#include <Arduino.h>
#include "PresetBoardChecks.h"
#if SUGAR_PRESET_BOARD_CHECKS
#include "esp_log.h"
#include "../presets/PresetStore.h"
#include "../audio/AudioTransport.h"
#include "../audio/InstrumentEngine.h"
namespace SugarPresetChecks {
namespace {
using namespace SugarPresets;
enum eStage : uint8_t { STAGE_START, STAGE_SAVING, STAGE_MUTATED, STAGE_OVERWRITING,
    STAGE_RESETTING, STAGE_LOADING, STAGE_RECOVER_MISSING, STAGE_RECOVER_CORRUPT, STAGE_INVALID,
    STAGE_RESTORE, STAGE_RESTORING, STAGE_CLEANUP, STAGE_MISSING, STAGE_DONE, STAGES_COUNT };
enum eResult : uint8_t { RESULT_RUNNING, RESULT_PASS, RESULT_FAIL, RESULT_SKIP, RESULTS_COUNT };
constexpr const char* resultNames[RESULTS_COUNT]={"RUNNING","PASS","FAIL","SKIP"};
eStage stage=STAGE_START;
eResult results[STORAGES_COUNT]={};
eStorage backend=STORAGE_FLASH;
State original,changed;
eSlot slot=SLOTS_COUNT;
char name[40],temporary[40],backup[40];
bool passed=true,owned=false;
uint32_t started=0;
fs::FS& files() { return Storage::filesystem(backend); }
bool matches(const State& expected) {
    State captured;
    if(!SugarEngine::capturePreset(captured) || captured.active!=expected.active) return false;
    for(unsigned ch=0;ch<SugarParts::MIDI_CHANNELS_COUNT;++ch)
        if(captured.routes[ch]!=expected.routes[ch]) return false;
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) {
        const auto part=SugarParts::ePartId(p);
        if(SugarEngine::isPartActive(part)!=bool(expected.active&SugarParts::partBit(part))) return false;
        for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw) {
            const auto id=SugarParameters::eParameterId(raw);
            const auto* d=SugarEngine::getParameterDescriptor(part,id);
            if(!d) continue;
            const unsigned count=d->scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
            for(unsigned ch=0;ch<count;++ch) {
                int32_t applied=0;
                if(captured.values[p][id][ch]!=expected.values[p][id][ch] ||
                    !SugarEngine::getAppliedParameter(part,id,applied,ch) || applied!=expected.values[p][id][ch]) return false;
            }
        }
    }
    for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned slot=0;slot<SugarFX::FX_SLOTS_COUNT;++slot) {
        const auto& want=expected.fx[p][slot]; const auto& got=captured.fx[p][slot];
        SugarFX::eAlgorithm audio;
        if(want.algorithm!=got.algorithm || !SugarEngine::getFxAlgorithm(SugarParts::ePartId(p),slot,audio,true) || audio!=want.algorithm) return false;
        for(unsigned i=0;i<SugarFX::InsertPool::controlCount(want.algorithm);++i) {
            int32_t value=0;
            if(want.values[i]!=got.values[i] || !SugarEngine::getFxValue(SugarParts::ePartId(p),slot,i,value,true) || value!=want.values[i]) return false;
        }
    }
    for(unsigned i=0;i<SugarFX::SharedSendEffect::controlCount();++i) {
        int32_t value=0;
        if(expected.busValues[i]!=captured.busValues[i] || !SugarEngine::getFxBusValue(i,value,true) || value!=expected.busValues[i]) return false;
    }
    return true;
}
bool finish(eResult result) {
    results[backend]=result;
    ESP_LOGI("SugarPresetTest","storage=%s result=%s slot=%u elapsedMs=%lu",storageName(backend),resultNames[result],
        slot<SLOTS_COUNT ? unsigned(slot)+1 : 0,(unsigned long)(millis()-started));
    if(unsigned(backend)+1<STORAGES_COUNT) {
        backend=eStorage(unsigned(backend)+1); passed=true; owned=false; slot=SLOTS_COUNT; stage=STAGE_START;
        return true;
    }
    selectStorage(STORAGE_FLASH); stage=STAGE_DONE; return false;
}
void fail(const char* reason) {
    passed=false;
    ESP_LOGE("SugarPresetTest","storage=%s FAIL stage=%u reason=%s",storageName(backend),unsigned(stage),reason);
    stage=STAGE_RESTORE;
}
bool createFault(bool corrupt) {
    if(!SugarAudio::pauseForStorage()) return false;
    bool ok=false;
    if(corrupt) {
        auto file=files().open(name,FILE_WRITE);
        constexpr char bad[]="{\"version\":";
        if(file) { ok=file.write(reinterpret_cast<const uint8_t*>(bad),sizeof(bad)-1)==sizeof(bad)-1; file.flush(); file.close(); }
    } else ok=files().remove(name);
    const bool resumed=SugarAudio::resumeAfterStorage();
    return ok && resumed;
}
}
bool tick() {
    if(stage==STAGE_DONE) return false;
    if(stage==STAGE_START) {
        started=millis();
        char contract[40];
        const char* expected=backend==STORAGE_FLASH ? "/preset-1.json" : "/sugar/preset-1.json";
        if(!presetPath(backend,SLOT_1,PRESET_FILE_JSON,contract,sizeof(contract)) || strcmp(contract,expected)!=0 ||
            presetPath(STORAGES_COUNT,SLOT_1,PRESET_FILE_JSON,contract,sizeof(contract)) ||
            presetPath(backend,SLOTS_COUNT,PRESET_FILE_JSON,contract,sizeof(contract)) ||
            presetPath(backend,SLOT_1,PRESET_FILES_COUNT,contract,sizeof(contract)) ||
            presetPath(backend,SLOT_1,PRESET_FILE_JSON,contract,2)) return finish(RESULT_FAIL);
        if(!selectStorage(backend) || currentStatus()!=STATUS_IDLE || !SugarEngine::capturePreset(original))
            return finish(RESULT_SKIP);
        for(unsigned i=SLOTS_COUNT;i>0;--i) {
            const auto candidate=eSlot(i-1);
            presetPath(backend,candidate,PRESET_FILE_JSON,name,sizeof(name));
            presetPath(backend,candidate,PRESET_FILE_TEMP,temporary,sizeof(temporary));
            presetPath(backend,candidate,PRESET_FILE_BACKUP,backup,sizeof(backup));
            if(!files().exists(name) && !files().exists(temporary) && !files().exists(backup)) { slot=candidate; break; }
        }
        if(slot==SLOTS_COUNT) return finish(RESULT_SKIP);
        if(!request(COMMAND_SAVE,slot)) { fail("save request"); return true; }
        owned=true; stage=STAGE_SAVING; return true;
    }
    if(uint32_t(millis()-started)>30000 && stage!=STAGE_CLEANUP) {
        ESP_LOGE("SugarPresetTest","FAIL timeout; cleanup/restoration may be incomplete");
        return finish(RESULT_FAIL);
    }
    if(stage==STAGE_SAVING && !busy()) {
        if(currentStatus()!=STATUS_SAVED) { fail("save result"); return true; }
        changed=original;
        constexpr uint32_t mask=SugarParts::Routing<SUGAR_PART_CAPACITY>::availableMask;
        changed.active^=mask; changed.routes[0]=changed.routes[0] ? 0 : mask;
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p)
            for(unsigned raw=0;raw<SugarParameters::PARAMETERS_COUNT;++raw) {
                const auto id=SugarParameters::eParameterId(raw);
                const auto* d=SugarEngine::getParameterDescriptor(SugarParts::ePartId(p),id);
                if(!d) continue;
                const unsigned count=d->scope==SugarParameters::PARAM_SCOPE_PART_CHANNEL ? SugarParameters::MIDI_CHANNELS_COUNT : 1;
                for(unsigned ch=0;ch<count;++ch) changed.values[p][id][ch]=original.values[p][id][ch]!=d->minimum ? d->minimum : d->maximum;
            }
        migrateLegacyEffects(changed);
        for(unsigned p=0;p<SUGAR_PART_CAPACITY;++p) for(unsigned index=1;index<SugarFX::FX_SLOTS_COUNT;++index) {
            auto& slot=changed.fx[p][index]; slot=SugarFX::defaultSlot(SugarFX::ALGORITHM_CRUSHER);
            for(unsigned i=0;i<SugarFX::InsertPool::controlCount(slot.algorithm);++i) {
                const auto* control=SugarFX::InsertPool::control(slot.algorithm,i);
                slot.values[i]=original.fx[p][index].values[i]==control->maximum ? control->minimum : control->maximum;
            }
        }
        if(!SugarEngine::submitPreset(changed)) { fail("mutation submit"); return true; }
        stage=STAGE_MUTATED;
    } else if(stage==STAGE_MUTATED && !SugarEngine::presetBusy()) {
        if(!matches(changed) || !request(COMMAND_SAVE,slot)) { fail("overwrite request/state"); return true; }
        stage=STAGE_OVERWRITING;
    } else if(stage==STAGE_OVERWRITING && !busy()) {
        if(currentStatus()!=STATUS_SAVED || !SugarEngine::submitPreset(original)) { fail("overwrite/reset"); return true; }
        stage=STAGE_RESETTING;
    } else if(stage==STAGE_RESETTING && !SugarEngine::presetBusy()) {
        if(!matches(original) || !request(COMMAND_LOAD,slot)) { fail("load request/state"); return true; }
        stage=STAGE_LOADING;
    } else if(stage==STAGE_LOADING && !busy()) {
        if(currentStatus()!=STATUS_LOADED || !matches(changed)) { fail("loaded overwrite"); return true; }
        if(backend==STORAGE_SD) {
            if(!files().exists(backup) || !createFault(false) || !request(COMMAND_LOAD,slot)) { fail("missing-main setup"); return true; }
            stage=STAGE_RECOVER_MISSING;
        } else stage=STAGE_RESTORE;
    } else if(stage==STAGE_RECOVER_MISSING && !busy()) {
        if(currentStatus()!=STATUS_RECOVERED || !matches(original)) { fail("missing-main recovery"); return true; }
        if(!createFault(true) || !request(COMMAND_LOAD,slot)) { fail("corrupt-main setup"); return true; }
        stage=STAGE_RECOVER_CORRUPT;
    } else if(stage==STAGE_RECOVER_CORRUPT && !busy()) {
        if(currentStatus()!=STATUS_RECOVERED || !matches(original)) { fail("corrupt-main recovery"); return true; }
        if(!SugarAudio::pauseForStorage()) { fail("invalid-file setup pause"); return true; }
        const bool removed=files().remove(backup);
        const bool resumed=SugarAudio::resumeAfterStorage();
        if(!removed || !resumed || !request(COMMAND_LOAD,slot)) { fail("invalid-file setup"); return true; }
        stage=STAGE_INVALID;
    } else if(stage==STAGE_INVALID && !busy()) {
        if(currentStatus()!=STATUS_INVALID || !matches(original)) { fail("invalid-file rejection"); return true; }
        stage=STAGE_RESTORE;
    } else if(stage==STAGE_RESTORE && !SugarEngine::presetBusy()) {
        if(SugarEngine::submitPreset(original)) stage=STAGE_RESTORING;
    } else if(stage==STAGE_RESTORING && !SugarEngine::presetBusy()) stage=STAGE_CLEANUP;
    if(stage==STAGE_CLEANUP) {
        if(owned) {
            if(Storage::ready(backend) && SugarAudio::pauseForStorage()) {
                const char* paths[]={name,temporary,backup};
                for(const char* file:paths) if(files().exists(file) && !files().remove(file)) passed=false;
                if(!SugarAudio::resumeAfterStorage()) passed=false;
            } else passed=false;
        }
        if(!matches(original)) passed=false;
        if(!request(COMMAND_LOAD,slot)) return finish(RESULT_FAIL);
        stage=STAGE_MISSING; return true;
    }
    if(stage==STAGE_MISSING && !busy()) {
        if(currentStatus()!=STATUS_MISSING || !matches(original)) passed=false;
        return finish(passed ? RESULT_PASS : RESULT_FAIL);
    }
    return true;
}
void report() {
    ESP_LOGI("SugarPresetTest","flash=%s sd=%s stage=%u",resultNames[results[STORAGE_FLASH]],resultNames[results[STORAGE_SD]],unsigned(stage));
}
}
#endif

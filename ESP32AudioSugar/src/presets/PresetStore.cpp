#include <Arduino.h>
#include <LittleFS.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "PresetStore.h"
#include "PresetCodec.h"
#include "../audio/AudioTransport.h"
#include "../audio/InstrumentEngine.h"
namespace SugarPresets {
namespace {
char buffer[maxFileBytes+1];
State scratch;
eCommand pending=COMMAND_NONE;
eStatus status=STATUS_IDLE;
eSlot selected=SLOT_1;
bool checksPassed=false,waitingCommit=false;
eStorage storage=STORAGE_FLASH;
bool recovered=false;
uint32_t scheduled=0,maximumUs=0;
uint32_t commitStarted=0;
constexpr const char* statuses[STATUSES_COUNT]={"READY","FORMAT REQUIRED","WORKING","SAVED",
    "LOADED","FORMATTED","EMPTY SLOT","INVALID PRESET","STORAGE ERROR","ENGINE BUSY","CODEC CHECK FAILED",
    "SD UNAVAILABLE","RECOVERED BACKUP","SD REMOUNTED"};
eStatus unavailable() { return storage==STORAGE_SD ? STATUS_NO_MEDIA : STATUS_NO_FILESYSTEM; }
fs::FS& filesystem() { return Storage::filesystem(storage); }
bool accessible() {
    return Storage::ready(storage) && (storage!=STORAGE_SD || Storage::probeSD());
}
void path(char* out,size_t size,ePresetFile file=PRESET_FILE_JSON) {
    presetPath(storage,selected,file,out,size);
}
uint32_t checksum(const char* bytes,size_t length) {
    uint32_t hash=2166136261U;
    for(size_t i=0;i<length;++i) hash=(hash^uint8_t(bytes[i]))*16777619U;
    return hash;
}
enum eReadResult : uint8_t { READ_OK, READ_INVALID_SIZE, READ_IO_ERROR, READ_RESULTS_COUNT };
eReadResult readFile(const char* name,size_t& length) {
    auto file=filesystem().open(name,FILE_READ);
    if(!file || file.isDirectory()) return READ_IO_ERROR;
    length=file.size();
    if(!length || length>maxFileBytes) return READ_INVALID_SIZE;
    if(file.read(reinterpret_cast<uint8_t*>(buffer),length)!=length) return READ_IO_ERROR;
    buffer[length]=0;
    return READ_OK;
}
eStatus save() {
    if(!accessible()) return unavailable();
    if(!SugarEngine::capturePreset(scratch)) return STATUS_BUSY;
    size_t length=0;
    if(!encode(scratch,buffer,sizeof(buffer),length)) return STATUS_INVALID;
    const uint32_t expected=checksum(buffer,length);
    if(!SugarAudio::pauseForStorage()) return STATUS_BUSY;
    char destination[40],temporary[40],backup[40];
    path(destination,sizeof(destination)); path(temporary,sizeof(temporary),PRESET_FILE_TEMP);
    path(backup,sizeof(backup),PRESET_FILE_BACKUP);
    bool success=false;
    {
        // Only an explicit SD save creates our directory; mounting is read-only
        // at the application level and never formats an unrecognized card.
        bool directory=true;
        if(storage==STORAGE_SD) directory=filesystem().exists("/sugar") || filesystem().mkdir("/sugar");
        auto file=directory ? filesystem().open(temporary,FILE_WRITE) : File();
        if(file) {
            const size_t written=file.write(reinterpret_cast<const uint8_t*>(buffer),length);
            file.flush(); file.close();
            // Verify persisted content before replacing the existing slot.
            size_t verified=0;
            if(written==length && readFile(temporary,verified)==READ_OK && verified==length && checksum(buffer,verified)==expected && decode(buffer,verified,scratch)) {
                bool replace=true;
                if(storage==STORAGE_SD && filesystem().exists(destination)) {
                    size_t oldLength=0;
                    const auto oldRead=readFile(destination,oldLength);
                    if(oldRead==READ_IO_ERROR) replace=false; // never treat an I/O error as corrupt content
                    else if(oldRead==READ_OK && decode(buffer,oldLength,scratch)) {
                        // FAT cannot replace an existing destination atomically.
                        // Keep the current valid main file until its backup is ready.
                        replace=(!filesystem().exists(backup) || filesystem().remove(backup)) && filesystem().rename(destination,backup);
                    } else {
                        // Preserve any existing backup if the main file is corrupt.
                        replace=filesystem().remove(destination);
                    }
                }
                if(replace) success=filesystem().rename(temporary,destination);
            }
        }
    }
    // A failed temporary file is never considered a preset and can be replaced
    // by a later save. Valid existing presets survive through install/backup rename.
    const bool resumed=SugarAudio::resumeAfterStorage();
    if(!success && storage==STORAGE_SD && !Storage::probeSD()) return STATUS_NO_MEDIA;
    return success && resumed ? STATUS_SAVED : STATUS_IO_ERROR;
}
eStatus load() {
    if(!accessible()) return unavailable();
    char name[40]; path(name,sizeof(name));
    size_t length=0;
    const bool mainExists=filesystem().exists(name);
    bool valid=mainExists && readFile(name,length)==READ_OK && decode(buffer,length,scratch);
    recovered=false;
    bool backupExists=false;
    if(!valid && storage==STORAGE_SD) {
        path(name,sizeof(name),PRESET_FILE_BACKUP);
        backupExists=filesystem().exists(name);
        valid=backupExists && readFile(name,length)==READ_OK && decode(buffer,length,scratch);
        recovered=valid;
    }
    if(!valid) {
        if(storage==STORAGE_SD && !Storage::probeSD()) return STATUS_NO_MEDIA;
        return mainExists || backupExists ? STATUS_INVALID : STATUS_MISSING;
    }
    if(!SugarEngine::submitPreset(scratch)) return STATUS_BUSY;
    waitingCommit=true;
    commitStarted=millis();
    return STATUS_WORKING;
}
eStatus format() {
    if(storage!=STORAGE_FLASH) return STATUS_NO_MEDIA; // never format an SD card
    if(!SugarAudio::pauseForStorage()) return STATUS_BUSY;
    if(Storage::ready(STORAGE_FLASH)) LittleFS.end();
    Storage::setFlashReady(false);
    // Avoid LittleFS.format()'s global logging/WDT changes: use the exposed
    // IDF component directly, while the audio transport is already quiescent.
    if(esp_littlefs_format("spiffs")==ESP_OK) Storage::setFlashReady(LittleFS.begin(false));
    const bool resumed=SugarAudio::resumeAfterStorage();
    if(!resumed) return STATUS_IO_ERROR;
    return Storage::ready(STORAGE_FLASH) ? STATUS_FORMATTED : STATUS_IO_ERROR;
}
eStatus remount() {
    if(storage!=STORAGE_SD) return STATUS_IO_ERROR;
    if(!SugarAudio::pauseForStorage()) return STATUS_BUSY;
    const bool ready=Storage::remountSD();
    const bool resumed=SugarAudio::resumeAfterStorage();
    return !resumed ? STATUS_IO_ERROR : ready ? STATUS_REMOUNTED : STATUS_NO_MEDIA;
}
}
void begin() {
    checksPassed=codecChecks();
    Storage::begin();
    status=!checksPassed ? STATUS_CHECK_FAILED : Storage::ready(storage) ? STATUS_IDLE : unavailable();
    ESP_LOGI("SugarPreset","ArduinoJson 7.4.3 codecChecks=%s flash=%u sd=%u slots=%u",checksPassed ? "PASS" : "FAIL",unsigned(Storage::ready(STORAGE_FLASH)),unsigned(Storage::ready(STORAGE_SD)),unsigned(SLOTS_COUNT));
}
bool busy() { return pending!=COMMAND_NONE || waitingCommit || SugarEngine::presetBusy(); }
bool selectStorage(eStorage next) {
    if(!validStorage(next) || busy()) return false;
    storage=next;
    status=!checksPassed ? STATUS_CHECK_FAILED : Storage::ready(storage) ? STATUS_IDLE : unavailable();
    return true;
}
eStorage selectedStorage() { return storage; }
bool request(eCommand command,eSlot slot) {
    if(!checksPassed || busy() || command<=COMMAND_NONE || command>=COMMANDS_COUNT || slot>=SLOTS_COUNT) return false;
    if((command==COMMAND_FORMAT && storage!=STORAGE_FLASH) || (command==COMMAND_REMOUNT && storage!=STORAGE_SD)) return false;
    selected=slot; pending=command; status=STATUS_WORKING; scheduled=millis();
    return true;
}
void tick() {
    if(waitingCommit) {
        if(!SugarEngine::presetBusy()) { waitingCommit=false; status=recovered ? STATUS_RECOVERED : STATUS_LOADED; }
        else if(uint32_t(millis()-commitStarted)>250) status=STATUS_IO_ERROR;
        return;
    }
    // Allow the status frame to finish before entering a synchronous flash operation.
    if(pending==COMMAND_NONE || uint32_t(millis()-scheduled)<250) return;
    const uint32_t start=micros();
    switch(pending) {
    case COMMAND_SAVE: status=save(); break;
    case COMMAND_LOAD: status=load(); break;
    case COMMAND_FORMAT: status=format(); break;
    case COMMAND_REMOUNT: status=remount(); break;
    default: break;
    }
    pending=COMMAND_NONE;
    const uint32_t elapsed=micros()-start;
    if(elapsed>maximumUs) maximumUs=elapsed;
    ESP_LOGI("SugarPreset","storage=%s slot=%u result=%s operationUs=%lu",storageName(storage),unsigned(selected)+1,statuses[status],(unsigned long)elapsed);
}
const char* statusText() { return statuses[status]; }
eStatus currentStatus() { return status; }
void report() {
    ESP_LOGI("SugarPreset","storage=%s flash=%u sd=%u state=%s busy=%u storageMaxUs=%lu codec=%s",storageName(storage),unsigned(Storage::ready(STORAGE_FLASH)),unsigned(Storage::ready(STORAGE_SD)),statuses[status],unsigned(busy()),(unsigned long)maximumUs,codecCheckDetail());
}
}

#include <Arduino.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "../../BoardConfig.h"
#include "PresetStorage.h"
#ifdef BOARD_HAS_1BIT_SDMMC
#error "This wiring requires a board variant that supports four-bit SDMMC."
#endif
namespace SugarPresets {
namespace {
bool mounted[STORAGES_COUNT]={};
static DRAM_ATTR uint32_t sector[512/sizeof(uint32_t)];
}
bool presetPath(eStorage storage,eSlot slot,ePresetFile file,char* out,size_t capacity) {
    if(!out || !capacity || !validStorage(storage) || slot>=SLOTS_COUNT || file>=PRESET_FILES_COUNT) return false;
    const char* suffix=nullptr;
    switch(file) {
    case PRESET_FILE_JSON: suffix="json"; break;
    case PRESET_FILE_TEMP: suffix="tmp"; break;
    case PRESET_FILE_BACKUP: suffix="bak"; break;
    default: return false;
    }
    const int size=snprintf(out,capacity,"%s/preset-%u.%s",storage==STORAGE_SD ? "/sugar" : "",unsigned(slot)+1,suffix);
    return size>0 && size_t(size)<capacity;
}
namespace Storage {
bool ready(eStorage storage) { return validStorage(storage) && mounted[storage]; }
fs::FS& filesystem(eStorage storage) { return storage==STORAGE_SD ? static_cast<fs::FS&>(SD_MMC) : static_cast<fs::FS&>(LittleFS); }
void invalidateSD() { SD_MMC.end(); mounted[STORAGE_SD]=false; }
bool probeSD() {
    if(!mounted[STORAGE_SD]) return false;
    if(SD_MMC.sectorSize()==int(sizeof(sector)) && SD_MMC.readRAW(reinterpret_cast<uint8_t*>(sector),0)) return true;
    invalidateSD(); return false;
}
bool remountSD() {
    invalidateSD();
    // Same pull-up preparation as the sampler for CMD and the four data lines.
    // External pull-ups remain required by the SDMMC electrical interface.
    const int pullups[]={SugarBoard::sdCommand,SugarBoard::sdData0,SugarBoard::sdData1,SugarBoard::sdData2,SugarBoard::sdData3};
    for(int pin:pullups) { gpio_pulldown_dis(gpio_num_t(pin)); gpio_pullup_en(gpio_num_t(pin)); }
    gpio_pulldown_dis(gpio_num_t(SugarBoard::sdClock));
    const bool pins=SD_MMC.setPins(SugarBoard::sdClock,SugarBoard::sdCommand,SugarBoard::sdData0,
        SugarBoard::sdData1,SugarBoard::sdData2,SugarBoard::sdData3);
    mounted[STORAGE_SD]=pins && SD_MMC.begin("/sdcard",false,false,SugarBoard::sdFrequencyKHz,3);
    if(mounted[STORAGE_SD]) probeSD();
    ESP_LOGI("SugarStorage","SD four-bit %u kHz mounted=%u sizeMiB=%lu",unsigned(SugarBoard::sdFrequencyKHz),unsigned(mounted[STORAGE_SD]),
        mounted[STORAGE_SD] ? (unsigned long)(SD_MMC.cardSize()>>20) : 0UL);
    return mounted[STORAGE_SD];
}
void setFlashReady(bool value) { mounted[STORAGE_FLASH]=value; }
void begin() {
    mounted[STORAGE_FLASH]=LittleFS.begin(false);
    remountSD();
}
}
}

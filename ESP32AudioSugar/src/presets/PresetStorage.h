#pragma once
#include <FS.h>
#include "PresetState.h"
namespace SugarPresets {
enum eStorage : uint8_t { STORAGE_FLASH, STORAGE_SD, STORAGES_COUNT };
enum ePresetFile : uint8_t { PRESET_FILE_JSON, PRESET_FILE_TEMP, PRESET_FILE_BACKUP, PRESET_FILES_COUNT };
constexpr bool validStorage(eStorage storage) { return unsigned(storage)<STORAGES_COUNT; }
constexpr const char* storageName(eStorage storage) {
    switch(storage) {
    case STORAGE_FLASH: return "FLASH";
    case STORAGE_SD: return "SD";
    default: return nullptr;
    }
}
bool presetPath(eStorage storage,eSlot slot,ePresetFile file,char* out,size_t capacity);
namespace Storage {
void begin(); // Core1 before audio; one mount attempt each, never format/mkdir
bool ready(eStorage storage);
fs::FS& filesystem(eStorage storage); // Core1 only; caller must validate storage/ready
bool remountSD(); // Core1, explicit operation; audio paused by caller
bool probeSD(); // bounded driver sector read, no file handles may be open
void invalidateSD(); // no file handles may be open
void setFlashReady(bool ready);
}
}

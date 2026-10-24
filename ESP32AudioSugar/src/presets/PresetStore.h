#pragma once
#include "PresetState.h"
#include "PresetStorage.h"
namespace SugarPresets {
enum eCommand : uint8_t { COMMAND_NONE, COMMAND_SAVE, COMMAND_LOAD, COMMAND_FORMAT, COMMAND_REMOUNT, COMMANDS_COUNT };
enum eStatus : uint8_t { STATUS_IDLE, STATUS_NO_FILESYSTEM, STATUS_WORKING, STATUS_SAVED,
    STATUS_LOADED, STATUS_FORMATTED, STATUS_MISSING, STATUS_INVALID, STATUS_IO_ERROR,
    STATUS_BUSY, STATUS_CHECK_FAILED, STATUS_NO_MEDIA, STATUS_RECOVERED, STATUS_REMOUNTED, STATUSES_COUNT };
void begin(); // Core1 before audio starts; never auto-format or auto-load
bool request(eCommand command,eSlot slot);
void tick(); // Core1 loop; storage writes explicitly pause I2S
bool busy();
bool selectStorage(eStorage storage); // Core1, rejects while an operation is pending
eStorage selectedStorage();
const char* statusText();
eStatus currentStatus();
void report();
}

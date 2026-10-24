#include <Arduino.h>
#include "../../BoardConfig.h"
#include "../presets/PresetStorage.h"
namespace {
constexpr bool separatePins() {
    using namespace SugarBoard;
    constexpr int pins[]={bclk,dout,wordClock,4,oledSda,oledScl,encoderA,encoderB,encoderSwitch,
        previousPart,nextPart,nextField,audition,sdCommand,sdClock,sdData0,sdData1,sdData2,sdData3};
    constexpr unsigned count=sizeof(pins)/sizeof(pins[0]);
    for(unsigned i=0;i<count;++i)
        for(unsigned j=0;j<i;++j) if(pins[i]==pins[j]) return false;
    return true;
}
static_assert(separatePins(),"SD, microphone data, DAC, display and controls must not overlap");
static_assert(SugarPresets::validStorage(SugarPresets::STORAGE_FLASH) &&
    SugarPresets::validStorage(SugarPresets::STORAGE_SD) &&
    !SugarPresets::validStorage(SugarPresets::STORAGES_COUNT) &&
    !SugarPresets::storageName(SugarPresets::STORAGES_COUNT),"Reject storage enum sentinel");
}

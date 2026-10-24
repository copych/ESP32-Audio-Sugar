#pragma once
#include <stdint.h>
#include "FxConfig.h"
namespace SugarFX {
enum eFxSlot : uint8_t { FX_SLOT_FIRST, FX_SLOTS_COUNT=SUGAR_FX_SLOTS };
enum eAlgorithm : uint8_t { ALGORITHM_BYPASS, ALGORITHM_DRIVE, ALGORITHM_CRUSHER, ALGORITHMS_COUNT };
enum eInsertType : uint8_t { INSERT_BYPASS, INSERT_EFFECT,
    INSERT_DRIVE=INSERT_EFFECT, // Source compatibility; the selected type now supplies the algorithm.
    INSERTS_COUNT=INSERT_EFFECT+1 };
enum eBus : uint8_t { BUS_MAIN, BUS_DELAY, BUSES_COUNT };
}

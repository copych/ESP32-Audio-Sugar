#pragma once
// Per-part serial insert slots. Override with compiler.cpp.extra_flags or here.
#ifndef SUGAR_FX_SLOTS
#define SUGAR_FX_SLOTS 2
#endif
static_assert(SUGAR_FX_SLOTS>=1 && SUGAR_FX_SLOTS<=8,"Choose 1..8 insert slots per part; remeasure each configuration");

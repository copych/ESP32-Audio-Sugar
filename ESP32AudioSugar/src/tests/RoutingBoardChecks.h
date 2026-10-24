#pragma once
#include <Arduino.h>
#include "../../Config.h"
#ifndef SUGAR_ROUTING_BOARD_CHECKS
#define SUGAR_ROUTING_BOARD_CHECKS 0
#endif
#if SUGAR_ROUTING_BOARD_CHECKS && SUGAR_MIX_BOARD_CHECKS
#error "Run routing and mix diagnostics separately."
#endif
namespace SugarRoutingChecks {
#if SUGAR_ROUTING_BOARD_CHECKS
bool tick();
void report();
#else
inline bool tick() { return false; }
inline void report() {}
#endif
}
#if SUGAR_ROUTING_BOARD_CHECKS
#include "../control/ControlLogic.h"
namespace SugarControl::Testing {
void click(eButton button);
void button(eButton button,bool down);
void turn(int delta);
ePage currentPage();
unsigned currentChannel();
bool auditioning();
void frame(char (&lines)[8][22]);
}
#endif

// HTTP API (rulebook interface contract), WebSocket telemetry, dashboard files.
#pragma once

#include <Arduino.h>

void webBegin(bool filesystemOk);
// Called from loop(): telemetry broadcast and client housekeeping.
void webLoop(uint32_t nowMs);

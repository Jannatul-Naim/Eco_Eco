// USB serial console for bench bring-up (type "help" in the serial monitor).
#pragma once

#include <Arduino.h>

void consoleLoop(uint32_t nowMs);
// Boot banner; also printed by the "info" command, since a native-USB monitor
// usually opens after the board has already printed it.
void consolePrintBanner();
// Why the S3 last restarted ("power_on", "brownout", "panic", ...).
const char* resetReasonName();

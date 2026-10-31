#include "estop_input.h"

#include "config_hardware.h"
#include "config_pins.h"

namespace {
constexpr uint32_t kReleaseStableMs = 100;  // released must be stable; pressed acts at once
}

void EStopInput::begin() {
  if (!ESTOP_SENSE_FITTED) {
    pressed_ = false;
    return;
  }
  pinMode(PIN_ESTOP_SENSE, INPUT_PULLUP);
  lastRaw_ = digitalRead(PIN_ESTOP_SENSE) == HIGH;
  pressed_ = true;
  rawSinceMs_ = millis();
}

bool EStopInput::fitted() const { return ESTOP_SENSE_FITTED; }

void EStopInput::update(uint32_t nowMs) {
  if (!ESTOP_SENSE_FITTED) return;
  const bool rawPressed = digitalRead(PIN_ESTOP_SENSE) == HIGH;
  if (rawPressed != lastRaw_) {
    lastRaw_ = rawPressed;
    rawSinceMs_ = nowMs;
  }
  if (rawPressed) {
    pressed_ = true;
  } else if (nowMs - rawSinceMs_ >= kReleaseStableMs) {
    pressed_ = false;
  }
}

const char* EStopInput::senseName() const {
  if (!ESTOP_SENSE_FITTED) return "not_fitted";
  return pressed_ ? "pressed" : "released";
}

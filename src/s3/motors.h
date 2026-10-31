// Low-level motor driver outputs (one single-channel driver per side).
// Only Rover calls write(); hardStop() may be called from any task.
#pragma once

#include <Arduino.h>

class MotorOutputs {
 public:
  void begin();
  // Signed duty per side in [-1, 1]. Applies invert flags. Caller has already
  // applied caps, slew limits and interlocks.
  void write(float left, float right);
  // Immediate zero output on every channel (and EN low on BTS7960).
  void hardStop();
  float left() const { return left_; }
  float right() const { return right_; }
  const char* driverName() const;

 private:
  void writeSide(int pinA, int pinB, uint8_t chA, uint8_t chB, float duty);
  float left_ = 0.0f;
  float right_ = 0.0f;
};

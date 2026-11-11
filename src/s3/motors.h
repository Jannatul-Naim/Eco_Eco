// Low-level motor driver outputs: one BTS7960 (IBT-2) module per side.
// Only Rover calls write(); hardStop() may be called from any task.
#pragma once

#include <Arduino.h>

class MotorOutputs {
 public:
  void begin();
  // Signed duty per side in [-1, 1]. Applies invert flags. Caller has already
  // applied caps, slew limits and interlocks.
  void write(float left, float right);
  // Immediate zero output on every channel, EN low.
  void hardStop();
  float left() const { return left_; }
  float right() const { return right_; }

 private:
  void writeSide(uint8_t chForward, uint8_t chReverse, float duty);
  float left_ = 0.0f;
  float right_ = 0.0f;
};

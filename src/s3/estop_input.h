// Hardware E-stop sense input (second pole of a 2-pole E-stop switch).
// This input only INFORMS the firmware. Cutting motor power is done by the
// E-stop switch in hardware and never depends on this code.
#pragma once

#include <Arduino.h>

class EStopInput {
 public:
  void begin();
  void update(uint32_t nowMs);
  // True when pressed, unplugged or wire broken (fail-safe).
  bool pressed() const { return pressed_; }
  bool fitted() const;
  const char* senseName() const;

 private:
  bool pressed_ = true;  // assume pressed until the first stable reading
  bool lastRaw_ = true;
  uint32_t rawSinceMs_ = 0;
};

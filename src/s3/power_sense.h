// Battery and motor-bus voltage sensing through resistor dividers (optional).
// When a divider is not fitted the value is reported as "not wired", never guessed.
#pragma once

#include <Arduino.h>

class PowerSense {
 public:
  void begin();
  void update(uint32_t nowMs);
  bool batteryWired() const;
  bool motorBusWired() const;
  bool batteryValid() const { return batteryWired() && battV_ > 1.0f; }
  float batteryV() const { return battV_; }
  int batteryPercent() const;  // rough linear estimate, -1 if unknown
  float motorBusV() const { return busV_; }
  // True/false only when the bus divider is fitted.
  bool motorBusPresent() const { return busV_ > 6.0f; }

 private:
  float battV_ = 0.0f;
  float busV_ = 0.0f;
  uint32_t lastMs_ = 0;
};

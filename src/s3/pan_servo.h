// SG90 camera-pan servo driven by a PCA9685 over I2C. The S3 never drives
// the servo signal from its own GPIO. There is no position feedback, so
// "settled" means: commanded target reached + SERVO_SETTLE_MS elapsed.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "rover_types.h"

class Pca9685 {
 public:
  bool begin(TwoWire& wire, uint8_t addr, uint32_t oscHz, float pwmHz);
  bool probe();
  bool setPulseUs(uint8_t channel, float us);
  bool setOff(uint8_t channel);
  float actualHz() const { return actualHz_; }
  uint8_t prescale() const { return prescale_; }

 private:
  bool write8(uint8_t reg, uint8_t value);
  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0x40;
  float actualHz_ = 0.0f;
  uint8_t prescale_ = 0;
};

class PanServo {
 public:
  void begin(TwoWire& wire);
  void update(uint32_t nowMs);
  // Clamped to the soft range. Returns false if the controller is absent.
  bool setTarget(float deg);
  // Holds the current position (used on E-stop / safe stop).
  void freeze();
  // Raw calibration pulse; bypasses the soft range but not SERVO_CAL_*_US.
  bool setRawPulse(float us);
  bool present() const { return present_; }
  PanState state() const { return present_ ? state_ : PanState::Absent; }
  float angle() const { return angle_; }
  float target() const { return target_; }
  float lastPulseUs() const { return pulseUs_; }
  uint32_t epoch() const { return epoch_; }  // increments at each new movement
  uint32_t i2cErrors() const { return errors_; }
  float pwmHz() const { return pca_.actualHz(); }

 private:
  bool writeAngle(float deg);
  Pca9685 pca_;
  TwoWire* wire_ = nullptr;
  bool present_ = false;
  PanState state_ = PanState::Absent;
  float angle_ = 90.0f;
  float target_ = 90.0f;
  float pulseUs_ = 0.0f;
  bool rawMode_ = false;
  uint32_t reachedMs_ = 0;
  uint32_t lastWriteMs_ = 0;
  uint32_t lastProbeMs_ = 0;
  uint32_t epoch_ = 0;
  uint32_t errors_ = 0;
};

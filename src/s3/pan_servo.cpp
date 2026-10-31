#include "pan_servo.h"

#include "config_hardware.h"
#include "config_tuning.h"
#include "events.h"

namespace {
constexpr uint8_t REG_MODE1 = 0x00;
constexpr uint8_t REG_MODE2 = 0x01;
constexpr uint8_t REG_LED0_ON_L = 0x06;
constexpr uint8_t REG_PRESCALE = 0xFE;
constexpr uint8_t MODE1_RESTART = 0x80;
constexpr uint8_t MODE1_AI = 0x20;
constexpr uint8_t MODE1_SLEEP = 0x10;
constexpr uint8_t MODE2_OUTDRV = 0x04;
constexpr uint32_t kReprobeMs = 2000;
constexpr uint32_t kMinWriteIntervalMs = 20;  // one update per servo frame
}  // namespace

// ------------------------------------------------------------------ PCA9685

bool Pca9685::write8(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Pca9685::probe() {
  wire_->beginTransmission(addr_);
  return wire_->endTransmission() == 0;
}

bool Pca9685::begin(TwoWire& wire, uint8_t addr, uint32_t oscHz, float pwmHz) {
  wire_ = &wire;
  addr_ = addr;
  if (!probe()) return false;
  const float pre = static_cast<float>(oscHz) / (4096.0f * pwmHz) - 1.0f;
  prescale_ = static_cast<uint8_t>(clampf(pre + 0.5f, 3.0f, 255.0f));
  actualHz_ = static_cast<float>(oscHz) / (4096.0f * (prescale_ + 1));
  // Prescale can only be written while the oscillator sleeps.
  bool ok = write8(REG_MODE1, MODE1_SLEEP | MODE1_AI);
  ok &= write8(REG_PRESCALE, prescale_);
  ok &= write8(REG_MODE2, MODE2_OUTDRV);
  ok &= write8(REG_MODE1, MODE1_AI);
  delay(1);  // oscillator start-up (500 us max)
  ok &= write8(REG_MODE1, MODE1_RESTART | MODE1_AI);
  return ok;
}

bool Pca9685::setPulseUs(uint8_t channel, float us) {
  const float periodUs = 1e6f / actualHz_;
  uint16_t off = static_cast<uint16_t>(clampf(us / periodUs * 4096.0f + 0.5f, 0.0f, 4095.0f));
  wire_->beginTransmission(addr_);
  wire_->write(REG_LED0_ON_L + 4 * channel);
  wire_->write(0x00);
  wire_->write(0x00);
  wire_->write(off & 0xFF);
  wire_->write(off >> 8);
  return wire_->endTransmission() == 0;
}

bool Pca9685::setOff(uint8_t channel) {
  wire_->beginTransmission(addr_);
  wire_->write(REG_LED0_ON_L + 4 * channel);
  wire_->write(0x00);
  wire_->write(0x00);
  wire_->write(0x00);
  wire_->write(0x10);  // FULL_OFF bit
  return wire_->endTransmission() == 0;
}

// ------------------------------------------------------------------ PanServo

void PanServo::begin(TwoWire& wire) {
  wire_ = &wire;
  if (!PCA9685_FITTED) return;
  present_ = pca_.begin(wire, PCA9685_I2C_ADDR, PCA9685_OSC_HZ, PCA9685_SERVO_HZ);
  lastProbeMs_ = millis();
  if (!present_) {
    g_events.add(EvLevel::Warn, "PCA9685 not found at 0x%02X (run 'i2cscan')", PCA9685_I2C_ADDR);
    return;
  }
  g_events.add(EvLevel::Info, "PCA9685 at 0x%02X, prescale %u, %.2f Hz", PCA9685_I2C_ADDR, pca_.prescale(),
               pca_.actualHz());
  angle_ = target_ = clampf(SERVO_HOME_DEG, SERVO_SOFT_MIN_DEG, SERVO_SOFT_MAX_DEG);
  writeAngle(angle_);
  state_ = PanState::Moving;  // unknown real position at power-up: let it settle
  reachedMs_ = millis();
  ++epoch_;
}

bool PanServo::writeAngle(float deg) {
  const float us = SERVO_US_AT_0_DEG + (SERVO_US_AT_180_DEG - SERVO_US_AT_0_DEG) * (deg / 180.0f);
  if (!pca_.setPulseUs(PAN_SERVO_CHANNEL, us)) {
    ++errors_;
    present_ = false;
    g_events.add(EvLevel::Error, "PCA9685 write failed; pan servo disabled");
    return false;
  }
  pulseUs_ = us;
  return true;
}

bool PanServo::setTarget(float deg) {
  if (!present_) return false;
  rawMode_ = false;
  const float t = clampf(deg, SERVO_SOFT_MIN_DEG, SERVO_SOFT_MAX_DEG);
  if (fabsf(t - target_) < 0.5f && state_ != PanState::Absent) return true;
  target_ = t;
  if (state_ != PanState::Moving) ++epoch_;
  state_ = PanState::Moving;
  return true;
}

void PanServo::freeze() {
  target_ = angle_;
}

bool PanServo::setRawPulse(float us) {
  if (!present_) return false;
  us = clampf(us, SERVO_CAL_MIN_US, SERVO_CAL_MAX_US);
  if (!pca_.setPulseUs(PAN_SERVO_CHANNEL, us)) {
    ++errors_;
    return false;
  }
  rawMode_ = true;
  pulseUs_ = us;
  ++epoch_;
  state_ = PanState::Moving;
  reachedMs_ = millis();
  // Keep angle/target consistent with the nominal mapping for display.
  angle_ = target_ = (us - SERVO_US_AT_0_DEG) * 180.0f / (SERVO_US_AT_180_DEG - SERVO_US_AT_0_DEG);
  return true;
}

void PanServo::update(uint32_t nowMs) {
  if (!PCA9685_FITTED) return;
  if (!present_) {
    if (nowMs - lastProbeMs_ >= kReprobeMs) {
      lastProbeMs_ = nowMs;
      if (pca_.begin(*wire_, PCA9685_I2C_ADDR, PCA9685_OSC_HZ, PCA9685_SERVO_HZ)) {
        present_ = true;
        g_events.add(EvLevel::Info, "PCA9685 found again at 0x%02X", PCA9685_I2C_ADDR);
        target_ = angle_;
        writeAngle(angle_);
        state_ = PanState::Moving;
        reachedMs_ = nowMs;
        ++epoch_;
      }
    }
    return;
  }
  if (state_ != PanState::Moving) return;
  if (!rawMode_ && fabsf(target_ - angle_) > 0.01f) {
    const uint32_t sinceWrite = nowMs - lastWriteMs_;
    if (sinceWrite < kMinWriteIntervalMs) return;
    const float maxStep = SERVO_MAX_DEG_PER_S * static_cast<float>(sinceWrite > 100 ? 100 : sinceWrite) / 1000.0f;
    const float step = clampf(target_ - angle_, -maxStep, maxStep);
    angle_ += step;
    lastWriteMs_ = nowMs;
    if (!writeAngle(angle_)) return;
    reachedMs_ = nowMs;
    return;
  }
  if (nowMs - reachedMs_ >= SERVO_SETTLE_MS) state_ = PanState::Settled;
}

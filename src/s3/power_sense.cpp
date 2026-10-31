#include "power_sense.h"

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "rover_types.h"

namespace {
float readVolts(int pin, float ratio, float gain) {
  uint32_t sum = 0;
  for (int i = 0; i < 8; ++i) sum += analogReadMilliVolts(pin);  // eFuse-calibrated
  return (static_cast<float>(sum) / 8.0f / 1000.0f) * ratio * gain;
}
}  // namespace

void PowerSense::begin() {
  if (BATTERY_SENSE_FITTED) analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
  if (MOTOR_BUS_SENSE_FITTED) analogSetPinAttenuation(PIN_MOTOR_BUS_ADC, ADC_11db);
}

bool PowerSense::batteryWired() const { return BATTERY_SENSE_FITTED; }
bool PowerSense::motorBusWired() const { return MOTOR_BUS_SENSE_FITTED; }

void PowerSense::update(uint32_t nowMs) {
  if (nowMs - lastMs_ < BATTERY_SAMPLE_MS) return;
  lastMs_ = nowMs;
  if (BATTERY_SENSE_FITTED) {
    const float v = readVolts(PIN_BATT_ADC, BATTERY_DIVIDER_RATIO, BATTERY_CAL_GAIN);
    battV_ = (battV_ < 0.5f) ? v : battV_ * 0.7f + v * 0.3f;
  }
  if (MOTOR_BUS_SENSE_FITTED) {
    busV_ = readVolts(PIN_MOTOR_BUS_ADC, MOTOR_BUS_DIVIDER_RATIO, MOTOR_BUS_CAL_GAIN);
  }
}

int PowerSense::batteryPercent() const {
  if (!batteryValid()) return -1;
  const float f = (battV_ - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V);
  return static_cast<int>(clampf(f, 0.0f, 1.0f) * 100.0f + 0.5f);
}

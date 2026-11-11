#include "motors.h"

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "rover_types.h"

namespace {
constexpr uint8_t CH_LEFT_RPWM = 0;
constexpr uint8_t CH_LEFT_LPWM = 1;
constexpr uint8_t CH_RIGHT_RPWM = 2;
constexpr uint8_t CH_RIGHT_LPWM = 3;
constexpr uint32_t kMaxDuty = (1u << MOTOR_PWM_BITS) - 1;

uint32_t toCounts(float magnitude) {
  return static_cast<uint32_t>(clampf(magnitude, 0.0f, 1.0f) * kMaxDuty + 0.5f);
}
}  // namespace

void MotorOutputs::begin() {
  // Drive every input LOW before attaching PWM so nothing twitches at boot.
  // Hardware: 10k pull-downs on every driver input keep them LOW while the
  // S3 is in reset or unpowered (see docs/03_electrical.md).
  const int pins[] = {PIN_DRIVER_EN, PIN_LEFT_RPWM, PIN_LEFT_LPWM, PIN_RIGHT_RPWM, PIN_RIGHT_LPWM};
  for (int p : pins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  const struct {
    int pin;
    uint8_t ch;
  } pwm[] = {{PIN_LEFT_RPWM, CH_LEFT_RPWM},
             {PIN_LEFT_LPWM, CH_LEFT_LPWM},
             {PIN_RIGHT_RPWM, CH_RIGHT_RPWM},
             {PIN_RIGHT_LPWM, CH_RIGHT_LPWM}};
  for (const auto& o : pwm) {
    ledcSetup(o.ch, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
    ledcAttachPin(o.pin, o.ch);
  }
  hardStop();
}

void MotorOutputs::hardStop() {
  // EN low first: it switches off all four half-bridges at once (motors coast).
  digitalWrite(PIN_DRIVER_EN, LOW);
  ledcWrite(CH_LEFT_RPWM, 0);
  ledcWrite(CH_LEFT_LPWM, 0);
  ledcWrite(CH_RIGHT_RPWM, 0);
  ledcWrite(CH_RIGHT_LPWM, 0);
  left_ = 0.0f;
  right_ = 0.0f;
}

void MotorOutputs::write(float left, float right) {
  left = clampf(left, -1.0f, 1.0f);
  right = clampf(right, -1.0f, 1.0f);
  left_ = left;
  right_ = right;
  // EN is high only while a side is driven. With EN high and both inputs LOW
  // a module brakes its side (both low-side switches on).
  digitalWrite(PIN_DRIVER_EN, (left != 0.0f || right != 0.0f) ? HIGH : LOW);
  writeSide(CH_LEFT_RPWM, CH_LEFT_LPWM, MOTOR_LEFT_INVERT ? -left : left);
  writeSide(CH_RIGHT_RPWM, CH_RIGHT_LPWM, MOTOR_RIGHT_INVERT ? -right : right);
}

void MotorOutputs::writeSide(uint8_t chForward, uint8_t chReverse, float duty) {
  // RPWM carries forward duty, LPWM reverse duty. The idle input is zeroed
  // first, so the two inputs are never driven at the same time.
  const uint32_t counts = toCounts(fabsf(duty));
  if (duty >= 0.0f) {
    ledcWrite(chReverse, 0);
    ledcWrite(chForward, counts);
  } else {
    ledcWrite(chForward, 0);
    ledcWrite(chReverse, counts);
  }
}

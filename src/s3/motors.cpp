#include "motors.h"

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "rover_types.h"

namespace {
constexpr uint8_t CH_LEFT_A = 0;
constexpr uint8_t CH_LEFT_B = 1;
constexpr uint8_t CH_RIGHT_A = 2;
constexpr uint8_t CH_RIGHT_B = 3;
constexpr uint32_t kMaxDuty = (1u << MOTOR_PWM_BITS) - 1;

uint32_t toCounts(float magnitude) {
  return static_cast<uint32_t>(clampf(magnitude, 0.0f, 1.0f) * kMaxDuty + 0.5f);
}
}  // namespace

void MotorOutputs::begin() {
  // Drive every input LOW before attaching PWM so nothing twitches at boot.
  // Hardware: 10k pull-downs on every driver input keep them LOW while the
  // S3 is in reset or unpowered (see docs/03_electrical.md).
  const int pins[] = {PIN_LEFT_A, PIN_LEFT_B, PIN_RIGHT_A, PIN_RIGHT_B};
  for (int p : pins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  ledcSetup(CH_LEFT_A, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  ledcSetup(CH_RIGHT_A, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  ledcAttachPin(PIN_LEFT_A, CH_LEFT_A);
  ledcAttachPin(PIN_RIGHT_A, CH_RIGHT_A);
#if MOTOR_DRIVER_TYPE == DRIVER_BTS7960
  ledcSetup(CH_LEFT_B, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  ledcSetup(CH_RIGHT_B, MOTOR_PWM_HZ, MOTOR_PWM_BITS);
  ledcAttachPin(PIN_LEFT_B, CH_LEFT_B);
  ledcAttachPin(PIN_RIGHT_B, CH_RIGHT_B);
  pinMode(PIN_DRIVER_EN, OUTPUT);
  digitalWrite(PIN_DRIVER_EN, LOW);
#endif
  hardStop();
}

const char* MotorOutputs::driverName() const {
#if MOTOR_DRIVER_TYPE == DRIVER_BTS7960
  return "BTS7960";
#else
  return "MD30C";
#endif
}

void MotorOutputs::hardStop() {
  ledcWrite(CH_LEFT_A, 0);
  ledcWrite(CH_RIGHT_A, 0);
#if MOTOR_DRIVER_TYPE == DRIVER_BTS7960
  ledcWrite(CH_LEFT_B, 0);
  ledcWrite(CH_RIGHT_B, 0);
  digitalWrite(PIN_DRIVER_EN, LOW);
#endif
  left_ = 0.0f;
  right_ = 0.0f;
}

void MotorOutputs::write(float left, float right) {
  left = clampf(left, -1.0f, 1.0f);
  right = clampf(right, -1.0f, 1.0f);
  left_ = left;
  right_ = right;
#if MOTOR_DRIVER_TYPE == DRIVER_BTS7960
  digitalWrite(PIN_DRIVER_EN, (left != 0.0f || right != 0.0f) ? HIGH : LOW);
#endif
  writeSide(PIN_LEFT_A, PIN_LEFT_B, CH_LEFT_A, CH_LEFT_B, MOTOR_LEFT_INVERT ? -left : left);
  writeSide(PIN_RIGHT_A, PIN_RIGHT_B, CH_RIGHT_A, CH_RIGHT_B, MOTOR_RIGHT_INVERT ? -right : right);
}

void MotorOutputs::writeSide(int pinA, int pinB, uint8_t chA, uint8_t chB, float duty) {
  const uint32_t counts = toCounts(fabsf(duty));
#if MOTOR_DRIVER_TYPE == DRIVER_BTS7960
  (void)pinA;
  (void)pinB;
  if (duty >= 0.0f) {
    ledcWrite(chB, 0);
    ledcWrite(chA, counts);
  } else {
    ledcWrite(chA, 0);
    ledcWrite(chB, counts);
  }
#else
  // MD30C sign-magnitude: DIR selects direction, PWM duty is the magnitude.
  (void)pinA;
  (void)chB;
  digitalWrite(pinB, duty < 0.0f ? HIGH : LOW);
  ledcWrite(chA, counts);
#endif
}

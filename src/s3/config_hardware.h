// ESP32-S3 main controller — what is fitted and how it is wired.
// Change these to match the physical build; every flag is reported on
// /api/status so the dashboard shows what the firmware believes is fitted.
#pragma once

#include <stdint.h>

// ---- Motor driver type -----------------------------------------------------
// Baseline: two Cytron MD30C (single channel, PWM + DIR, sign-magnitude).
// Alternative listed in the team's component notes: two BTS7960 (IBT-2)
// modules (RPWM + LPWM + EN). Select exactly one.
#define DRIVER_MD30C 1
#define DRIVER_BTS7960 2
#ifndef MOTOR_DRIVER_TYPE
#define MOTOR_DRIVER_TYPE DRIVER_MD30C
#endif

// Flip a side if "forward" spins that side backwards during the raised-wheel test.
constexpr bool MOTOR_LEFT_INVERT = false;
constexpr bool MOTOR_RIGHT_INVERT = false;  // set during docs/06 stage 8 (wheels raised)

// ---- Sonars (index: 0 front-center, 1 front-left, 2 front-right, 3 rear) ----
// The user currently owns ONE HC-SR04. Fit it as front-center first.
// A sonar that is not fitted reports "not_fitted"; the rear one being absent
// means reverse is always blocked (fail-safe interlock).
constexpr bool SONAR_FITTED[4] = {true, false, false, false};
constexpr const char* SONAR_NAME[4] = {"front_center", "front_left", "front_right", "rear"};
// Distance from each sensor face to the rover's outer edge on that side (mm).
// Positive when the sensor face sits BEHIND the bumper (reading is longer than
// the bumper-to-wall gap). Measure with a ruler; see docs/06 stage 4.
constexpr int SONAR_FACE_TO_BUMPER_MM[4] = {0, 0, 0, 0};
// Per-sensor scale correction after calibration against a ruler (1.0 = none).
constexpr float SONAR_SCALE[4] = {1.0f, 1.0f, 1.0f, 1.0f};

// ---- Hardware E-stop sense ---------------------------------------------------
// true: PIN_ESTOP_SENSE is wired to the second pole of the E-stop switch.
// If true and the wire is missing, the firmware stays in ESTOP (fail-safe).
constexpr bool ESTOP_SENSE_FITTED = true;

// ---- Voltage sensing ---------------------------------------------------------
// Set to true only after the divider is soldered AND calibrated (docs/06).
constexpr bool BATTERY_SENSE_FITTED = false;
constexpr bool MOTOR_BUS_SENSE_FITTED = false;
// Divider ratio Vin/Vadc for 100k (top) / 22k (bottom): (100+22)/22.
constexpr float BATTERY_DIVIDER_RATIO = 122.0f / 22.0f;
constexpr float MOTOR_BUS_DIVIDER_RATIO = 122.0f / 22.0f;
// Multiply by this after comparing against a multimeter (1.0 = uncalibrated).
constexpr float BATTERY_CAL_GAIN = 1.0f;
constexpr float MOTOR_BUS_CAL_GAIN = 1.0f;

// ---- Battery chemistry (3S Li-ion baseline) -----------------------------------
constexpr float BATTERY_FULL_V = 12.6f;
constexpr float BATTERY_EMPTY_V = 9.6f;   // conservative; BMS cut-off is lower
constexpr float BATTERY_WARN_V = 10.5f;

// ---- Motor rating (Pololu 37D 70:1 12 V #4744 baseline) -----------------------
// A full 3S pack is 12.6 V. When the battery voltage is known the PWM duty is
// capped so the average motor voltage stays at or below this rating.
constexpr float MOTOR_RATED_V = 12.0f;
// Duty cap used when battery voltage is NOT measured (12.0 / 12.6).
constexpr float MOTOR_DUTY_CAP_NO_VSENSE = 0.95f;

// The camera-pan servo (MG996R) is wired to the ESP32-CAM, not to the S3
// (src/cam/config_cam.h). The S3 only picks its mode over the camera link.

// ---- Hardware the project does NOT have (documented, not faked) --------------
// No wheel encoders and no lane-end marker sensor: traverse completion can not
// be verified on board, so the traverse mission ends as FAILED, never COMPLETE.
constexpr bool LANE_END_SENSOR_FITTED = false;

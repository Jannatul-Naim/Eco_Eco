// ESP32-S3 main controller — what is fitted and how it is wired.
// Change these to match the physical build; every flag is reported on
// /api/status so the dashboard shows what the firmware believes is fitted.
#pragma once

#include <stdint.h>

// ---- Motor driver -------------------------------------------------------------
// Two BTS7960 (IBT-2) modules, one per side (team decision, 11 Nov 2026):
// RPWM + LPWM per module, one shared EN. Wiring: docs/03_electrical.md §3.5.
constexpr const char* MOTOR_DRIVER_NAME = "BTS7960";

// Flip a side if "forward" spins that side backwards during the raised-wheel test.
constexpr bool MOTOR_LEFT_INVERT = false;
constexpr bool MOTOR_RIGHT_INVERT = false;  // set during docs/06 stage 8 (wheels raised)

// ---- Sonars (index: 0 front-center, 1 front-left, 2 front-right, 3 rear) ----
// The team list has four HC-SR04 (13 Nov 2026); only front-center is wired so far.
// Set each one to true once it is wired (docs/06 stage 4).
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
// The firmware must see the hardware E-stop, so that switching it back ON never
// restarts motion. Two ways; pick the one that matches the switch:
//  * ESTOP_SENSE_FITTED: a 2-pole switch whose second pole goes to PIN_ESTOP_SENSE.
//    A missing wire reads as OFF (fail-safe).
//  * ESTOP_FROM_MOTOR_BUS: a 1-pole switch in the motor supply line. No voltage on
//    PIN_MOTOR_BUS_ADC means the switch is OFF. Needs the motor-bus divider.
// The team's switches are 1-pole (13 Nov 2026), so the motor bus is the sense.
constexpr bool ESTOP_SENSE_FITTED = false;
constexpr bool ESTOP_FROM_MOTOR_BUS = true;

// ---- Voltage sensing ---------------------------------------------------------
// Battery = the logic pack (BT2). Set to true only after its divider is soldered
// AND calibrated (docs/06). Motor bus = after the E-stop switch; required while
// ESTOP_FROM_MOTOR_BUS is true. Without its divider GPIO2 floats: on the bench,
// tie GPIO2 to 3V3 (reads as "present") and connect nothing that moves.
constexpr bool BATTERY_SENSE_FITTED = false;
constexpr bool MOTOR_BUS_SENSE_FITTED = true;
static_assert(!ESTOP_FROM_MOTOR_BUS || MOTOR_BUS_SENSE_FITTED, "ESTOP_FROM_MOTOR_BUS needs the motor-bus divider");
// Divider ratio Vin/Vadc for 100k (top) / 22k (bottom): (100+22)/22.
constexpr float BATTERY_DIVIDER_RATIO = 122.0f / 22.0f;
constexpr float MOTOR_BUS_DIVIDER_RATIO = 122.0f / 22.0f;
// Multiply by this after comparing against a multimeter (1.0 = uncalibrated).
constexpr float BATTERY_CAL_GAIN = 1.0f;
constexpr float MOTOR_BUS_CAL_GAIN = 1.0f;

// ---- Batteries: two 3S LiPo packs, 850 mAh (team list, 13 Nov 2026) -----------
// BT1 feeds the motors and servo through the E-stop switch; BT2 feeds the logic.
// A bare LiPo has no BMS: never run a pack below 3.3 V per cell.
constexpr float BATTERY_FULL_V = 12.6f;
constexpr float BATTERY_EMPTY_V = 9.9f;  // 3.3 V per cell
constexpr float BATTERY_WARN_V = 10.5f;  // 3.5 V per cell: stop and charge

// ---- Motor rating: 25GA 300 RPM, 12 V (team list, 13 Nov 2026) -----------------
// A full 3S pack is 12.6 V. While the motor-bus voltage is measured the PWM duty
// is capped so the average motor voltage stays at or below this rating.
constexpr float MOTOR_RATED_V = 12.0f;
// Duty cap used when the motor supply is NOT measured (12.0 / 12.6).
constexpr float MOTOR_DUTY_CAP_NO_VSENSE = 0.95f;

// The camera-pan servo (SG90) is wired to the ESP32-CAM, not to the S3
// (src/cam/config_cam.h). The S3 only picks its mode over the camera link.

// ---- Hardware the project does NOT have (documented, not faked) --------------
// No wheel encoders and no lane-end marker sensor: traverse completion can not
// be verified on board, so the traverse mission ends as FAILED, never COMPLETE.
constexpr bool LANE_END_SENSOR_FITTED = false;

// ESP32-S3 main controller — thresholds, timing and mission parameters.
// All distances are millimetres measured from the rover's outer edge
// (after SONAR_FACE_TO_BUMPER_MM is applied). All times are milliseconds.
#pragma once

#include <stdint.h>

// ---- Control loop -----------------------------------------------------------
constexpr uint32_t CONTROL_PERIOD_MS = 10;
constexpr uint32_t CONTROL_WDT_TIMEOUT_S = 3;  // task watchdog -> reboot -> outputs off

// ---- Motor output -----------------------------------------------------------
constexpr uint32_t MOTOR_PWM_HZ = 20000;  // MD30C and BTS7960 both accept 20 kHz (verify datasheet)
constexpr uint8_t MOTOR_PWM_BITS = 10;
constexpr float MOTOR_ACCEL_PER_S = 2.5f;     // full scale per second (0 -> 100 % in 400 ms)
constexpr float MOTOR_DECEL_PER_S = 6.0f;     // slowing down is allowed to be faster
constexpr float MOTOR_MIN_EFFECTIVE = 0.03f;  // below this the output is forced to 0
constexpr float TELEOP_MAX_DUTY = 0.60f;      // hard firmware cap in teleoperation
constexpr uint32_t TELEOP_CMD_TIMEOUT_MS = 300;  // no fresh drive command -> stop

// ---- Sonar ------------------------------------------------------------------
constexpr float SOUND_SPEED_M_S = 346.0f;     // ~25 C; 343 at 20 C, 349 at 30 C
constexpr uint32_t SONAR_SLOT_MS = 40;        // one sensor per slot, never two at once
constexpr uint32_t SONAR_MIN_RETRIGGER_MS = 60;  // same sensor is not re-pinged sooner
constexpr uint32_t SONAR_ECHO_START_TIMEOUT_MS = 8;  // echo never rose -> sensor fault
constexpr uint32_t SONAR_MAX_ECHO_US = 25000;        // ~4.3 m; longer = out of range
constexpr uint32_t SONAR_STALE_MS = 400;      // older readings are not trusted
// Schedule: front-center is sampled every other slot for the proximity task.
constexpr uint8_t SONAR_SCHEDULE[] = {0, 1, 0, 2, 0, 3};

// ---- Rear reverse interlock ---------------------------------------------------
constexpr int REAR_CLEARANCE_MM = 200;  // reverse blocked below this (or if stale/invalid)

// ---- Autonomous: traverse ----------------------------------------------------
constexpr float TRAVERSE_CRUISE_DUTY = 0.35f;
constexpr float TRAVERSE_STEER_GAIN = 0.25f;    // max differential added by side sonars
constexpr int TRAVERSE_SIDE_CLEAR_MM = 250;     // side sonar closer than this -> steer away
constexpr int TRAVERSE_FRONT_BLOCK_MM = 250;    // front-center closer than this -> BLOCKED
constexpr uint32_t TRAVERSE_BLOCKED_WAIT_MS = 3000;  // wait for path to clear, then FAILED
constexpr uint32_t TRAVERSE_TIMEOUT_MS = 30000;      // no end sensor -> FAILED at timeout

// ---- Autonomous: sonar proximity stop (rulebook: 100 +/- 20 mm) -------------
constexpr int PROX_TARGET_MM = 100;
constexpr int PROX_TOLERANCE_MM = 20;        // reported window
constexpr int PROX_INNER_TOLERANCE_MM = 8;   // controller aims inside this
constexpr float PROX_MAX_DUTY = 0.30f;
constexpr float PROX_MIN_DUTY = 0.12f;       // below this the motors may not move; tune on the floor
constexpr float PROX_GAIN_PER_MM = 0.0015f;  // duty per mm of error
constexpr uint8_t PROX_STABLE_READINGS = 5;  // consecutive in-window readings while stopped
constexpr uint32_t PROX_TIMEOUT_MS = 20000;

// Camera pan: the servo, its scan positions and target following live on the
// ESP32-CAM (src/cam/config_cam.h); the shared angle limits are in
// lib/rover_common/src/rover_protocol.h.

// ---- Camera link ------------------------------------------------------------
constexpr uint32_t CAM_LINK_STALE_MS = 1000;
constexpr uint32_t CAM_CFG_PERIOD_MS = 200;

// ---- Autonomous / teleop-triggered: QR decode + ping ---------------------------
constexpr uint32_t QR_TIMEOUT_MS = 30000;
constexpr uint32_t PING_COUNT = 1;          // rulebook: one logged echo request is enough
constexpr uint32_t PING_TIMEOUT_MS = 1500;

// ---- Autonomous: motion tracking ----------------------------------------------
constexpr uint32_t MOTION_GOAL_MS = 5000;     // rulebook: track for 5 s
// Longest gap between detections still counted as continuous. Must exceed one
// recentring move on the camera: slew + settle + ~2 frames (see
// PAN_TRACK_LOST_MS in src/cam/config_cam.h).
constexpr uint32_t MOTION_MAX_GAP_MS = 1000;
constexpr uint32_t MOTION_TIMEOUT_MS = 60000;

// ---- Telemetry / events --------------------------------------------------------
constexpr uint32_t TELEMETRY_PERIOD_MS = 75;  // rulebook full marks need median <= 100 ms
constexpr uint32_t BATTERY_SAMPLE_MS = 200;

// Shared enums and plain data structs for the S3 firmware.
#pragma once

#include <stdint.h>
#include <string.h>

constexpr const char* FW_VERSION = "s3-main 0.1.0";

enum class Mode : uint8_t { Idle, Teleop, Auto };

enum class State : uint8_t {
  Boot,
  EStop,        // latched; needs hardware release + explicit reset
  Idle,         // outputs disabled
  Teleop,       // outputs follow fresh operator commands only
  AutoReady,    // mission selected, controls locked, waiting for START
  AutoRunning,
  AutoBlocked,  // mission paused: path blocked
  AutoComplete,
  AutoFailed,
  SafeStop,     // fault (stale sensor / lost link); needs acknowledgement
};

enum class Mission : uint8_t { None, Traverse, Proximity, QrPing, Motion };

enum class CmdSource : uint8_t { Http, WebSocket, Serial, Internal };

enum class SonarStatus : uint8_t { NotFitted, NoData, Ok, OutOfRange, NoEcho, StuckHigh };

enum class PanState : uint8_t { Absent, Moving, Settled };

enum class PingState : uint8_t { Idle, Running, Reply, Timeout, Error };

struct SonarReading {
  SonarStatus status = SonarStatus::NoData;
  int mm = -1;          // bumper-referenced distance, -1 when not applicable
  uint32_t tMs = 0;     // time of the measurement
};

struct CmdResult {
  bool ok;
  int http;             // 200, 400, 409, 503
  const char* message;
  static CmdResult accepted(const char* m = "ok") { return {true, 200, m}; }
  static CmdResult bad(const char* m) { return {false, 400, m}; }
  static CmdResult conflict(const char* m) { return {false, 409, m}; }
};

inline const char* modeName(Mode m) {
  switch (m) {
    case Mode::Idle: return "idle";
    case Mode::Teleop: return "teleop";
    case Mode::Auto: return "auto";
  }
  return "?";
}

inline bool parseMode(const char* s, Mode* out) {
  if (!s) return false;
  if (!strcmp(s, "idle")) { *out = Mode::Idle; return true; }
  if (!strcmp(s, "teleop")) { *out = Mode::Teleop; return true; }
  if (!strcmp(s, "auto")) { *out = Mode::Auto; return true; }
  return false;
}

inline const char* stateName(State s) {
  switch (s) {
    case State::Boot: return "BOOT";
    case State::EStop: return "ESTOP";
    case State::Idle: return "IDLE";
    case State::Teleop: return "TELEOP";
    case State::AutoReady: return "AUTO_READY";
    case State::AutoRunning: return "AUTO_RUNNING";
    case State::AutoBlocked: return "AUTO_BLOCKED";
    case State::AutoComplete: return "AUTO_COMPLETE";
    case State::AutoFailed: return "AUTO_FAILED";
    case State::SafeStop: return "SAFE_STOP";
  }
  return "?";
}

inline const char* missionName(Mission m) {
  switch (m) {
    case Mission::None: return "none";
    case Mission::Traverse: return "traverse";
    case Mission::Proximity: return "proximity";
    case Mission::QrPing: return "qr_ping";
    case Mission::Motion: return "motion";
  }
  return "?";
}

inline bool parseMission(const char* s, Mission* out) {
  if (!s) return false;
  if (!strcmp(s, "none")) { *out = Mission::None; return true; }
  if (!strcmp(s, "traverse")) { *out = Mission::Traverse; return true; }
  if (!strcmp(s, "proximity")) { *out = Mission::Proximity; return true; }
  if (!strcmp(s, "qr_ping")) { *out = Mission::QrPing; return true; }
  if (!strcmp(s, "motion")) { *out = Mission::Motion; return true; }
  return false;
}

inline const char* sourceName(CmdSource s) {
  switch (s) {
    case CmdSource::Http: return "http";
    case CmdSource::WebSocket: return "ws";
    case CmdSource::Serial: return "serial";
    case CmdSource::Internal: return "internal";
  }
  return "?";
}

inline const char* sonarStatusName(SonarStatus s) {
  switch (s) {
    case SonarStatus::NotFitted: return "not_fitted";
    case SonarStatus::NoData: return "no_data";
    case SonarStatus::Ok: return "ok";
    case SonarStatus::OutOfRange: return "out_of_range";
    case SonarStatus::NoEcho: return "no_echo";
    case SonarStatus::StuckHigh: return "echo_stuck_high";
  }
  return "?";
}

inline const char* panStateName(PanState s) {
  switch (s) {
    case PanState::Absent: return "absent";
    case PanState::Moving: return "moving";
    case PanState::Settled: return "settled";
  }
  return "?";
}

inline const char* pingStateName(PingState s) {
  switch (s) {
    case PingState::Idle: return "idle";
    case PingState::Running: return "running";
    case PingState::Reply: return "reply";
    case PingState::Timeout: return "timeout";
    case PingState::Error: return "error";
  }
  return "?";
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

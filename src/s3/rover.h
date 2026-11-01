// The rover state machine. This is the ONLY code that decides motor output.
//
// Threading: every public method must be called with the rover lock held
// (see RoverLock). tick() runs in the control task every CONTROL_PERIOD_MS;
// command methods are called from the web server, WebSocket and serial
// console. Camera/audio boards have no path into this class except the
// read-only observation structs owned by CamLink.
#pragma once

#include <Arduino.h>

#include <atomic>

#include "cam_link.h"
#include "estop_input.h"
#include "motors.h"
#include "pinger.h"
#include "power_sense.h"
#include "rover_types.h"
#include "sonar.h"

// Everything the dashboard/API shows, copied under the lock.
struct RoverSnapshot {
  uint32_t nowMs;
  Mode mode;
  State state;
  Mission mission;
  uint32_t modeChangedMs;
  uint32_t stateChangedMs;
  char detail[96];
  // E-stop
  bool estopActive;
  bool estopHwPressed;
  const char* estopSense;
  const char* estopSource;
  // Drive
  bool motorsArmed;
  float outLeft, outRight;
  bool reverseAllowed;
  const char* reverseReason;
  // Power
  bool battWired;
  float battV;
  int battPct;
  bool busWired;
  float busV;
  // Sensors
  SonarReading sonar[4];
  bool sonarFitted[4];
  int frontFilteredMm;
  // Camera and its pan servo (pan fields are in cam.pan*)
  CamState cam;
  const char* camLink;
  bool panPresent;  // camera link fresh and it reports a fitted servo
  // QR + ping
  bool qrRequested;
  PingResult ping;
  char lastValidQrIp[16];
  uint32_t lastValidQrMs;
  // Motion
  bool motionRequested;
  bool motionTracking;
  uint32_t motionTrackedMs;
  uint32_t motionLastDetMs;
  // Proximity
  bool proxInWindow;
  int proxMeasuredMm;
  uint32_t missionStartMs;
};

class Rover {
 public:
  void begin();
  void tick(uint32_t nowMs);

  // ---- commands (lock held). Each returns an HTTP-friendly result.
  CmdResult setMode(Mode m, Mission mission, bool missionGiven, CmdSource src);
  CmdResult startMission(CmdSource src);
  CmdResult abortMission(CmdSource src);
  CmdResult drive(float left, float right, CmdSource src);
  CmdResult engageEstop(CmdSource src);
  CmdResult resetEstop(CmdSource src);
  CmdResult ackFault(CmdSource src);
  CmdResult setPan(float deg, CmdSource src);
  CmdResult setPanPulse(float us, CmdSource src);
  CmdResult triggerQrPing(CmdSource src);   // teleop-triggered decode + on-board ping
  CmdResult setMotionDetect(bool on, CmdSource src);

  void snapshot(RoverSnapshot* out) const;
  // Immediate output cut + latch request; safe from any task WITHOUT the lock.
  // The control task latches ESTOP on its next tick.
  void requestEstop() {
    motors_.hardStop();
    softEstopPending_.store(true);
  }

  SonarArray& sonar() { return sonar_; }

 private:
  enum class QrPhase : uint8_t { Off, Scanning, Pinging, Done };

  void setState(State s, uint32_t nowMs, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
  void enterEstop(const char* source, const char* why, uint32_t nowMs);
  void enterSafeStop(const char* why, uint32_t nowMs);
  void finishMission(bool success, uint32_t nowMs, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
  void resetMissionState(uint32_t nowMs);
  bool reverseAllowed(uint32_t nowMs, const char** reason) const;
  bool motorPowerAbsent() const;

  void runTraverse(uint32_t nowMs, float* l, float* r);
  void runProximity(uint32_t nowMs, float* l, float* r);
  void runQrPing(uint32_t nowMs, bool missionMode);
  void runMotion(uint32_t nowMs);
  CamRequest camRequest();
  void applyDrive(float l, float r, bool armed, float cap, uint32_t nowMs, float dt);

  EStopInput estop_;
  MotorOutputs motors_;
  SonarArray sonar_;
  PowerSense power_;
  CamLink cam_;
  Pinger pinger_;

  Mode mode_ = Mode::Idle;
  State state_ = State::Boot;
  Mission mission_ = Mission::None;
  uint32_t modeChangedMs_ = 0;
  uint32_t stateChangedMs_ = 0;
  char detail_[96] = "booting";

  std::atomic<bool> softEstopPending_{false};
  bool estopLatched_ = false;
  const char* estopSource_ = "none";

  // Teleop
  float teleL_ = 0.0f, teleR_ = 0.0f;
  uint32_t teleCmdMs_ = 0;
  bool teleTimedOut_ = true;

  // Output shaping
  float outL_ = 0.0f, outR_ = 0.0f;
  bool armed_ = false;
  uint32_t lastTickMs_ = 0;
  uint32_t lastReverseEventMs_ = 0;
  uint32_t lastDriveRejectEventMs_ = 0;

  // Missions
  uint32_t missionStartMs_ = 0;
  uint32_t blockedSinceMs_ = 0;
  uint8_t proxStable_ = 0;
  uint32_t proxLastReadingMs_ = 0;
  bool proxInWindow_ = false;
  int proxMeasuredMm_ = -1;

  QrPhase qrPhase_ = QrPhase::Off;
  bool qrTeleop_ = false;        // teleop-triggered decode + ping
  uint32_t qrSeqAtStart_ = 0;
  uint32_t qrStartMs_ = 0;
  char qrIp_[16] = "";
  char lastValidQrIp_[16] = "";
  uint32_t lastValidQrMs_ = 0;

  bool motionTeleop_ = false;
  uint32_t motSeqSeen_ = 0;
  uint32_t trackStartMs_ = 0;
  uint32_t lastDetMs_ = 0;

  // Manual pan (IDLE/TELEOP). Scanning and tracking run on the camera.
  float panManualDeg_ = boardlink::kPanHomeDeg;
  bool panPulseMode_ = false;  // raw calibration pulse, IDLE only
  float panPulseUs_ = 0.0f;
};

// Global lock guarding the Rover instance.
class RoverLock {
 public:
  explicit RoverLock(uint32_t timeoutMs);
  ~RoverLock();
  bool ok() const { return ok_; }
  static void init();

 private:
  bool ok_;
};

extern Rover g_rover;

#include "rover.h"

#include <stdarg.h>

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "events.h"
#include "ipv4.h"

Rover g_rover;
TwoWire& g_i2c = Wire;

namespace {
SemaphoreHandle_t s_lock = nullptr;
constexpr size_t kScanCount = sizeof(PAN_SCAN_DEG) / sizeof(PAN_SCAN_DEG[0]);
constexpr int FC = SonarArray::kFrontCenter;
constexpr int FL = SonarArray::kFrontLeft;
constexpr int FR = SonarArray::kFrontRight;
constexpr int REAR = SonarArray::kRear;
// Armed when POST /api/mode {"mode":"auto"} names no mission and none was chosen
// before (the rulebook's contract body has no mission field). qr_ping never
// turns the wheels, and arming still waits for an explicit START.
constexpr Mission kDefaultAutoMission = Mission::QrPing;

bool autoActive(State s) { return s == State::AutoRunning || s == State::AutoBlocked; }

EvLevel levelFor(State s) {
  switch (s) {
    case State::EStop:
    case State::SafeStop: return EvLevel::Error;
    case State::AutoFailed:
    case State::AutoBlocked: return EvLevel::Warn;
    default: return EvLevel::Info;
  }
}

float slewToward(float current, float target, float dt) {
  const bool slowing = fabsf(target) < fabsf(current) || target * current < 0.0f;
  const float step = (slowing ? MOTOR_DECEL_PER_S : MOTOR_ACCEL_PER_S) * dt;
  return current + clampf(target - current, -step, step);
}
}  // namespace

// ------------------------------------------------------------------ lock

void RoverLock::init() { s_lock = xSemaphoreCreateMutex(); }
RoverLock::RoverLock(uint32_t timeoutMs) : ok_(xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {}
RoverLock::~RoverLock() {
  if (ok_) xSemaphoreGive(s_lock);
}

// ------------------------------------------------------------------ setup

void Rover::begin() {
  motors_.begin();  // outputs LOW before anything else
  estop_.begin();
  power_.begin();
  sonar_.begin();
  g_i2c.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  g_i2c.setTimeOut(20);
  pan_.begin(g_i2c);
  cam_.begin();

  // Let the E-stop input settle so the boot state reflects the real switch.
  const uint32_t t0 = millis();
  while (millis() - t0 < 150) {
    estop_.update(millis());
    delay(5);
  }
  const uint32_t now = millis();
  lastTickMs_ = now;
  modeChangedMs_ = now;
  if (estop_.pressed()) {
    enterEstop("hardware", "E-stop pressed (or sense wire open) at boot", now);
  } else {
    setState(State::Idle, now, "ready; motor outputs disabled");
  }
  if (!ESTOP_SENSE_FITTED) g_events.add(EvLevel::Warn, "E-stop sense not fitted: firmware cannot see the hardware E-stop");
  if (!sonar_.fitted(REAR)) g_events.add(EvLevel::Warn, "rear sonar not fitted: reverse is always blocked");
}

// ------------------------------------------------------------------ helpers

void Rover::setState(State s, uint32_t nowMs, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail_, sizeof(detail_), fmt, ap);
  va_end(ap);
  if (s == state_) return;
  g_events.add(levelFor(s), "%s -> %s: %s", stateName(state_), stateName(s), detail_);
  state_ = s;
  stateChangedMs_ = nowMs;
}

void Rover::resetMissionState(uint32_t nowMs) {
  (void)nowMs;
  qrPhase_ = QrPhase::Off;
  qrTeleop_ = false;
  motionTeleop_ = false;
  trackStartMs_ = 0;
  lastDetMs_ = 0;
  blockedSinceMs_ = 0;
  proxStable_ = 0;
  proxInWindow_ = false;
  scanDwellStartMs_ = 0;
  teleL_ = teleR_ = 0.0f;
  teleCmdMs_ = 0;
}

void Rover::enterEstop(const char* source, const char* why, uint32_t nowMs) {
  motors_.hardStop();
  outL_ = outR_ = 0.0f;
  armed_ = false;
  estopLatched_ = true;
  estopSource_ = source;
  resetMissionState(nowMs);
  pan_.freeze();
  if (mode_ != Mode::Idle) {
    mode_ = Mode::Idle;
    modeChangedMs_ = nowMs;
  }
  setState(State::EStop, nowMs, "%s", why);
}

void Rover::enterSafeStop(const char* why, uint32_t nowMs) {
  motors_.hardStop();
  outL_ = outR_ = 0.0f;
  armed_ = false;
  resetMissionState(nowMs);
  pan_.freeze();
  setState(State::SafeStop, nowMs, "%s", why);
}

void Rover::finishMission(bool success, uint32_t nowMs, const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  resetMissionState(nowMs);
  setState(success ? State::AutoComplete : State::AutoFailed, nowMs, "%s", buf);
}

bool Rover::motorPowerAbsent() const { return MOTOR_BUS_SENSE_FITTED && !power_.motorBusPresent(); }

bool Rover::reverseAllowed(uint32_t nowMs, const char** reason) const {
  if (!sonar_.fitted(REAR)) {
    *reason = "rear sonar not fitted";
    return false;
  }
  if (!sonar_.fresh(REAR, nowMs)) {
    *reason = "rear sonar stale or faulty";
    return false;
  }
  const SonarReading r = sonar_.reading(REAR);
  if (r.status == SonarStatus::Ok && r.mm < REAR_CLEARANCE_MM) {
    *reason = "rear obstacle too close";
    return false;
  }
  *reason = "clear";
  return true;
}

// ------------------------------------------------------------------ control tick

void Rover::tick(uint32_t nowMs) {
  const float dt = clampf((nowMs - lastTickMs_) / 1000.0f, 0.0f, 0.1f);
  lastTickMs_ = nowMs;

  estop_.update(nowMs);
  power_.update(nowMs);
  sonar_.update(nowMs);
  cam_.poll(nowMs);
  pan_.update(nowMs);

  if (softEstopPending_.exchange(false) && state_ != State::EStop) {
    enterEstop("software", "software E-stop", nowMs);
  }
  if (estop_.pressed() && state_ != State::EStop) {
    enterEstop("hardware", "hardware E-stop pressed (or sense wire open)", nowMs);
  }
  if (armed_ && motorPowerAbsent() && state_ != State::EStop) {
    enterEstop("motor_power", "motor bus lost while armed (E-stop, contactor or fuse)", nowMs);
  }

  float tl = 0.0f, tr = 0.0f, cap = 1.0f;
  switch (state_) {
    case State::Teleop:
      cap = TELEOP_MAX_DUTY;
      if (teleCmdMs_ != 0 && nowMs - teleCmdMs_ <= TELEOP_CMD_TIMEOUT_MS) {
        tl = teleL_;
        tr = teleR_;
        teleTimedOut_ = false;
      } else {
        if (!teleTimedOut_ && (teleL_ != 0.0f || teleR_ != 0.0f)) {
          g_events.add(EvLevel::Warn, "teleop command timeout (%lu ms): stopping",
                       static_cast<unsigned long>(TELEOP_CMD_TIMEOUT_MS));
        }
        teleTimedOut_ = true;
        teleL_ = teleR_ = 0.0f;
      }
      if (qrTeleop_) runQrPing(nowMs, false);
      break;
    case State::AutoRunning:
    case State::AutoBlocked:
      switch (mission_) {
        case Mission::Traverse: runTraverse(nowMs, &tl, &tr); break;
        case Mission::Proximity: runProximity(nowMs, &tl, &tr); break;
        case Mission::QrPing: runQrPing(nowMs, true); break;
        case Mission::Motion: runMotion(nowMs); break;
        case Mission::None: enterSafeStop("AUTO running without a mission", nowMs); break;
      }
      break;
    default:
      break;
  }
  // A mission may have finished or faulted during this tick.
  const bool armed = state_ == State::Teleop || autoActive(state_);
  applyDrive(tl, tr, armed, cap, nowMs, dt);

  CamRequest req;
  req.qr = qrPhase_ == QrPhase::Scanning;
  req.motion = (mission_ == Mission::Motion && autoActive(state_)) || (motionTeleop_ && state_ == State::Teleop);
  req.pan = pan_.state();
  req.panDeg = pan_.angle();
  req.panEpoch = pan_.epoch();
  cam_.request(req, nowMs);
}

void Rover::applyDrive(float l, float r, bool armed, float cap, uint32_t nowMs, float dt) {
  if (!armed) {
    if (armed_ || outL_ != 0.0f || outR_ != 0.0f) motors_.hardStop();
    outL_ = outR_ = 0.0f;
    armed_ = false;
    return;
  }
  armed_ = true;
  l = clampf(l, -cap, cap);
  r = clampf(r, -cap, cap);

  // Rear interlock: remove the backwards component, keep any turn-in-place part.
  const float v = (l + r) * 0.5f;
  const float w = (l - r) * 0.5f;
  const char* reason = "clear";
  if (v < -0.001f && !reverseAllowed(nowMs, &reason)) {
    l = w;
    r = -w;
    if (nowMs - lastReverseEventMs_ > 2000) {
      lastReverseEventMs_ = nowMs;
      g_events.add(EvLevel::Warn, "reverse blocked: %s", reason);
    }
  }

  // Keep the average motor voltage within the 12 V rating on a full 3S pack.
  const float vcap = power_.batteryValid() ? clampf(MOTOR_RATED_V / power_.batteryV(), 0.0f, 1.0f)
                                           : MOTOR_DUTY_CAP_NO_VSENSE;
  l = clampf(l, -vcap, vcap);
  r = clampf(r, -vcap, vcap);

  outL_ = slewToward(outL_, l, dt);
  outR_ = slewToward(outR_, r, dt);
  const float wl = fabsf(outL_) < MOTOR_MIN_EFFECTIVE ? 0.0f : outL_;
  const float wr = fabsf(outR_) < MOTOR_MIN_EFFECTIVE ? 0.0f : outR_;
  motors_.write(wl, wr);
}

// ------------------------------------------------------------------ missions

void Rover::scanPan(uint32_t nowMs) {
  if (!pan_.present() || pan_.state() != PanState::Settled) return;
  if (scanDwellStartMs_ == 0) {
    scanDwellStartMs_ = nowMs;
    return;
  }
  if (nowMs - scanDwellStartMs_ < PAN_SCAN_DWELL_MS) return;
  scanIdx_ = (scanIdx_ + 1) % kScanCount;
  pan_.setTarget(PAN_SCAN_DEG[scanIdx_]);
  scanDwellStartMs_ = 0;
}

void Rover::runTraverse(uint32_t nowMs, float* l, float* r) {
  if (nowMs - missionStartMs_ > TRAVERSE_TIMEOUT_MS) {
    finishMission(false, nowMs, "timeout after %lu s: lane end cannot be verified (no encoders/end sensor)",
                  static_cast<unsigned long>(TRAVERSE_TIMEOUT_MS / 1000));
    return;
  }
  const int front[] = {FC, FL, FR};
  for (int i : front) {
    if (sonar_.fitted(i) && !sonar_.fresh(i, nowMs)) {
      char why[64];
      snprintf(why, sizeof(why), "sonar %s stale or faulty", SONAR_NAME[i]);
      enterSafeStop(why, nowMs);
      return;
    }
  }
  const SonarReading fc = sonar_.reading(FC);
  if (fc.status == SonarStatus::Ok && fc.mm < TRAVERSE_FRONT_BLOCK_MM) {
    if (state_ == State::AutoRunning) {
      blockedSinceMs_ = nowMs;
      setState(State::AutoBlocked, nowMs, "path blocked: front %d mm", fc.mm);
    } else if (nowMs - blockedSinceMs_ > TRAVERSE_BLOCKED_WAIT_MS) {
      finishMission(false, nowMs, "path blocked at %d mm; lane completion not verified", fc.mm);
    }
    *l = *r = 0.0f;
    return;
  }
  if (state_ == State::AutoBlocked) setState(State::AutoRunning, nowMs, "path clear; resuming traverse");

  auto sidePush = [&](int i) -> float {
    if (!sonar_.fitted(i)) return 0.0f;
    const SonarReading s = sonar_.reading(i);
    if (s.status != SonarStatus::Ok || s.mm >= TRAVERSE_SIDE_CLEAR_MM) return 0.0f;
    return TRAVERSE_STEER_GAIN * (1.0f - static_cast<float>(s.mm) / TRAVERSE_SIDE_CLEAR_MM);
  };
  // Obstacle on the left -> left side faster -> rover turns right, and vice versa.
  const float diff = sidePush(FL) - sidePush(FR);
  *l = TRAVERSE_CRUISE_DUTY + diff;
  *r = TRAVERSE_CRUISE_DUTY - diff;
}

void Rover::runProximity(uint32_t nowMs, float* l, float* r) {
  *l = *r = 0.0f;
  if (nowMs - missionStartMs_ > PROX_TIMEOUT_MS) {
    finishMission(false, nowMs, "timeout; last front distance %d mm", proxMeasuredMm_);
    return;
  }
  if (!sonar_.fresh(FC, nowMs)) {
    enterSafeStop("front-center sonar stale or faulty", nowMs);
    return;
  }
  const SonarReading fc = sonar_.reading(FC);
  const bool far = fc.status == SonarStatus::OutOfRange;
  const int d = far ? 100000 : sonar_.filteredMm(FC, nowMs);
  if (d < 0) return;  // waiting for in-range data
  proxMeasuredMm_ = far ? -1 : d;
  const int err = d - PROX_TARGET_MM;
  proxInWindow_ = abs(err) <= PROX_TOLERANCE_MM;
  const bool stopped = fabsf(outL_) < 0.01f && fabsf(outR_) < 0.01f;
  if (fc.tMs != proxLastReadingMs_) {  // count each new measurement once
    proxLastReadingMs_ = fc.tMs;
    proxStable_ = (proxInWindow_ && stopped) ? static_cast<uint8_t>(proxStable_ + 1) : 0;
  }
  if (proxStable_ >= PROX_STABLE_READINGS) {
    finishMission(true, nowMs, "stopped at %d mm (target %d +/- %d mm)", d, PROX_TARGET_MM, PROX_TOLERANCE_MM);
    return;
  }
  // Inside the inner band: stop. Inside the outer window and already stopped: hold.
  if (abs(err) <= PROX_INNER_TOLERANCE_MM || (proxInWindow_ && proxStable_ > 0)) return;

  const float duty = clampf(fabsf(static_cast<float>(err)) * PROX_GAIN_PER_MM, PROX_MIN_DUTY, PROX_MAX_DUTY);
  if (err > 0) {
    *l = *r = duty;
    return;
  }
  const char* reason = "";
  if (!reverseAllowed(nowMs, &reason)) {
    if (proxInWindow_) return;  // hold; completes once stable
    finishMission(false, nowMs, "too close (%d mm) and reverse blocked: %s", d, reason);
    return;
  }
  *l = *r = -duty;
}

void Rover::runQrPing(uint32_t nowMs, bool missionMode) {
  const CamState& cs = cam_.state();
  if (qrPhase_ == QrPhase::Scanning) {
    if (!cam_.fresh(nowMs)) {
      if (missionMode) {
        enterSafeStop("camera link lost during QR task", nowMs);
      } else {
        g_events.add(EvLevel::Warn, "QR scan cancelled: camera link lost");
        qrPhase_ = QrPhase::Off;
        qrTeleop_ = false;
      }
      return;
    }
    if (nowMs - qrStartMs_ > QR_TIMEOUT_MS) {
      if (missionMode) {
        finishMission(false, nowMs, "no valid QR/IPv4 within %lu s", static_cast<unsigned long>(QR_TIMEOUT_MS / 1000));
      } else {
        g_events.add(EvLevel::Warn, "QR scan timed out");
        qrPhase_ = QrPhase::Off;
        qrTeleop_ = false;
      }
      return;
    }
    if (cs.qrSeq != qrSeqAtStart_) {
      qrSeqAtStart_ = cs.qrSeq;
      char ip[16] = "";
      // Never trust the camera's verdict alone: validate again here.
      const ipv4::Result vr = cs.qrValid ? ipv4::validate(cs.qrIp, strlen(cs.qrIp), ip) : ipv4::Result::kSyntax;
      if (cs.qrValid && vr == ipv4::Result::kOk) {
        strncpy(qrIp_, ip, sizeof(qrIp_));
        strncpy(lastValidQrIp_, ip, sizeof(lastValidQrIp_));
        lastValidQrMs_ = nowMs;
        g_events.add(EvLevel::Info, "QR decoded: %s -> on-board ping", qrIp_);
        qrPhase_ = QrPhase::Pinging;
        if (!pinger_.start(qrIp_, PING_COUNT, PING_TIMEOUT_MS)) {
          const PingResult pr = pinger_.result();
          qrPhase_ = QrPhase::Done;
          qrTeleop_ = false;
          if (missionMode) {
            finishMission(false, nowMs, "QR %s ok; ping not started: %s", qrIp_, pr.error);
          } else {
            g_events.add(EvLevel::Error, "ping not started: %s", pr.error);
          }
        }
        return;
      }
      g_events.add(EvLevel::Warn, "QR rejected (%s): \"%.28s\"",
                   cs.qrErr[0] ? cs.qrErr : ipv4::resultName(vr), cs.qrText);
    }
    if (missionMode) scanPan(nowMs);
    return;
  }
  if (qrPhase_ == QrPhase::Pinging) {
    const PingResult pr = pinger_.result();
    if (pr.state == PingState::Running) return;
    qrPhase_ = QrPhase::Done;
    qrTeleop_ = false;
    if (pr.state == PingState::Reply) {
      if (missionMode) {
        finishMission(true, nowMs, "QR %s decoded; ping reply in %lu ms", qrIp_, static_cast<unsigned long>(pr.rttMs));
      } else {
        g_events.add(EvLevel::Info, "ping %s reply in %lu ms", qrIp_, static_cast<unsigned long>(pr.rttMs));
      }
    } else {
      // The referee's log of the echo REQUEST is what scores; a missing reply
      // is reported honestly as a failure for the operator to check.
      if (missionMode) {
        finishMission(false, nowMs, "QR %s decoded; %lu echo request(s) sent, no reply", qrIp_,
                      static_cast<unsigned long>(pr.sent));
      } else {
        g_events.add(EvLevel::Warn, "ping %s: %lu sent, no reply", qrIp_, static_cast<unsigned long>(pr.sent));
      }
    }
  }
}

void Rover::runMotion(uint32_t nowMs) {
  const CamState& cs = cam_.state();
  if (!cam_.fresh(nowMs)) {
    enterSafeStop("camera link lost during motion task", nowMs);
    return;
  }
  if (nowMs - missionStartMs_ > MOTION_TIMEOUT_MS) {
    finishMission(false, nowMs, "no %lu s continuous track within %lu s",
                  static_cast<unsigned long>(MOTION_GOAL_MS / 1000), static_cast<unsigned long>(MOTION_TIMEOUT_MS / 1000));
    return;
  }
  const bool panSettled = !pan_.present() || pan_.state() == PanState::Settled;
  if (cs.motSeq != motSeqSeen_) {
    motSeqSeen_ = cs.motSeq;
    // Only frames whose baseline was taken after the camera settled count.
    const bool epochOk = !pan_.present() || cs.motEpoch == pan_.epoch();
    if (cs.motDetected && panSettled && epochOk) {
      if (trackStartMs_ == 0) {
        trackStartMs_ = nowMs;
        g_events.add(EvLevel::Info, "motion target acquired (cx %.2f)", cs.motCx);
      }
      lastDetMs_ = nowMs;
      if (pan_.present() && fabsf(cs.motCx) > MOTION_RECENTER_CX) {
        const float dir = (cs.motCx > 0.0f ? 1.0f : -1.0f) * MOTION_PAN_DIRECTION;
        pan_.setTarget(pan_.angle() + dir * MOTION_RECENTER_STEP_DEG);
      }
    }
  }
  if (trackStartMs_ != 0) {
    if (nowMs - lastDetMs_ > MOTION_MAX_GAP_MS) {
      g_events.add(EvLevel::Warn, "motion target lost after %lu ms", static_cast<unsigned long>(lastDetMs_ - trackStartMs_));
      trackStartMs_ = 0;
      lastDetMs_ = 0;
      scanDwellStartMs_ = 0;
    } else if (lastDetMs_ - trackStartMs_ >= MOTION_GOAL_MS) {
      finishMission(true, nowMs, "target tracked continuously for %.1f s", (lastDetMs_ - trackStartMs_) / 1000.0f);
      return;
    }
  }
  if (trackStartMs_ == 0) scanPan(nowMs);
}

// ------------------------------------------------------------------ commands

CmdResult Rover::setMode(Mode m, Mission mission, bool missionGiven, CmdSource src) {
  const uint32_t now = millis();
  if (state_ == State::EStop) return CmdResult::conflict("E-stop active: release it, then POST /api/estop/reset");
  if (state_ == State::SafeStop && m != Mode::Idle) {
    return CmdResult::conflict("safe stop: acknowledge first (POST /api/fault/ack or mode idle)");
  }
  const Mode before = mode_;
  switch (m) {
    case Mode::Idle:
      if (autoActive(state_)) g_events.add(EvLevel::Warn, "AUTO mission aborted by operator (%s)", sourceName(src));
      resetMissionState(now);
      mode_ = Mode::Idle;
      setState(State::Idle, now, "mode idle (%s); outputs disabled", sourceName(src));
      break;
    case Mode::Teleop:
      if (motorPowerAbsent()) return CmdResult::conflict("motor power absent: release E-stop and press ARM");
      if (autoActive(state_)) {
        g_events.add(EvLevel::Warn, "AUTO aborted by operator -> teleop (%s)", sourceName(src));
      }
      resetMissionState(now);
      mode_ = Mode::Teleop;
      teleTimedOut_ = true;
      setState(State::Teleop, now, "teleoperation (%s); waiting for drive commands", sourceName(src));
      break;
    case Mode::Auto: {
      if (autoActive(state_)) return CmdResult::conflict("mission running: abort it first");
      Mission target = missionGiven ? mission : mission_;
      if (target == Mission::None) {
        if (missionGiven) return CmdResult::bad("mission required: traverse|proximity|qr_ping|motion");
        target = kDefaultAutoMission;
      }
      resetMissionState(now);
      mode_ = Mode::Auto;
      mission_ = target;
      setState(State::AutoReady, now, "AUTO %s armed; controls locked; waiting for START", missionName(target));
      break;
    }
  }
  if (mode_ != before) modeChangedMs_ = now;
  return CmdResult::accepted("mode changed");
}

CmdResult Rover::startMission(CmdSource src) {
  const uint32_t now = millis();
  if (mode_ != Mode::Auto) return CmdResult::conflict("not in AUTO mode");
  if (state_ != State::AutoReady && state_ != State::AutoComplete && state_ != State::AutoFailed) {
    return CmdResult::conflict("mission running or rover not ready");
  }
  const bool drives = mission_ == Mission::Traverse || mission_ == Mission::Proximity;
  if (drives) {
    if (motorPowerAbsent()) return CmdResult::conflict("motor power absent: release E-stop and press ARM");
    if (!sonar_.fitted(FC)) return CmdResult::conflict("front-center sonar not fitted");
    if (!sonar_.fresh(FC, now)) return CmdResult::conflict("front-center sonar has no fresh reading");
  } else if (!cam_.fresh(now)) {
    return CmdResult::conflict("camera link not up");
  }
  resetMissionState(now);
  missionStartMs_ = now;
  scanIdx_ = 0;
  if (mission_ == Mission::QrPing) {
    qrPhase_ = QrPhase::Scanning;
    qrSeqAtStart_ = cam_.state().qrSeq;
    qrStartMs_ = now;
  } else if (mission_ == Mission::Motion) {
    motSeqSeen_ = cam_.state().motSeq;
  }
  setState(State::AutoRunning, now, "%s started (%s)", missionName(mission_), sourceName(src));
  if (mission_ == Mission::Traverse && !LANE_END_SENSOR_FITTED) {
    g_events.add(EvLevel::Warn, "no encoders/end sensor: traverse can only end as FAILED (timeout or blocked)");
  }
  return CmdResult::accepted("mission started");
}

CmdResult Rover::abortMission(CmdSource src) {
  const uint32_t now = millis();
  if (!autoActive(state_)) return CmdResult::conflict("no mission running");
  finishMission(false, now, "aborted by operator (%s)", sourceName(src));
  return CmdResult::accepted("mission aborted");
}

CmdResult Rover::drive(float left, float right, CmdSource src) {
  const uint32_t now = millis();
  if (mode_ == Mode::Auto) {
    if (now - lastDriveRejectEventMs_ > 2000) {
      lastDriveRejectEventMs_ = now;
      g_events.add(EvLevel::Warn, "drive command rejected: AUTO active (%s)", sourceName(src));
    }
    return CmdResult::conflict("drive rejected: AUTO mode active");
  }
  if (state_ != State::Teleop) return CmdResult::conflict("drive rejected: not in teleoperation");
  if (!isfinite(left) || !isfinite(right)) return CmdResult::bad("left/right must be numbers in [-1, 1]");
  teleL_ = clampf(left, -1.0f, 1.0f);
  teleR_ = clampf(right, -1.0f, 1.0f);
  teleCmdMs_ = now;
  return CmdResult::accepted("drive accepted");
}

CmdResult Rover::engageEstop(CmdSource src) {
  char why[48];
  snprintf(why, sizeof(why), "software E-stop (%s)", sourceName(src));
  softEstopPending_.store(false);
  enterEstop("software", why, millis());
  return CmdResult::accepted("E-stop engaged");
}

CmdResult Rover::resetEstop(CmdSource src) {
  const uint32_t now = millis();
  if (state_ != State::EStop) return CmdResult::conflict("not in E-stop");
  if (estop_.pressed()) return CmdResult::conflict("hardware E-stop still pressed (twist to release) or sense wire open");
  estopLatched_ = false;
  estopSource_ = "none";
  mode_ = Mode::Idle;
  modeChangedMs_ = now;
  setState(State::Idle, now, "E-stop reset (%s); outputs disabled; press ARM to restore motor power", sourceName(src));
  return CmdResult::accepted("E-stop reset; mode idle");
}

CmdResult Rover::ackFault(CmdSource src) {
  const uint32_t now = millis();
  if (state_ != State::SafeStop) return CmdResult::conflict("no fault to acknowledge");
  mode_ = Mode::Idle;
  modeChangedMs_ = now;
  setState(State::Idle, now, "fault acknowledged (%s)", sourceName(src));
  return CmdResult::accepted("fault acknowledged; mode idle");
}

CmdResult Rover::setPan(float deg, CmdSource src) {
  (void)src;
  if (mode_ == Mode::Auto) return CmdResult::conflict("pan is controlled on board in AUTO");
  if (state_ == State::EStop) return CmdResult::conflict("E-stop active");
  if (!pan_.present()) return CmdResult::conflict("PCA9685 not detected");
  if (!isfinite(deg)) return CmdResult::bad("angle must be a number");
  pan_.setTarget(deg);
  return CmdResult::accepted("pan target set");
}

CmdResult Rover::setPanPulse(float us, CmdSource src) {
  if (state_ != State::Idle) return CmdResult::conflict("raw pulse calibration only in IDLE");
  if (!pan_.present()) return CmdResult::conflict("PCA9685 not detected");
  if (!isfinite(us)) return CmdResult::bad("us must be a number");
  pan_.setRawPulse(us);
  g_events.add(EvLevel::Info, "servo raw pulse %.0f us (%s)", pan_.lastPulseUs(), sourceName(src));
  return CmdResult::accepted("raw pulse set");
}

CmdResult Rover::triggerQrPing(CmdSource src) {
  const uint32_t now = millis();
  if (state_ != State::Teleop) return CmdResult::conflict("teleop QR task needs TELEOP mode (use AUTO qr_ping otherwise)");
  if (!cam_.fresh(now)) return CmdResult::conflict("camera link not up");
  if (qrPhase_ == QrPhase::Scanning || qrPhase_ == QrPhase::Pinging) return CmdResult::conflict("QR task already running");
  qrTeleop_ = true;
  qrPhase_ = QrPhase::Scanning;
  qrSeqAtStart_ = cam_.state().qrSeq;
  qrStartMs_ = now;
  g_events.add(EvLevel::Info, "teleop QR scan + ping started (%s)", sourceName(src));
  return CmdResult::accepted("QR scan started");
}

CmdResult Rover::setMotionDetect(bool on, CmdSource src) {
  if (state_ != State::Teleop) return CmdResult::conflict("manual motion detection needs TELEOP mode");
  motionTeleop_ = on;
  g_events.add(EvLevel::Info, "teleop motion detection %s (%s)", on ? "on" : "off", sourceName(src));
  return CmdResult::accepted(on ? "motion detection on" : "motion detection off");
}

// ------------------------------------------------------------------ snapshot

void Rover::snapshot(RoverSnapshot* s) const {
  const uint32_t now = millis();
  s->nowMs = now;
  s->mode = mode_;
  s->state = state_;
  s->mission = mission_;
  s->modeChangedMs = modeChangedMs_;
  s->stateChangedMs = stateChangedMs_;
  strncpy(s->detail, detail_, sizeof(s->detail));
  s->estopActive = state_ == State::EStop;
  s->estopHwPressed = estop_.pressed();
  s->estopSense = estop_.senseName();
  s->estopSource = estopLatched_ ? estopSource_ : "none";
  s->motorsArmed = armed_;
  s->outLeft = motors_.left();
  s->outRight = motors_.right();
  s->reverseAllowed = reverseAllowed(now, &s->reverseReason);
  s->battWired = power_.batteryWired();
  s->battV = power_.batteryV();
  s->battPct = power_.batteryPercent();
  s->busWired = power_.motorBusWired();
  s->busV = power_.motorBusV();
  for (int i = 0; i < SonarArray::kCount; ++i) {
    s->sonar[i] = sonar_.reading(i);
    s->sonarFitted[i] = sonar_.fitted(i);
  }
  s->frontFilteredMm = sonar_.filteredMm(FC, now);
  s->cam = cam_.state();
  s->camLink = cam_.linkName(now);
  s->pan = pan_.state();
  s->panDeg = pan_.angle();
  s->panTarget = pan_.target();
  s->panPulseUs = pan_.lastPulseUs();
  s->panPresent = pan_.present();
  s->qrRequested = qrPhase_ == QrPhase::Scanning;
  s->ping = pinger_.result();
  strncpy(s->lastValidQrIp, lastValidQrIp_, sizeof(s->lastValidQrIp));
  s->lastValidQrMs = lastValidQrMs_;
  s->motionRequested = (mission_ == Mission::Motion && autoActive(state_)) || (motionTeleop_ && state_ == State::Teleop);
  s->motionTracking = trackStartMs_ != 0;
  s->motionTrackedMs = trackStartMs_ ? lastDetMs_ - trackStartMs_ : 0;
  s->motionLastDetMs = lastDetMs_;
  s->proxInWindow = proxInWindow_;
  s->proxMeasuredMm = proxMeasuredMm_;
  s->missionStartMs = missionStartMs_;
}

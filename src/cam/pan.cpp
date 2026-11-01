#include "pan.h"

#include "config_cam.h"

namespace {
using boardlink::PanMode;
using boardlink::PanState;

constexpr size_t kScanCount = sizeof(PAN_SCAN_DEG) / sizeof(PAN_SCAN_DEG[0]);
constexpr uint32_t kWriteIntervalMs = 20;  // one update per servo frame

// Loop task only.
float g_angle = boardlink::kPanHomeDeg;
float g_target = boardlink::kPanHomeDeg;
float g_pulseUs = 0.0f;
bool g_moving = false;
uint32_t g_reachedMs = 0;
uint32_t g_lastWriteMs = 0;
uint32_t g_epoch = 0;
PanMode g_mode = PanMode::Hold;
size_t g_scanIdx = 0;
uint32_t g_dwellStartMs = 0;

// Shared with other tasks under g_mux.
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
PanStatus g_status = {PanState::Absent, PanMode::Hold, boardlink::kPanHomeDeg, boardlink::kPanHomeDeg, 0.0f, 0, false};
bool g_detNew = false;
float g_detCx = 0.0f;
uint32_t g_lastDetMs = 0;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float pulseFor(float deg) { return SERVO_US_AT_0_DEG + (SERVO_US_AT_180_DEG - SERVO_US_AT_0_DEG) * (deg / 180.0f); }
float degFor(float us) { return (us - SERVO_US_AT_0_DEG) * 180.0f / (SERVO_US_AT_180_DEG - SERVO_US_AT_0_DEG); }

void writePulse(float us) {
  constexpr float kCountsPerUs = PAN_PWM_HZ * static_cast<float>((1u << PAN_PWM_BITS) - 1) / 1e6f;
  ledcWrite(PAN_LEDC_CHANNEL, static_cast<uint32_t>(us * kCountsPerUs + 0.5f));
  g_pulseUs = us;
}

void startMove(uint32_t nowMs) {
  if (!g_moving) ++g_epoch;
  g_moving = true;
  g_reachedMs = nowMs;
}

void moveTo(float deg, uint32_t nowMs) {
  const float t = clampf(deg, boardlink::kPanMinDeg, boardlink::kPanMaxDeg);
  if (fabsf(t - g_target) < 0.5f) return;
  g_target = t;
  startMove(nowMs);
}

void scanStep(uint32_t nowMs) {
  if (g_moving) return;
  if (g_dwellStartMs == 0) {
    g_dwellStartMs = nowMs;
    return;
  }
  if (nowMs - g_dwellStartMs < PAN_SCAN_DWELL_MS) return;
  g_scanIdx = (g_scanIdx + 1) % kScanCount;
  moveTo(PAN_SCAN_DEG[g_scanIdx], nowMs);
  g_dwellStartMs = 0;
}
}  // namespace

void panBegin() {
  if (!PAN_SERVO_FITTED) return;
  ledcSetup(PAN_LEDC_CHANNEL, PAN_PWM_HZ, PAN_PWM_BITS);
  ledcAttachPin(PAN_PIN, PAN_LEDC_CHANNEL);
  // Real position unknown at power-up: drive home and let it settle.
  writePulse(pulseFor(g_angle));
  startMove(millis());
}

void panUpdate(uint32_t nowMs, const LinkConfig& cfg) {
  if (!PAN_SERVO_FITTED) return;
  const PanMode mode = cfg.fresh ? cfg.panMode : PanMode::Hold;
  if (mode != g_mode) {
    if (g_mode == PanMode::Pulse) g_target = g_angle;  // leave the raw pulse where it is
    g_dwellStartMs = 0;
    g_mode = mode;
  }

  portENTER_CRITICAL(&g_mux);
  const bool det = g_detNew;
  const float cx = g_detCx;
  const uint32_t lastDetMs = g_lastDetMs;
  g_detNew = false;
  portEXIT_CRITICAL(&g_mux);

  bool tracking = false;
  switch (mode) {
    case PanMode::Hold:
      g_target = g_angle;
      break;
    case PanMode::Goto:
      moveTo(cfg.panDeg, nowMs);
      break;
    case PanMode::Pulse: {
      const float us = clampf(cfg.pulseUs, boardlink::kPanPulseMinUs, boardlink::kPanPulseMaxUs);
      if (fabsf(us - g_pulseUs) >= 1.0f) {
        writePulse(us);
        g_angle = g_target = degFor(us);  // nominal mapping, for display
        startMove(nowMs);
      }
      break;
    }
    case PanMode::Track:
      tracking = lastDetMs != 0 && nowMs - lastDetMs <= PAN_TRACK_LOST_MS;
      if (tracking) {
        // A detection arrives only from a baseline taken after the last settle.
        if (det && !g_moving && fabsf(cx) > MOTION_RECENTER_CX) {
          moveTo(g_angle + (cx > 0.0f ? 1.0f : -1.0f) * MOTION_PAN_DIRECTION * MOTION_RECENTER_STEP_DEG, nowMs);
        }
        g_dwellStartMs = 0;
        break;
      }
      scanStep(nowMs);  // no target: search
      break;
    case PanMode::Scan:
      scanStep(nowMs);
      break;
  }

  if (g_moving) {
    if (mode != PanMode::Pulse && fabsf(g_target - g_angle) > 0.01f) {
      const uint32_t sinceWrite = nowMs - g_lastWriteMs;
      if (sinceWrite >= kWriteIntervalMs) {
        const float maxStep = SERVO_MAX_DEG_PER_S * static_cast<float>(sinceWrite > 100 ? 100 : sinceWrite) / 1000.0f;
        g_angle += clampf(g_target - g_angle, -maxStep, maxStep);
        g_lastWriteMs = nowMs;
        writePulse(pulseFor(g_angle));
        g_reachedMs = nowMs;
      }
    } else if (nowMs - g_reachedMs >= SERVO_SETTLE_MS) {
      g_moving = false;
    }
  }

  const PanStatus s = {g_moving ? PanState::Moving : PanState::Settled, mode, g_angle, g_target, g_pulseUs, g_epoch,
                       tracking};
  portENTER_CRITICAL(&g_mux);
  g_status = s;
  portEXIT_CRITICAL(&g_mux);
}

PanStatus panStatus() {
  portENTER_CRITICAL(&g_mux);
  const PanStatus s = g_status;
  portEXIT_CRITICAL(&g_mux);
  return s;
}

void panMotionSeen(bool detected, float cx, uint32_t nowMs) {
  if (!detected) return;
  portENTER_CRITICAL(&g_mux);
  g_detNew = true;
  g_detCx = cx;
  g_lastDetMs = nowMs;
  portEXIT_CRITICAL(&g_mux);
}

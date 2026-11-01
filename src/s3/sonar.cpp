#include "sonar.h"

#include <esp_timer.h>
#include <soc/gpio_reg.h>

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"

namespace {
struct IsrCtx {
  void* cap;
  int pin;
};
IsrCtx g_ctx[SonarArray::kCount];

inline bool IRAM_ATTR fastRead(int pin) {
  if (pin < 32) return (REG_READ(GPIO_IN_REG) >> pin) & 1U;
  return (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1U;
}

constexpr size_t kScheduleLen = sizeof(SONAR_SCHEDULE) / sizeof(SONAR_SCHEDULE[0]);
}  // namespace

void IRAM_ATTR SonarArray::onEchoEdge(void* arg) {
  IsrCtx* ctx = static_cast<IsrCtx*>(arg);
  EchoCapture* c = static_cast<EchoCapture*>(ctx->cap);
  const int64_t t = esp_timer_get_time();
  if (fastRead(ctx->pin)) {
    if (!c->rose) {
      c->riseUs = t;
      c->rose = true;
    }
  } else if (c->rose && !c->fell) {
    c->fallUs = t;
    c->fell = true;
  }
}

void SonarArray::begin() {
  for (int i = 0; i < kCount; ++i) {
    last_[i].status = SONAR_FITTED[i] ? SonarStatus::NoData : SonarStatus::NotFitted;
    if (!SONAR_FITTED[i]) continue;
    pinMode(PIN_SONAR_TRIG[i], OUTPUT);
    digitalWrite(PIN_SONAR_TRIG[i], LOW);
    // The external 1k/1k8 divider sets the level when a sensor is wired. The
    // internal pull-down (~45k, negligible next to 1k8) keeps an unwired or
    // broken echo line LOW, so it reports no_echo. Left floating, it picked up
    // noise that read as steady ~1.5 m "ok" distances on the bench.
    pinMode(PIN_SONAR_ECHO[i], INPUT_PULLDOWN);
    g_ctx[i] = {&cap_[i], PIN_SONAR_ECHO[i]};
    attachInterruptArg(PIN_SONAR_ECHO[i], onEchoEdge, &g_ctx[i], CHANGE);
  }
}

bool SonarArray::fitted(int i) const { return i >= 0 && i < kCount && SONAR_FITTED[i]; }

int SonarArray::nextScheduled(uint32_t nowMs) {
  for (size_t tries = 0; tries < kScheduleLen; ++tries) {
    const int idx = SONAR_SCHEDULE[schedIdx_];
    if (!fitted(idx)) {
      schedIdx_ = (schedIdx_ + 1) % kScheduleLen;
      continue;
    }
    // HC-SR04 needs a rest period between its own pings; wait if necessary.
    if (lastTrigMs_[idx] != 0 && nowMs - lastTrigMs_[idx] < SONAR_MIN_RETRIGGER_MS) return -1;
    schedIdx_ = (schedIdx_ + 1) % kScheduleLen;
    return idx;
  }
  return -1;
}

void SonarArray::store(int i, SonarStatus st, int mm, uint32_t nowMs) {
  last_[i].status = st;
  last_[i].mm = mm;
  last_[i].tMs = nowMs;
  ++count_[i];
  if (st == SonarStatus::Ok) {
    history_[i][historyIdx_[i]] = mm;
    historyMs_[i][historyIdx_[i]] = nowMs;
    historyIdx_[i] = (historyIdx_[i] + 1) % 3;
  }
}

void SonarArray::update(uint32_t nowMs) {
  if (active_ < 0) {
    const int idx = nextScheduled(nowMs);
    if (idx < 0) return;
    lastTrigMs_[idx] = nowMs;
    EchoCapture& c = cap_[idx];
    if (digitalRead(PIN_SONAR_ECHO[idx]) == HIGH) {
      // Echo already high before the trigger: wiring fault or a sensor that
      // never finished its previous cycle. Report it and use the slot anyway.
      store(idx, SonarStatus::StuckHigh, -1, nowMs);
      active_ = idx;
      measured_ = true;
      slotStartMs_ = nowMs;
      return;
    }
    c.rose = false;
    c.fell = false;
    active_ = idx;
    measured_ = false;
    slotStartMs_ = nowMs;
    digitalWrite(PIN_SONAR_TRIG[idx], HIGH);
    delayMicroseconds(12);
    digitalWrite(PIN_SONAR_TRIG[idx], LOW);
    trigUs_ = esp_timer_get_time();
    return;
  }

  const int i = active_;
  EchoCapture& c = cap_[i];
  const uint32_t elapsed = nowMs - slotStartMs_;
  if (!measured_) {
    if (c.fell) {
      const int64_t widthUs = c.fallUs - c.riseUs;
      if (widthUs > static_cast<int64_t>(SONAR_MAX_ECHO_US)) {
        store(i, SonarStatus::OutOfRange, -1, nowMs);
      } else {
        // Very short echoes (< ~20 mm) are imprecise but still mean "something
        // is very close", which is the safe interpretation, so they are kept.
        const float rawMm = static_cast<float>(widthUs) * SOUND_SPEED_M_S / 2000.0f;
        const int mm = static_cast<int>(rawMm * SONAR_SCALE[i] + 0.5f) - SONAR_FACE_TO_BUMPER_MM[i];
        store(i, SonarStatus::Ok, mm < 0 ? 0 : mm, nowMs);
      }
      measured_ = true;
    } else if (!c.rose && elapsed >= SONAR_ECHO_START_TIMEOUT_MS) {
      store(i, SonarStatus::NoEcho, -1, nowMs);  // sensor missing, unpowered or broken echo wire
      measured_ = true;
    }
  }
  if (elapsed >= SONAR_SLOT_MS) {
    if (!measured_) {
      // Echo rose but never fell inside the slot: nothing within range.
      store(i, SonarStatus::OutOfRange, -1, nowMs);
    }
    active_ = -1;
  }
}

SonarReading SonarArray::reading(int i) const { return last_[i]; }

bool SonarArray::fresh(int i, uint32_t nowMs) const {
  if (!fitted(i)) return false;
  const SonarReading& r = last_[i];
  if (r.status != SonarStatus::Ok && r.status != SonarStatus::OutOfRange) return false;
  return nowMs - r.tMs <= SONAR_STALE_MS;
}

int SonarArray::filteredMm(int i, uint32_t nowMs) const {
  int vals[3];
  int n = 0;
  for (int k = 0; k < 3; ++k) {
    if (historyMs_[i][k] != 0 && nowMs - historyMs_[i][k] <= SONAR_STALE_MS) vals[n++] = history_[i][k];
  }
  if (n == 0) return -1;
  if (n == 1) return vals[0];
  if (n == 2) return (vals[0] + vals[1]) / 2;
  const int a = vals[0], b = vals[1], c = vals[2];
  return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

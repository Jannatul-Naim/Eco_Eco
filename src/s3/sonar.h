// Four HC-SR04 sonars triggered strictly one at a time (no crosstalk).
// Echo edges are time-stamped in an ISR; update() never blocks.
#pragma once

#include <Arduino.h>

#include "rover_types.h"

class SonarArray {
 public:
  static constexpr int kCount = 4;
  static constexpr int kFrontCenter = 0;
  static constexpr int kFrontLeft = 1;
  static constexpr int kFrontRight = 2;
  static constexpr int kRear = 3;

  void begin();
  void update(uint32_t nowMs);

  SonarReading reading(int i) const;
  bool fitted(int i) const;
  // Fitted, recent, and either a distance or a clean out-of-range echo.
  bool fresh(int i, uint32_t nowMs) const;
  // Median of the last three in-range readings that are still fresh; -1 if none.
  int filteredMm(int i, uint32_t nowMs) const;
  uint32_t measurementCount(int i) const { return count_[i]; }

 private:
  struct EchoCapture {
    volatile int64_t riseUs = 0;
    volatile int64_t fallUs = 0;
    volatile bool rose = false;
    volatile bool fell = false;
  };
  static void IRAM_ATTR onEchoEdge(void* arg);
  void store(int i, SonarStatus st, int mm, uint32_t nowMs);
  int nextScheduled(uint32_t nowMs);

  EchoCapture cap_[kCount];
  SonarReading last_[kCount];
  int history_[kCount][3] = {};
  uint32_t historyMs_[kCount][3] = {};
  uint8_t historyIdx_[kCount] = {};
  uint32_t count_[kCount] = {};
  uint32_t lastTrigMs_[kCount] = {};

  int active_ = -1;
  bool measured_ = false;
  uint32_t slotStartMs_ = 0;
  int64_t trigUs_ = 0;
  size_t schedIdx_ = 0;
};

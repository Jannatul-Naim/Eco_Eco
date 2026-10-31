// On-board ICMP echo ("ping") using ESP-IDF's ping session API.
// The echo request originates from the S3 itself, as the rulebook requires.
#pragma once

#include <Arduino.h>

#include "rover_types.h"

struct PingResult {
  PingState state = PingState::Idle;
  char target[16] = "";
  uint32_t sent = 0;
  uint32_t received = 0;
  uint32_t rttMs = 0;
  uint32_t startedMs = 0;
  uint32_t finishedMs = 0;
  char error[40] = "";
};

class Pinger {
 public:
  // `ip` must already be validated. Returns false (and records the error) if
  // a session could not be started.
  bool start(const char* ip, uint32_t count, uint32_t timeoutMs);
  PingResult result() const;
  bool busy() const;

 private:
  static void onSuccess(void* hdl, void* arg);
  static void onTimeout(void* hdl, void* arg);
  static void onEnd(void* hdl, void* arg);
  PingResult res_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

// Small thread-safe ring buffer of recent events, mirrored to the serial log.
#pragma once

#include <Arduino.h>

enum class EvLevel : uint8_t { Info, Warn, Error };

struct EventRecord {
  uint32_t seq;
  uint32_t ms;
  EvLevel level;
  char text[72];
};

class EventLog {
 public:
  static constexpr size_t kCapacity = 32;

  void add(EvLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  // Copies up to `max` most recent events (oldest first). Returns count.
  size_t recent(EventRecord* out, size_t max) const;
  uint32_t lastSeq() const;

 private:
  EventRecord ring_[kCapacity] = {};
  uint32_t nextSeq_ = 1;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern EventLog g_events;

inline const char* evLevelName(EvLevel l) {
  return l == EvLevel::Error ? "error" : (l == EvLevel::Warn ? "warn" : "info");
}

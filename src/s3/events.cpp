#include "events.h"

#include <stdarg.h>

EventLog g_events;

void EventLog::add(EvLevel level, const char* fmt, ...) {
  EventRecord rec;
  rec.ms = millis();
  rec.level = level;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(rec.text, sizeof(rec.text), fmt, ap);
  va_end(ap);

  portENTER_CRITICAL(&mux_);
  rec.seq = nextSeq_++;
  ring_[rec.seq % kCapacity] = rec;
  portEXIT_CRITICAL(&mux_);

  Serial.printf("[%8lu] %-5s %s\n", static_cast<unsigned long>(rec.ms), evLevelName(level), rec.text);
}

size_t EventLog::recent(EventRecord* out, size_t max) const {
  portENTER_CRITICAL(&mux_);
  const uint32_t last = nextSeq_ - 1;
  size_t n = last < max ? last : max;
  if (n > kCapacity) n = kCapacity;
  for (size_t i = 0; i < n; ++i) {
    const uint32_t seq = last - n + 1 + i;
    out[i] = ring_[seq % kCapacity];
  }
  portEXIT_CRITICAL(&mux_);
  return n;
}

uint32_t EventLog::lastSeq() const {
  portENTER_CRITICAL(&mux_);
  const uint32_t s = nextSeq_ - 1;
  portEXIT_CRITICAL(&mux_);
  return s;
}

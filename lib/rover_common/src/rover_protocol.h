// Inter-board link framing shared by the S3 and the ESP32-CAM.
//
// Wire format (one message per line, ASCII):
//     <json-object>*<CRC8 as 2 upper-case hex digits>\n
// The CRC-8 (poly 0x07, init 0x00) covers the JSON bytes only.
// Every JSON object carries "v" (protocol version) and "t" (message type).
// See docs/02_protocol_api.md for the message catalogue.
//
// SAFETY: there is deliberately no message type that carries a motor or
// drive command. The S3 parser only accepts the camera types listed below
// and only writes them into an observation struct. The S3 -> CAM "cfg"
// message carries a pan mode for the camera's own pan servo, which can only turn
// the camera.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace boardlink {

constexpr int kProtocolVersion = 2;  // 2: the pan servo moved to the camera board
constexpr size_t kMaxLine = 240;  // bytes including "*HH" but excluding '\n'
constexpr uint32_t kBaud = 115200;

// CAM -> S3 message types
constexpr const char* kTypeHeartbeat = "hb";
constexpr const char* kTypeQr = "qr";
constexpr const char* kTypeMotion = "mot";
// S3 -> CAM message types
constexpr const char* kTypeConfig = "cfg";

// ---- Camera pan servo (MG996R on the ESP32-CAM) -----------------------------
// The camera enforces these limits; the S3 validates commands against them and
// shows them on the dashboard. Widen the range only after checking the cable
// and bracket (docs/06 stage 6).
constexpr float kPanMinDeg = 45.0f;
constexpr float kPanMaxDeg = 135.0f;
constexpr float kPanHomeDeg = 90.0f;
constexpr float kPanPulseMinUs = 500.0f;  // raw-pulse calibration hard limits
constexpr float kPanPulseMaxUs = 2500.0f;

// What the S3 asks the camera to do with the servo (cfg "pm").
//   hold:  stay where it is (E-stop, safe stop, link lost, missions without pan)
//   goto:  move to "ang" (manual pan)
//   scan:  sweep the scan positions (QR search)
//   track: follow a motion target, sweep while there is none
//   pulse: output the raw pulse "pul" in microseconds (calibration, IDLE only)
enum class PanMode : uint8_t { Hold, Goto, Scan, Track, Pulse };
// What the camera reports (heartbeat "pan"). No position feedback: "settled"
// means the commanded angle was reached and the settle time has passed.
enum class PanState : uint8_t { Absent, Moving, Settled };

inline const char* panModeName(PanMode m) {
  switch (m) {
    case PanMode::Hold: return "hold";
    case PanMode::Goto: return "goto";
    case PanMode::Scan: return "scan";
    case PanMode::Track: return "track";
    case PanMode::Pulse: return "pulse";
  }
  return "hold";
}

// Unknown or missing names fall back to Hold, the mode that never moves.
inline PanMode parsePanMode(const char* s) {
  if (!s) return PanMode::Hold;
  if (!strcmp(s, "goto")) return PanMode::Goto;
  if (!strcmp(s, "scan")) return PanMode::Scan;
  if (!strcmp(s, "track")) return PanMode::Track;
  if (!strcmp(s, "pulse")) return PanMode::Pulse;
  return PanMode::Hold;
}

inline const char* panStateName(PanState s) {
  switch (s) {
    case PanState::Absent: return "absent";
    case PanState::Moving: return "moving";
    case PanState::Settled: return "settled";
  }
  return "absent";
}

inline PanState parsePanState(const char* s) {
  if (s && !strcmp(s, "moving")) return PanState::Moving;
  if (s && !strcmp(s, "settled")) return PanState::Settled;
  return PanState::Absent;
}

inline uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Appends "*HH" to json. Returns the framed length (without '\n'),
// or 0 if it does not fit in `cap` or exceeds kMaxLine.
inline size_t frame(char* out, size_t cap, const char* json, size_t jsonLen) {
  const size_t total = jsonLen + 3;
  if (total > kMaxLine || total + 1 > cap) return 0;
  static const char kHex[] = "0123456789ABCDEF";
  memcpy(out, json, jsonLen);
  const uint8_t c = crc8(reinterpret_cast<const uint8_t*>(json), jsonLen);
  out[jsonLen] = '*';
  out[jsonLen + 1] = kHex[c >> 4];
  out[jsonLen + 2] = kHex[c & 0x0F];
  out[total] = '\0';
  return total;
}

// Validates a received line (without '\n'; a trailing '\r' is tolerated).
// On success returns true and sets *jsonLen to the length of the JSON part.
inline bool unframe(const char* line, size_t len, size_t* jsonLen) {
  if (len > 0 && line[len - 1] == '\r') --len;
  if (len < 5 || len > kMaxLine) return false;  // shortest: {}*HH
  if (line[len - 3] != '*' || line[0] != '{') return false;
  const int hi = hexNibble(line[len - 2]);
  const int lo = hexNibble(line[len - 1]);
  if (hi < 0 || lo < 0) return false;
  const size_t n = len - 3;
  if (line[n - 1] != '}') return false;
  if (crc8(reinterpret_cast<const uint8_t*>(line), n) != static_cast<uint8_t>((hi << 4) | lo)) {
    return false;
  }
  *jsonLen = n;
  return true;
}

// Bounded line assembler for a byte stream. Over-long lines are discarded
// entirely (the parser resynchronises on the next '\n').
class LineAssembler {
 public:
  // Feed one byte; returns true when a complete line is available in line().
  bool push(char c) {
    if (c == '\n') {
      const bool ok = !overflow_ && len_ > 0;
      buf_[len_] = '\0';
      complete_ = len_;
      len_ = 0;
      overflow_ = false;
      return ok;
    }
    if (overflow_) return false;
    if (len_ >= kMaxLine + 1) {  // +1 tolerates a '\r'
      overflow_ = true;
      ++overflows_;
      len_ = 0;
      return false;
    }
    buf_[len_++] = c;
    return false;
  }
  const char* line() const { return buf_; }
  size_t length() const { return complete_; }
  uint32_t overflows() const { return overflows_; }

 private:
  char buf_[kMaxLine + 2] = {0};
  size_t len_ = 0;
  size_t complete_ = 0;
  bool overflow_ = false;
  uint32_t overflows_ = 0;
};

}  // namespace boardlink

// Strict IPv4 validation for QR payloads. Used on the camera board (first
// check) and again on the S3 before it sends any ICMP echo request.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ipv4 {

enum class Result : uint8_t {
  kOk = 0,
  kSyntax,       // not a dotted quad of 0..255 without leading zeros
  kNotUnicast,   // 0.x.x.x, 127.x.x.x, 224.0.0.0 and above (multicast/reserved/broadcast)
  kNone,         // no candidate found in the payload
  kAmbiguous,    // more than one different address in the payload
};

inline const char* resultName(Result r) {
  switch (r) {
    case Result::kOk: return "ok";
    case Result::kSyntax: return "not_ipv4";
    case Result::kNotUnicast: return "not_unicast";
    case Result::kNone: return "no_ipv4";
    case Result::kAmbiguous: return "ambiguous";
  }
  return "unknown";
}

// Parses exactly `len` characters as a dotted quad. No whitespace, no
// leading '+', no leading zeros ("010" rejected, "0" accepted).
inline bool parseStrict(const char* s, size_t len, uint8_t out[4]) {
  if (len < 7 || len > 15) return false;
  int octet = 0;
  size_t i = 0;
  while (octet < 4) {
    const size_t start = i;
    unsigned value = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
      value = value * 10 + static_cast<unsigned>(s[i] - '0');
      ++i;
      if (i - start > 3) return false;
    }
    const size_t digits = i - start;
    if (digits == 0 || value > 255) return false;
    if (digits > 1 && s[start] == '0') return false;
    out[octet++] = static_cast<uint8_t>(value);
    if (octet < 4) {
      if (i >= len || s[i] != '.') return false;
      ++i;
    }
  }
  return i == len;
}

inline Result classify(const uint8_t a[4]) {
  if (a[0] == 0 || a[0] == 127 || a[0] >= 224) return Result::kNotUnicast;
  return Result::kOk;
}

inline void format(const uint8_t a[4], char out[16]) {
  snprintf(out, 16, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
}

// Validates a whole string (surrounding ASCII whitespace is ignored).
inline Result validate(const char* s, size_t len, char out[16]) {
  while (len > 0 && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) { ++s; --len; }
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) --len;
  uint8_t a[4];
  if (!parseStrict(s, len, a)) return Result::kSyntax;
  const Result r = classify(a);
  if (r == Result::kOk) format(a, out);
  return r;
}

// Finds IPv4 candidates inside free text (e.g. "Server: 10.0.0.5").
// Exactly one distinct valid unicast address must be present.
inline Result extractUnique(const char* s, size_t len, char out[16]) {
  uint8_t found[4] = {0, 0, 0, 0};
  int distinct = 0;
  bool sawSyntaxCandidate = false;
  bool sawNonUnicast = false;
  auto digitOrDot = [](char c) { return (c >= '0' && c <= '9') || c == '.'; };
  size_t i = 0;
  while (i < len) {
    if (!digitOrDot(s[i])) { ++i; continue; }
    // Maximal run of digits and dots, bounded by anything else.
    size_t j = i;
    while (j < len && digitOrDot(s[j])) ++j;
    size_t a0 = i, b0 = j;
    while (a0 < b0 && s[a0] == '.') ++a0;      // leading dots
    while (b0 > a0 && s[b0 - 1] == '.') --b0;  // sentence-ending dots
    const size_t runLen = b0 - a0;
    if (runLen > 0 && memchr(s + a0, '.', runLen) != nullptr) {
      uint8_t a[4];
      if (parseStrict(s + a0, runLen, a)) {
        if (classify(a) == Result::kOk) {
          if (distinct == 0) {
            memcpy(found, a, 4);
            distinct = 1;
          } else if (memcmp(found, a, 4) != 0) {
            distinct = 2;
          }
        } else {
          sawNonUnicast = true;
        }
      } else {
        sawSyntaxCandidate = true;
      }
    }
    i = j;
  }
  if (distinct == 1) {
    format(found, out);
    return Result::kOk;
  }
  if (distinct > 1) return Result::kAmbiguous;
  if (sawNonUnicast) return Result::kNotUnicast;
  if (sawSyntaxCandidate) return Result::kSyntax;
  return Result::kNone;
}

}  // namespace ipv4

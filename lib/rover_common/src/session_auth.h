// Dashboard login: one shared password, a few session tokens kept in RAM.
// Header-only and Arduino-free so the native tests can run it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace auth {

constexpr size_t kTokenBytes = 16;
constexpr size_t kTokenHexLen = 2 * kTokenBytes;

// Takes the same time wherever the first difference is. An empty expected
// value never matches, so an unset password cannot be "guessed" as "".
inline bool matches(const char* given, const char* expected) {
  if (!given || !expected || !expected[0]) return false;
  const size_t gl = strlen(given);
  const size_t el = strlen(expected);
  uint8_t diff = gl != el;
  for (size_t i = 0; i < gl; ++i) diff |= static_cast<uint8_t>(given[i] ^ expected[i % el]);
  return diff == 0;
}

// 16 random bytes -> 32 lowercase hex characters plus a terminator.
inline void tokenFromBytes(const uint8_t bytes[kTokenBytes], char out[kTokenHexLen + 1]) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < kTokenBytes; ++i) {
    out[2 * i] = kHex[bytes[i] >> 4];
    out[2 * i + 1] = kHex[bytes[i] & 0x0f];
  }
  out[kTokenHexLen] = '\0';
}

// The newest kSlots tokens stay valid until logout or reboot; a new login
// pushes out the oldest.
template <size_t kSlots>
class Sessions {
 public:
  void add(const char* token) {
    strncpy(slot_[next_], token, kTokenHexLen);
    slot_[next_][kTokenHexLen] = '\0';
    next_ = (next_ + 1) % kSlots;
  }
  bool valid(const char* token) const {
    if (!token || strlen(token) != kTokenHexLen) return false;
    for (const auto& s : slot_) {
      if (s[0] && matches(token, s)) return true;
    }
    return false;
  }
  void remove(const char* token) {
    if (!token) return;
    for (auto& s : slot_) {
      if (s[0] && strcmp(s, token) == 0) s[0] = '\0';
    }
  }

 private:
  char slot_[kSlots][kTokenHexLen + 1] = {};
  size_t next_ = 0;
};

}  // namespace auth

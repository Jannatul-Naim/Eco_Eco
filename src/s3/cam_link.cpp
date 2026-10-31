#include "cam_link.h"

#include <ArduinoJson.h>

#include "config_network.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "events.h"

namespace {
HardwareSerial& camSerial() { return Serial1; }

void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) src = "";
  size_t n = 0;
  // Keep printable ASCII only; the text ends up in JSON and on the dashboard.
  for (; *src && n + 1 < cap; ++src) {
    const char c = *src;
    dst[n++] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  dst[n] = '\0';
}
}  // namespace

void CamLink::begin() {
  camSerial().setRxBufferSize(1024);
  camSerial().begin(boardlink::kBaud, SERIAL_8N1, PIN_CAM_UART_RX, PIN_CAM_UART_TX);
}

bool CamLink::fresh(uint32_t nowMs) const {
  return st_.everSeen && nowMs - st_.lastHbMs <= CAM_LINK_STALE_MS;
}

const char* CamLink::linkName(uint32_t nowMs) const {
  if (!st_.everSeen) return "never";
  return fresh(nowMs) ? "ok" : "stale";
}

void CamLink::poll(uint32_t nowMs) {
  int budget = 512;  // bounded work per control tick
  while (budget-- > 0 && camSerial().available() > 0) {
    const char c = static_cast<char>(camSerial().read());
    if (rx_.push(c)) handleLine(rx_.line(), rx_.length(), nowMs);
  }
}

void CamLink::handleLine(const char* line, size_t len, uint32_t nowMs) {
  size_t jsonLen = 0;
  if (!boardlink::unframe(line, len, &jsonLen)) {
    ++st_.rxBad;
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, line, jsonLen)) {
    ++st_.rxBad;
    return;
  }
  if ((doc["v"] | -1) != boardlink::kProtocolVersion) {
    if (st_.versionMismatch++ == 0) {
      g_events.add(EvLevel::Error, "camera protocol version %d != %d", doc["v"] | -1, boardlink::kProtocolVersion);
    }
    return;
  }
  const char* type = doc["t"] | "";
  if (!strcmp(type, boardlink::kTypeHeartbeat)) {
    if (!st_.everSeen || !fresh(nowMs)) g_events.add(EvLevel::Info, "camera link up (%s)", doc["ip"] | "no ip");
    st_.everSeen = true;
    st_.lastHbMs = nowMs;
    copyStr(st_.ip, sizeof(st_.ip), doc["ip"] | "");
    copyStr(st_.sensor, sizeof(st_.sensor), doc["sen"] | "");
    copyStr(st_.pid, sizeof(st_.pid), doc["pid"] | "");
    st_.fps = doc["fps"] | 0.0f;
    st_.camUptimeMs = doc["up"] | 0u;
    st_.qrArmed = (doc["qr"] | 0) != 0;
    st_.motArmed = (doc["mot"] | 0) != 0;
  } else if (!strcmp(type, boardlink::kTypeQr)) {
    st_.qrSeq++;
    st_.qrMs = nowMs;
    st_.qrValid = (doc["ok"] | 0) != 0;
    copyStr(st_.qrText, sizeof(st_.qrText), doc["txt"] | "");
    copyStr(st_.qrIp, sizeof(st_.qrIp), doc["ip"] | "");
    copyStr(st_.qrErr, sizeof(st_.qrErr), doc["err"] | "");
  } else if (!strcmp(type, boardlink::kTypeMotion)) {
    st_.motSeq++;
    st_.motMs = nowMs;
    st_.motDetected = (doc["det"] | 0) != 0;
    st_.motCx = clampf(doc["cx"] | 0.0f, -1.0f, 1.0f);
    st_.motCy = clampf(doc["cy"] | 0.0f, -1.0f, 1.0f);
    st_.motArea = clampf(doc["a"] | 0.0f, 0.0f, 1.0f);
    st_.motEpoch = doc["ep"] | 0u;
  } else {
    ++st_.rxBad;  // unknown types are ignored; there is no command channel
    return;
  }
  st_.lastRxMs = nowMs;
  ++st_.rxOk;
}

void CamLink::request(const CamRequest& req, uint32_t nowMs) {
  const bool changed = req.qr != lastSent_.qr || req.motion != lastSent_.motion || req.pan != lastSent_.pan ||
                       req.panEpoch != lastSent_.panEpoch;
  if (!changed && nowMs - lastSendMs_ < CAM_CFG_PERIOD_MS) return;
  send(req);
  lastSent_ = req;
  lastSendMs_ = nowMs;
}

void CamLink::send(const CamRequest& req) {
  char json[160];
  const int n = snprintf(json, sizeof(json),
                         "{\"v\":%d,\"t\":\"%s\",\"s\":%lu,\"qr\":%d,\"mot\":%d,\"pan\":\"%s\",\"ang\":%.1f,\"ep\":%lu}",
                         boardlink::kProtocolVersion, boardlink::kTypeConfig, static_cast<unsigned long>(++txSeq_),
                         req.qr ? 1 : 0, req.motion ? 1 : 0, panStateName(req.pan), req.panDeg,
                         static_cast<unsigned long>(req.panEpoch));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(json)) return;
  char framed[boardlink::kMaxLine + 2];
  const size_t len = boardlink::frame(framed, sizeof(framed), json, static_cast<size_t>(n));
  if (len == 0) return;
  framed[len] = '\n';
  camSerial().write(reinterpret_cast<const uint8_t*>(framed), len + 1);
}

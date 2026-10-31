#include "cam_link.h"

#include <ArduinoJson.h>

#include "config_cam.h"
#include "rover_protocol.h"

namespace {
boardlink::LineAssembler g_rx;
LinkConfig g_cfg;
uint32_t g_cfgMs = 0;
uint32_t g_txSeq = 0;
uint32_t g_lastHbMs = 0;
portMUX_TYPE g_cfgMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t g_txLock = nullptr;

void sendJson(JsonDocument& doc) {
  char json[boardlink::kMaxLine];
  const size_t n = serializeJson(doc, json, sizeof(json));
  if (n == 0 || n >= sizeof(json) - 4) return;
  char framed[boardlink::kMaxLine + 2];
  const size_t len = boardlink::frame(framed, sizeof(framed), json, n);
  if (len == 0) return;
  framed[len] = '\n';
  if (xSemaphoreTake(g_txLock, pdMS_TO_TICKS(50)) == pdTRUE) {
    Serial1.write(reinterpret_cast<const uint8_t*>(framed), len + 1);
    xSemaphoreGive(g_txLock);
  }
}

void handleLine(const char* line, size_t len, uint32_t nowMs) {
  size_t jsonLen = 0;
  if (!boardlink::unframe(line, len, &jsonLen)) return;
  JsonDocument doc;
  if (deserializeJson(doc, line, jsonLen)) return;
  if ((doc["v"] | -1) != boardlink::kProtocolVersion) return;
  if (strcmp(doc["t"] | "", boardlink::kTypeConfig) != 0) return;
  LinkConfig c;
  c.qr = (doc["qr"] | 0) != 0;
  c.motion = (doc["mot"] | 0) != 0;
  const char* pan = doc["pan"] | "absent";
  c.pan = !strcmp(pan, "moving") ? PanInfo::Moving : (!strcmp(pan, "settled") ? PanInfo::Settled : PanInfo::Absent);
  c.panEpoch = doc["ep"] | 0u;
  c.fresh = true;
  portENTER_CRITICAL(&g_cfgMux);
  g_cfg = c;
  g_cfgMs = nowMs;
  portEXIT_CRITICAL(&g_cfgMux);
}
}  // namespace

void linkBegin() {
  g_txLock = xSemaphoreCreateMutex();
  Serial1.setRxBufferSize(512);
  Serial1.begin(boardlink::kBaud, SERIAL_8N1, LINK_PIN_RX, LINK_PIN_TX);
}

LinkConfig linkConfig() {
  portENTER_CRITICAL(&g_cfgMux);
  LinkConfig c = g_cfg;
  const uint32_t at = g_cfgMs;
  portEXIT_CRITICAL(&g_cfgMux);
  if (at == 0 || millis() - at > LINK_CFG_STALE_MS) {
    c = LinkConfig();  // S3 silent: detectors off, treat pan as unknown/absent
  }
  return c;
}

void linkPoll(uint32_t nowMs, const CamHealth& h) {
  int budget = 512;
  while (budget-- > 0 && Serial1.available() > 0) {
    if (g_rx.push(static_cast<char>(Serial1.read()))) handleLine(g_rx.line(), g_rx.length(), nowMs);
  }
  if (nowMs - g_lastHbMs < LINK_HB_PERIOD_MS) return;
  g_lastHbMs = nowMs;
  const LinkConfig c = linkConfig();
  JsonDocument doc;
  doc["v"] = boardlink::kProtocolVersion;
  doc["t"] = boardlink::kTypeHeartbeat;
  doc["s"] = ++g_txSeq;
  doc["up"] = nowMs;
  doc["ip"] = h.ip;
  doc["sen"] = h.sensor;
  doc["pid"] = h.pid;
  doc["fps"] = roundf(h.fps * 10.0f) / 10.0f;
  doc["qr"] = c.qr ? 1 : 0;
  doc["mot"] = c.motion ? 1 : 0;
  doc["cam"] = h.cameraOk ? 1 : 0;
  doc["rst"] = h.resetReason;
  sendJson(doc);
}

void linkSendQr(bool ok, const char* ip, const char* text, const char* err) {
  JsonDocument doc;
  doc["v"] = boardlink::kProtocolVersion;
  doc["t"] = boardlink::kTypeQr;
  doc["s"] = ++g_txSeq;
  doc["ok"] = ok ? 1 : 0;
  doc["ip"] = ip;
  doc["txt"] = text;
  doc["err"] = err;
  sendJson(doc);
}

void linkSendMotion(bool detected, float cx, float cy, float area, uint32_t epoch, bool baseline) {
  JsonDocument doc;
  doc["v"] = boardlink::kProtocolVersion;
  doc["t"] = boardlink::kTypeMotion;
  doc["s"] = ++g_txSeq;
  doc["det"] = detected ? 1 : 0;
  doc["cx"] = roundf(cx * 100.0f) / 100.0f;
  doc["cy"] = roundf(cy * 100.0f) / 100.0f;
  doc["a"] = roundf(area * 1000.0f) / 1000.0f;
  doc["ep"] = epoch;
  doc["base"] = baseline ? 1 : 0;
  sendJson(doc);
}

#include "cam_link.h"

#include <ArduinoJson.h>

#include "config_cam.h"
#include "pan.h"
#include "rover_protocol.h"

namespace {
boardlink::LineAssembler g_rx;
LinkConfig g_cfg;
uint32_t g_cfgMs = 0;
LinkConfig g_bench;
uint32_t g_benchMs = 0;
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
  c.panMode = boardlink::parsePanMode(doc["pm"] | "hold");
  c.panDeg = doc["ang"] | boardlink::kPanHomeDeg;
  c.pulseUs = doc["pul"] | 0.0f;
  c.source = CfgSource::S3;
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
  const LinkConfig c = g_cfg;
  const uint32_t at = g_cfgMs;
  const LinkConfig b = g_bench;
  const uint32_t benchAt = g_benchMs;
  portEXIT_CRITICAL(&g_cfgMux);
  const uint32_t now = millis();
  if (at != 0 && now - at <= LINK_CFG_STALE_MS) return c;  // the S3 is in charge
  if (benchAt != 0 && now - benchAt <= BENCH_KEEPALIVE_MS) return b;
  return LinkConfig();  // nobody: detectors off, pan holds where it is
}

void linkSetBench(const LinkConfig& c) {
  LinkConfig b = c;
  if (b.panMode == boardlink::PanMode::Pulse) b.panMode = boardlink::PanMode::Hold;  // calibration stays on the S3
  b.source = CfgSource::Bench;
  b.fresh = true;
  portENTER_CRITICAL(&g_cfgMux);
  g_bench = b;
  g_benchMs = millis();
  portEXIT_CRITICAL(&g_cfgMux);
}

void linkPoll(uint32_t nowMs, const CamHealth& h) {
  int budget = 512;
  while (budget-- > 0 && Serial1.available() > 0) {
    if (g_rx.push(static_cast<char>(Serial1.read()))) handleLine(g_rx.line(), g_rx.length(), nowMs);
  }
  if (nowMs - g_lastHbMs < LINK_HB_PERIOD_MS) return;
  g_lastHbMs = nowMs;
  const LinkConfig c = linkConfig();
  const PanStatus p = panStatus();
  // Worst case is ~210 bytes; boardlink::kMaxLine is 240.
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
  doc["pm"] = boardlink::panModeName(p.mode);
  doc["pan"] = boardlink::panStateName(p.state);
  doc["ang"] = roundf(p.angle * 10.0f) / 10.0f;
  doc["tgt"] = roundf(p.target * 10.0f) / 10.0f;
  doc["pul"] = static_cast<int>(p.pulseUs);
  doc["trk"] = p.tracking ? 1 : 0;
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

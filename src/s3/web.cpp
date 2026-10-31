#include "web.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <math.h>

#include "config_hardware.h"
#include "config_network.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "events.h"
#include "net.h"
#include "rover.h"

namespace {
AsyncWebServer g_server(HTTP_PORT);
AsyncWebSocket g_ws("/ws");
constexpr size_t kMaxBody = 512;
constexpr uint32_t kHandlerLockMs = 50;
uint32_t g_lastTelemetryMs = 0;
uint32_t g_telemetrySeq = 0;
uint32_t g_lastEventSeqSent = 0;
volatile bool g_resendEvents = false;
uint32_t g_lastWsErrorMs = 0;

// HTTP handlers all run in the AsyncTCP task, one at a time.
RoverSnapshot g_httpSnap;
RoverSnapshot g_loopSnap;

template <typename T>
void setOrNull(JsonObject o, const char* key, bool valid, T value) {
  if (valid) {
    o[key] = value;
  } else {
    o[key] = nullptr;
  }
}

float r2(float v) { return roundf(v * 100.0f) / 100.0f; }

void fillStatus(JsonObject o, const RoverSnapshot& s) {
  const NetInfo n = netInfo();
  o["ok"] = true;
  o["proto"] = 1;
  o["fw"] = FW_VERSION;
  o["uptime_ms"] = s.nowMs;
  JsonObject c = o["connection"].to<JsonObject>();
  c["wifi"] = n.mode;
  c["ssid"] = n.ssid;
  c["ip"] = n.ip;
  setOrNull(c, "rssi", strcmp(n.mode, "sta") == 0, n.rssi);
  c["hostname"] = ROVER_HOSTNAME;
  c["ws_clients"] = g_ws.count();
  o["mode"] = modeName(s.mode);
  o["mode_changed_ms"] = s.modeChangedMs;
  o["mission"] = missionName(s.mission);
  o["state"] = stateName(s.state);
  o["state_changed_ms"] = s.stateChangedMs;
  o["detail"] = s.detail;
  o["controls_locked"] = s.mode == Mode::Auto;
  JsonObject e = o["estop"].to<JsonObject>();
  e["active"] = s.estopActive;
  e["hw_pressed"] = s.estopHwPressed;
  e["hw_sense"] = s.estopSense;
  e["source"] = s.estopSource;
  e["reset_allowed"] = s.estopActive && !s.estopHwPressed;
  JsonObject b = o["battery"].to<JsonObject>();
  b["wired"] = s.battWired;
  const bool battOk = s.battWired && s.battV > 1.0f;
  setOrNull(b, "v", battOk, r2(s.battV));
  setOrNull(b, "pct", battOk && s.battPct >= 0, s.battPct);
  setOrNull(b, "low", battOk, s.battV < BATTERY_WARN_V);
  JsonObject mb = o["motor_bus"].to<JsonObject>();
  mb["wired"] = s.busWired;
  setOrNull(mb, "v", s.busWired, r2(s.busV));
  setOrNull(mb, "present", s.busWired, s.busV > 6.0f);
  JsonObject m = o["motors"].to<JsonObject>();
  m["armed"] = s.motorsArmed;
  m["left"] = r2(s.outLeft);
  m["right"] = r2(s.outRight);
  m["driver"] = MOTOR_DRIVER_TYPE == DRIVER_BTS7960 ? "BTS7960" : "MD30C";
  m["teleop_max"] = TELEOP_MAX_DUTY;
  o["pinmap"] = ROVER_PINMAP_STATUS;
  o["free_heap"] = ESP.getFreeHeap();
}

void fillSensors(JsonObject o, const RoverSnapshot& s) {
  o["ok"] = true;
  o["t_ms"] = s.nowMs;
  JsonArray arr = o["sonar"].to<JsonArray>();
  for (int i = 0; i < 4; ++i) {
    JsonObject x = arr.add<JsonObject>();
    const SonarReading& r = s.sonar[i];
    x["id"] = SONAR_NAME[i];
    x["fitted"] = s.sonarFitted[i];
    x["status"] = sonarStatusName(r.status);
    setOrNull(x, "mm", r.mm >= 0 && r.status == SonarStatus::Ok, r.mm);
    setOrNull(x, "t_ms", r.tMs != 0, r.tMs);
    setOrNull(x, "age_ms", r.tMs != 0, s.nowMs - r.tMs);
  }
  JsonObject ri = o["rear_interlock"].to<JsonObject>();
  ri["reverse_allowed"] = s.reverseAllowed;
  ri["reason"] = s.reverseReason;
  ri["clearance_mm"] = REAR_CLEARANCE_MM;

  JsonObject p = o["proximity"].to<JsonObject>();
  p["target_mm"] = PROX_TARGET_MM;
  p["tol_mm"] = PROX_TOLERANCE_MM;
  setOrNull(p, "front_mm", s.frontFilteredMm >= 0, s.frontFilteredMm);
  setOrNull(p, "in_window", s.frontFilteredMm >= 0,
            abs(s.frontFilteredMm - PROX_TARGET_MM) <= PROX_TOLERANCE_MM);
  p["t_ms"] = s.sonar[0].tMs;

  const CamState& cs = s.cam;
  JsonObject c = o["camera"].to<JsonObject>();
  c["link"] = s.camLink;
  c["ip"] = cs.ip;
  if (cs.ip[0]) {
    char url[48];
    snprintf(url, sizeof(url), "http://%s:%u/stream", cs.ip, CAM_STREAM_PORT);
    c["stream_url"] = url;
  } else {
    c["stream_url"] = nullptr;
  }
  c["fps"] = r2(cs.fps);
  c["sensor"] = cs.sensor;
  c["pid"] = cs.pid;
  setOrNull(c, "hb_age_ms", cs.everSeen, s.nowMs - cs.lastHbMs);
  setOrNull(c, "t_ms", cs.everSeen, cs.lastHbMs);
  c["rx_ok"] = cs.rxOk;
  c["rx_bad"] = cs.rxBad;

  JsonObject pan = o["pan"].to<JsonObject>();
  pan["present"] = s.panPresent;
  pan["state"] = panStateName(s.pan);
  pan["angle"] = r2(s.panDeg);
  pan["target"] = r2(s.panTarget);
  pan["pulse_us"] = static_cast<int>(s.panPulseUs);
  pan["soft_min"] = SERVO_SOFT_MIN_DEG;
  pan["soft_max"] = SERVO_SOFT_MAX_DEG;
  pan["t_ms"] = s.nowMs;

  JsonObject q = o["qr"].to<JsonObject>();
  q["requested"] = s.qrRequested;
  q["camera_armed"] = cs.qrArmed;
  q["text"] = cs.qrText;
  q["ip"] = cs.qrIp;
  q["valid"] = cs.qrValid;
  q["error"] = cs.qrErr;
  setOrNull(q, "t_ms", cs.qrMs != 0, cs.qrMs);
  q["last_valid_ip"] = s.lastValidQrIp;
  setOrNull(q, "last_valid_t_ms", s.lastValidQrMs != 0, s.lastValidQrMs);

  JsonObject pg = o["ping"].to<JsonObject>();
  pg["state"] = pingStateName(s.ping.state);
  pg["target"] = s.ping.target;
  pg["sent"] = s.ping.sent;
  pg["received"] = s.ping.received;
  setOrNull(pg, "rtt_ms", s.ping.state == PingState::Reply, s.ping.rttMs);
  pg["error"] = s.ping.error;
  setOrNull(pg, "t_ms", s.ping.startedMs != 0, s.ping.finishedMs ? s.ping.finishedMs : s.ping.startedMs);

  JsonObject mo = o["motion"].to<JsonObject>();
  mo["enabled"] = s.motionRequested;
  mo["camera_armed"] = cs.motArmed;
  const bool motFresh = cs.motMs != 0 && s.nowMs - cs.motMs < 1000;
  mo["detected"] = motFresh && cs.motDetected;
  mo["cx"] = r2(cs.motCx);
  mo["cy"] = r2(cs.motCy);
  mo["area"] = r2(cs.motArea);
  mo["tracking"] = s.motionTracking;
  mo["tracked_ms"] = s.motionTrackedMs;
  mo["goal_ms"] = MOTION_GOAL_MS;
  setOrNull(mo, "t_ms", cs.motMs != 0, cs.motMs);

  const AudioStatus a = audioStatus(s.nowMs);
  JsonObject au = o["audio"].to<JsonObject>();
  au["online"] = a.online;
  au["ip"] = a.ip;
  au["mic_level"] = a.micLevel;
  au["clients"] = a.clients;
  au["speaker_active"] = a.speakerActive;
  setOrNull(au, "t_ms", a.lastMs != 0, a.lastMs);
}

void fillEvents(JsonArray arr, size_t max) {
  EventRecord recs[EventLog::kCapacity];
  const size_t n = g_events.recent(recs, max);
  for (size_t i = 0; i < n; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["seq"] = recs[i].seq;
    e["t_ms"] = recs[i].ms;
    e["level"] = evLevelName(recs[i].level);
    e["text"] = recs[i].text;
  }
}

void sendJson(AsyncWebServerRequest* req, int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

void sendError(AsyncWebServerRequest* req, int code, const char* msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(req, code, doc);
}

void collectBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (total > kMaxBody) return;
  if (index == 0 && req->_tempObject == nullptr) req->_tempObject = calloc(total + 1, 1);
  if (req->_tempObject && index + len <= total) memcpy(static_cast<char*>(req->_tempObject) + index, data, len);
}

using CommandFn = CmdResult (*)(JsonVariantConst body);

// POST route: parses an optional JSON body, runs the command under the rover
// lock, and replies {"ok":..,"message"|"error":..,"mode":..,"state":..}.
void postRoute(const char* path, CommandFn fn) {
  g_server.on(
      path, HTTP_POST,
      [fn](AsyncWebServerRequest* req) {
        if (req->contentLength() > kMaxBody) return sendError(req, 413, "body too large");
        JsonDocument body;
        const char* raw = static_cast<const char*>(req->_tempObject);
        if (raw && raw[0] && deserializeJson(body, raw)) return sendError(req, 400, "invalid JSON body");
        CmdResult r{false, 503, "rover busy, retry"};
        {
          RoverLock lock(kHandlerLockMs);
          if (lock.ok()) {
            r = fn(body.as<JsonVariantConst>());
            g_rover.snapshot(&g_httpSnap);
          }
        }
        JsonDocument doc;
        doc["ok"] = r.ok;
        doc[r.ok ? "message" : "error"] = r.message;
        if (r.http != 503) {
          doc["mode"] = modeName(g_httpSnap.mode);
          doc["state"] = stateName(g_httpSnap.state);
          doc["mission"] = missionName(g_httpSnap.mission);
        }
        sendJson(req, r.http, doc);
      },
      nullptr, collectBody);
}

bool snapshotNow(RoverSnapshot* snap) {
  RoverLock lock(kHandlerLockMs);
  if (!lock.ok()) return false;
  g_rover.snapshot(snap);
  return true;
}

void handleWsText(AsyncWebSocketClient* client, const uint8_t* data, size_t len) {
  JsonDocument d;
  if (deserializeJson(d, data, len)) return;
  const char* type = d["type"] | "";
  if (!strcmp(type, "drive")) {
    CmdResult r{false, 503, "rover busy"};
    {
      RoverLock lock(20);
      if (lock.ok()) r = g_rover.drive(d["left"] | NAN, d["right"] | NAN, CmdSource::WebSocket);
    }
    const uint32_t now = millis();
    if (!r.ok && now - g_lastWsErrorMs > 500) {
      g_lastWsErrorMs = now;
      char msg[128];
      snprintf(msg, sizeof(msg), "{\"type\":\"ack\",\"cmd\":\"drive\",\"ok\":false,\"error\":\"%s\"}", r.message);
      client->text(msg);
    }
  } else if (!strcmp(type, "estop")) {
    g_rover.requestEstop();
    {
      RoverLock lock(kHandlerLockMs);
      if (lock.ok()) g_rover.engageEstop(CmdSource::WebSocket);
    }
    client->text("{\"type\":\"ack\",\"cmd\":\"estop\",\"ok\":true}");
  } else if (!strcmp(type, "ping")) {
    char msg[80];
    snprintf(msg, sizeof(msg), "{\"type\":\"pong\",\"c\":%.0f,\"t_ms\":%lu}", static_cast<double>(d["c"] | 0.0),
             static_cast<unsigned long>(millis()));
    client->text(msg);
  }
}

void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data,
               size_t len) {
  (void)server;
  switch (type) {
    case WS_EVT_CONNECT:
      g_events.add(EvLevel::Info, "dashboard connected (ws #%lu from %s)", static_cast<unsigned long>(client->id()),
                   client->remoteIP().toString().c_str());
      g_resendEvents = true;
      break;
    case WS_EVT_DISCONNECT:
      g_events.add(EvLevel::Info, "dashboard disconnected (ws #%lu)", static_cast<unsigned long>(client->id()));
      break;
    case WS_EVT_DATA: {
      const AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT && len <= 256) {
        handleWsText(client, data, len);
      }
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------- commands

CmdResult cmdMode(JsonVariantConst b) {
  Mode m;
  if (!parseMode(b["mode"] | "", &m)) return CmdResult::bad("body must be {\"mode\":\"teleop\"|\"auto\"|\"idle\"}");
  Mission mission = Mission::None;
  const bool given = !b["mission"].isNull();
  if (given && !parseMission(b["mission"] | "", &mission)) {
    return CmdResult::bad("mission must be traverse|proximity|qr_ping|motion");
  }
  return g_rover.setMode(m, mission, given, CmdSource::Http);
}
CmdResult cmdDrive(JsonVariantConst b) {
  if (!b["left"].is<float>() || !b["right"].is<float>()) {
    return CmdResult::bad("body must be {\"left\":-1..1,\"right\":-1..1}");
  }
  return g_rover.drive(b["left"].as<float>(), b["right"].as<float>(), CmdSource::Http);
}
CmdResult cmdEstopReset(JsonVariantConst) { return g_rover.resetEstop(CmdSource::Http); }
CmdResult cmdStart(JsonVariantConst) { return g_rover.startMission(CmdSource::Http); }
CmdResult cmdAbort(JsonVariantConst) { return g_rover.abortMission(CmdSource::Http); }
CmdResult cmdAck(JsonVariantConst) { return g_rover.ackFault(CmdSource::Http); }
CmdResult cmdQrPing(JsonVariantConst) { return g_rover.triggerQrPing(CmdSource::Http); }
CmdResult cmdServo(JsonVariantConst b) {
  if (b["pulse_us"].is<float>()) return g_rover.setPanPulse(b["pulse_us"].as<float>(), CmdSource::Http);
  if (!b["angle"].is<float>()) return CmdResult::bad("body must be {\"angle\":deg} or {\"pulse_us\":us}");
  return g_rover.setPan(b["angle"].as<float>(), CmdSource::Http);
}
CmdResult cmdMotionDetect(JsonVariantConst b) {
  if (!b["enabled"].is<bool>()) return CmdResult::bad("body must be {\"enabled\":true|false}");
  return g_rover.setMotionDetect(b["enabled"].as<bool>(), CmdSource::Http);
}

const char kNoFsPage[] PROGMEM =
    "<!doctype html><meta charset=utf-8><title>URRT rover</title><body style='background:#000;color:#fc0;"
    "font-family:sans-serif'><h1>Dashboard files not uploaded</h1><p>Run <code>pio run -e s3_main -t uploadfs"
    "</code>. The API is live at <a href='/api/status' style='color:#fc0'>/api/status</a>.</p>";
}  // namespace

void webBegin(bool filesystemOk) {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  g_ws.onEvent(onWsEvent);
  g_server.addHandler(&g_ws);

  g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!snapshotNow(&g_httpSnap)) return sendError(req, 503, "rover busy, retry");
    JsonDocument doc;
    fillStatus(doc.to<JsonObject>(), g_httpSnap);
    sendJson(req, 200, doc);
  });
  g_server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!snapshotNow(&g_httpSnap)) return sendError(req, 503, "rover busy, retry");
    JsonDocument doc;
    fillSensors(doc.to<JsonObject>(), g_httpSnap);
    sendJson(req, 200, doc);
  });
  g_server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ok"] = true;
    fillEvents(doc["events"].to<JsonArray>(), EventLog::kCapacity);
    sendJson(req, 200, doc);
  });
  g_server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["fw"] = FW_VERSION;
    doc["pinmap"] = ROVER_PINMAP_STATUS;
    doc["driver"] = MOTOR_DRIVER_TYPE == DRIVER_BTS7960 ? "BTS7960" : "MD30C";
    doc["teleop_max_duty"] = TELEOP_MAX_DUTY;
    doc["teleop_cmd_timeout_ms"] = TELEOP_CMD_TIMEOUT_MS;
    doc["telemetry_period_ms"] = TELEMETRY_PERIOD_MS;
    doc["rear_clearance_mm"] = REAR_CLEARANCE_MM;
    doc["prox_target_mm"] = PROX_TARGET_MM;
    doc["prox_tol_mm"] = PROX_TOLERANCE_MM;
    doc["motion_goal_ms"] = MOTION_GOAL_MS;
    doc["servo_soft_min"] = SERVO_SOFT_MIN_DEG;
    doc["servo_soft_max"] = SERVO_SOFT_MAX_DEG;
    doc["estop_sense_fitted"] = ESTOP_SENSE_FITTED;
    doc["battery_sense_fitted"] = BATTERY_SENSE_FITTED;
    doc["lane_end_sensor_fitted"] = LANE_END_SENSOR_FITTED;
    doc["cam_stream_port"] = CAM_STREAM_PORT;
    JsonArray missions = doc["missions"].to<JsonArray>();
    for (const char* m : {"traverse", "proximity", "qr_ping", "motion"}) missions.add(m);
    sendJson(req, 200, doc);
  });
  g_server.on("/api/camera/stream", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!snapshotNow(&g_httpSnap)) return sendError(req, 503, "rover busy, retry");
    if (!g_httpSnap.cam.ip[0]) return sendError(req, 503, "camera board has not reported an IP yet");
    char url[48];
    snprintf(url, sizeof(url), "http://%s:%u/stream", g_httpSnap.cam.ip, CAM_STREAM_PORT);
    req->redirect(url);
  });

  // The software E-stop cuts outputs and queues the latch before taking the
  // lock, so it works even if the lock is momentarily unavailable.
  g_server.on(
      "/api/estop", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        g_rover.requestEstop();
        CmdResult r{true, 200, "E-stop engaged (outputs cut; latch pending)"};
        {
          RoverLock lock(kHandlerLockMs);
          if (lock.ok()) r = g_rover.engageEstop(CmdSource::Http);
        }
        JsonDocument doc;
        doc["ok"] = r.ok;
        doc["message"] = r.message;
        doc["state"] = "ESTOP";
        sendJson(req, 200, doc);
      },
      nullptr, collectBody);
  postRoute("/api/estop/reset", cmdEstopReset);
  postRoute("/api/mode", cmdMode);
  postRoute("/api/drive", cmdDrive);
  postRoute("/api/mission/start", cmdStart);
  postRoute("/api/mission/abort", cmdAbort);
  postRoute("/api/fault/ack", cmdAck);
  postRoute("/api/servo", cmdServo);
  postRoute("/api/task/qr_ping", cmdQrPing);
  postRoute("/api/motion_detect", cmdMotionDetect);

  if (filesystemOk) {
    g_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-cache");
  } else {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/html", kNoFsPage); });
  }
  g_server.onNotFound([](AsyncWebServerRequest* req) {
    if (req->method() == HTTP_OPTIONS) {  // CORS preflight for a locally served dashboard
      AsyncWebServerResponse* res = req->beginResponse(204);
      res->addHeader("Access-Control-Allow-Private-Network", "true");
      req->send(res);
      return;
    }
    sendError(req, 404, "not found");
  });
  g_server.begin();
}

void webLoop(uint32_t nowMs) {
  static uint32_t lastCleanup = 0;
  if (nowMs - lastCleanup > 1000) {
    lastCleanup = nowMs;
    g_ws.cleanupClients();
  }
  if (g_ws.count() == 0 || nowMs - g_lastTelemetryMs < TELEMETRY_PERIOD_MS) return;
  g_lastTelemetryMs = nowMs;
  {
    RoverLock lock(20);
    if (!lock.ok()) return;
    g_rover.snapshot(&g_loopSnap);
  }
  static char buf[4096];
  if (!g_ws.availableForWriteAll()) return;  // a slow client: skip this frame
  JsonDocument doc;
  doc["type"] = "telemetry";
  doc["seq"] = ++g_telemetrySeq;
  doc["ev_seq"] = g_events.lastSeq();
  fillStatus(doc["status"].to<JsonObject>(), g_loopSnap);
  fillSensors(doc["sensors"].to<JsonObject>(), g_loopSnap);
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf) - 1) g_ws.textAll(buf, n);

  const uint32_t evSeq = g_events.lastSeq();
  if (evSeq != g_lastEventSeqSent || g_resendEvents) {
    g_resendEvents = false;
    g_lastEventSeqSent = evSeq;
    JsonDocument ev;
    ev["type"] = "events";
    fillEvents(ev["items"].to<JsonArray>(), 20);
    n = serializeJson(ev, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) - 1) g_ws.textAll(buf, n);
  }
}

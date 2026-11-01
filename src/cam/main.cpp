// URRT rover — ESP32-CAM vision board.
//
// MJPEG stream (port 81), QR decode, motion detection and the camera-pan
// servo. Results go to the S3 over the UART link as observations; the S3 picks
// the detectors and the pan mode. This board has no drive-motor wiring and the
// link protocol has no drive command.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#include "bench_page.h"
#include "cam_link.h"
#include "config_cam.h"
#include "pan.h"
#include "vision.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/cam/secrets.h missing: using secrets.example.h (S3 bench AP only). Copy and edit it."
#include "secrets.example.h"
#endif
#ifndef CAM_OTA_PASSWORD
#define CAM_OTA_PASSWORD ""  // older secrets.h: OTA stays off
#endif

constexpr const char* CAM_FW_VERSION = "cam 0.1.0";
static_assert(CAM_W == 320 && CAM_H == 240, "config_cam.h: only QVGA (320x240) is wired up below");

namespace {
CamHealth g_health = {"unknown", "", 0.0f, "", "unknown", false};
bool g_cameraOk = false;
char g_cameraError[48] = "";
httpd_handle_t g_ctrlServer = nullptr;
httpd_handle_t g_streamServer = nullptr;

// ---------------------------------------------------------------- camera

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

bool cameraBegin() {
  if (!psramFound()) {
    strlcpy(g_cameraError, "no PSRAM: RGB565 frames do not fit", sizeof(g_cameraError));
    return false;
  }
  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_D7;
  c.pin_d6 = CAM_PIN_D6;
  c.pin_d5 = CAM_PIN_D5;
  c.pin_d4 = CAM_PIN_D4;
  c.pin_d3 = CAM_PIN_D3;
  c.pin_d2 = CAM_PIN_D2;
  c.pin_d1 = CAM_PIN_D1;
  c.pin_d0 = CAM_PIN_D0;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_pclk = CAM_PIN_PCLK;
  c.xclk_freq_hz = CAM_XCLK_HZ;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = CAM_FB_COUNT;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  const esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    snprintf(g_cameraError, sizeof(g_cameraError), "esp_camera_init: %s", esp_err_to_name(err));
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (!s) {
    strlcpy(g_cameraError, "no sensor handle", sizeof(g_cameraError));
    return false;
  }
  snprintf(g_health.pid, sizeof(g_health.pid), "0x%04X", s->id.PID);
  const camera_sensor_info_t* info = esp_camera_sensor_get_info(&s->id);
  g_health.sensor = info ? info->name : "unknown";
  if (s->set_vflip) s->set_vflip(s, CAM_VFLIP ? 1 : 0);
  if (s->set_hmirror) s->set_hmirror(s, CAM_HMIRROR ? 1 : 0);
  return true;
}

// ---------------------------------------------------------------- HTTP

esp_err_t sendJson(httpd_req_t* req, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, out.c_str(), out.length());
}

esp_err_t statusHandler(httpd_req_t* req) {
  const VisionStats v = visionStats();
  const LinkConfig cfg = linkConfig();
  JsonDocument doc;
  doc["fw"] = CAM_FW_VERSION;
  doc["uptime_ms"] = millis();
  doc["reset_reason"] = g_health.resetReason;
  doc["camera_ok"] = g_cameraOk;
  doc["camera_error"] = g_cameraError;
  doc["sensor"] = g_health.sensor;
  doc["pid"] = g_health.pid;
  doc["frame"] = "320x240 RGB565";
  doc["fps"] = roundf(v.fps * 10.0f) / 10.0f;
  doc["frames"] = v.frames;
  doc["ip"] = g_health.ip;
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  JsonObject l = doc["link"].to<JsonObject>();
  l["source"] = cfgSourceName(cfg.source);  // s3, bench or none
  l["cfg_fresh"] = cfg.fresh;
  l["qr"] = cfg.qr;
  l["motion"] = cfg.motion;
  l["pan_mode"] = boardlink::panModeName(cfg.panMode);
  l["pan_angle"] = cfg.panDeg;
  const PanStatus p = panStatus();
  JsonObject pan = doc["pan"].to<JsonObject>();
  pan["fitted"] = PAN_SERVO_FITTED;
  pan["pin"] = PAN_PIN;
  pan["mode"] = boardlink::panModeName(p.mode);
  pan["state"] = boardlink::panStateName(p.state);
  pan["angle"] = roundf(p.angle * 10.0f) / 10.0f;
  pan["target"] = roundf(p.target * 10.0f) / 10.0f;
  pan["pulse_us"] = static_cast<int>(p.pulseUs);
  pan["tracking"] = p.tracking;
  pan["epoch"] = p.epoch;
  JsonObject q = doc["qr"].to<JsonObject>();
  q["attempts"] = v.qrAttempts;
  q["decoded"] = v.qrDecoded;
  q["last_ms"] = v.lastQrMs;
  // v is a local copy: pass its arrays as const char* so ArduinoJson copies them.
  q["last_text"] = static_cast<const char*>(v.qrText);
  q["last_ip"] = static_cast<const char*>(v.qrIp);
  q["last_err"] = static_cast<const char*>(v.qrErr);
  if (v.qrAtMs) {
    q["age_ms"] = millis() - v.qrAtMs;
  } else {
    q["age_ms"] = nullptr;
  }
  JsonObject m = doc["motion"].to<JsonObject>();
  m["detected"] = v.motionDetected;
  m["cx"] = v.motionCx;
  m["area"] = v.motionArea;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["free_psram"] = ESP.getFreePsram();
  doc["vision_stack_free"] = visionStackFreeBytes();
  return sendJson(req, doc);
}

esp_err_t grayHandler(httpd_req_t* req) {
  uint8_t* jpg = nullptr;
  size_t len = 0;
  if (!g_cameraOk || !debugGrayJpeg(&jpg, &len)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no frame");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg), len);
  free(jpg);
  return res;
}

esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, kBenchPage, HTTPD_RESP_USE_STRLEN);
}

// POST /bench?qr=0|1&mot=0|1&pm=hold|goto|scan|track&ang=deg  (bench page, camera alone)
esp_err_t benchHandler(httpd_req_t* req) {
  char q[96] = "";
  httpd_req_get_url_query_str(req, q, sizeof(q));
  char v[12];
  auto has = [&](const char* key) { return httpd_query_key_value(q, key, v, sizeof(v)) == ESP_OK; };
  LinkConfig c;
  c.qr = has("qr") && v[0] == '1';
  c.motion = has("mot") && v[0] == '1';
  c.panMode = has("pm") ? boardlink::parsePanMode(v) : boardlink::PanMode::Hold;
  c.panDeg = has("ang") ? static_cast<float>(atof(v)) : boardlink::kPanHomeDeg;
  linkSetBench(c);
  JsonDocument doc;
  const bool s3 = linkConfig().source == CfgSource::S3;
  doc["ok"] = true;
  doc["applied"] = !s3;
  doc["note"] = s3 ? "the S3 is connected and in charge; bench settings ignored" : "bench settings applied";
  return sendJson(req, doc);
}

#define PART_BOUNDARY "urrtframe"
esp_err_t streamHandler(httpd_req_t* req) {
  if (!g_cameraOk) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not initialised");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  streamClientDelta(+1);
  uint32_t seq = 0;
  uint32_t lastSentMs = 0;
  uint32_t lastFrameMs = millis();
  esp_err_t res = ESP_OK;
  while (res == ESP_OK) {
    const uint32_t now = millis();
    if (now - lastFrameMs > 5000) {  // vision task stalled: let the browser reconnect
      res = ESP_FAIL;
      break;
    }
    uint8_t* jpg = nullptr;
    size_t len = 0;
    if (now - lastSentMs < 1000 / STREAM_MAX_FPS || !streamEncodeLatest(&seq, &jpg, &len)) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    lastSentMs = lastFrameMs = now;
    char part[96];
    const int n = snprintf(part, sizeof(part),
                           "\r\n--" PART_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                           static_cast<unsigned>(len));
    res = httpd_resp_send_chunk(req, part, n);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(jpg), len);
    free(jpg);
  }
  streamClientDelta(-1);
  return res;
}

void route(httpd_handle_t server, const char* uri, esp_err_t (*handler)(httpd_req_t*), httpd_method_t method = HTTP_GET) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = method;
  u.handler = handler;
  httpd_register_uri_handler(server, &u);
}

void httpBegin() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.core_id = CAM_HTTP_CORE;  // keep JPEG encoding off the vision core
  cfg.server_port = HTTP_PORT;
  cfg.ctrl_port = 32768;
  if (httpd_start(&g_ctrlServer, &cfg) == ESP_OK) {
    route(g_ctrlServer, "/", indexHandler);
    route(g_ctrlServer, "/status", statusHandler);
    route(g_ctrlServer, "/gray.jpg", grayHandler);
    route(g_ctrlServer, "/bench", benchHandler, HTTP_POST);
  }
  // Separate server: a stream handler occupies its server task while a client watches.
  cfg.server_port = STREAM_PORT;
  cfg.ctrl_port = 32769;
  if (httpd_start(&g_streamServer, &cfg) == ESP_OK) route(g_streamServer, "/stream", streamHandler);
}

// ---------------------------------------------------------------- Wi-Fi

struct Network {
  const char* ssid;
  const char* pass;
};
const Network kNetworks[] = {{WIFI_STA_SSID, WIFI_STA_PASSWORD}, {WIFI_FALLBACK_SSID, WIFI_FALLBACK_PASSWORD}};
constexpr int kNetworkCount = sizeof(kNetworks) / sizeof(kNetworks[0]);
int g_netIdx = -1;
uint32_t g_netTryMs = 0;
bool g_wifiUp = false;

void tryNextNetwork(uint32_t nowMs) {
  for (int i = 0; i < kNetworkCount; ++i) {
    g_netIdx = (g_netIdx + 1) % kNetworkCount;
    if (strlen(kNetworks[g_netIdx].ssid) > 0) break;
  }
  WiFi.disconnect();
  WiFi.begin(kNetworks[g_netIdx].ssid, kNetworks[g_netIdx].pass);
  g_netTryMs = nowMs;
  Serial.printf("Wi-Fi: trying \"%s\"\n", kNetworks[g_netIdx].ssid);
}

bool g_otaOn = false;

// Wireless updates (tools/cam_ota.sh). Started once, after the first Wi-Fi
// connection; never without a password.
void otaBegin() {
  if (!CAM_OTA_ENABLED || strlen(CAM_OTA_PASSWORD) < 8) {
    Serial.println("OTA: off (set CAM_OTA_PASSWORD, 8+ characters, in src/cam/secrets.h)");
    return;
  }
  ArduinoOTA.setHostname(CAM_HOSTNAME);
  ArduinoOTA.setPassword(CAM_OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);  // mDNS is managed in wifiPoll()
  ArduinoOTA.onStart([] { Serial.println("OTA: update starting"); });
  ArduinoOTA.onEnd([] { Serial.println("OTA: done, rebooting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA: error %u\n", static_cast<unsigned>(e)); });
  ArduinoOTA.begin();
  g_otaOn = true;
  Serial.printf("OTA: ready on %s port 3232\n", g_health.ip);
}

void wifiPoll(uint32_t nowMs) {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !g_wifiUp) {
    strlcpy(g_health.ip, WiFi.localIP().toString().c_str(), sizeof(g_health.ip));
    Serial.printf("Wi-Fi: connected to \"%s\" ip %s rssi %d\n", WiFi.SSID().c_str(), g_health.ip, WiFi.RSSI());
    Serial.printf("stream: http://%s:%u/stream  status: http://%s/status\n", g_health.ip, STREAM_PORT, g_health.ip);
    MDNS.end();
    if (MDNS.begin(CAM_HOSTNAME)) MDNS.addService("http", "tcp", HTTP_PORT);
    if (!g_otaOn) otaBegin();
  } else if (!up && g_wifiUp) {
    g_health.ip[0] = '\0';
    g_netTryMs = nowMs;
    Serial.println("Wi-Fi: connection lost");
  }
  g_wifiUp = up;
  if (!up && nowMs - g_netTryMs > WIFI_TRY_MS) tryNextNetwork(nowMs);
}
}  // namespace

void setup() {
  pinMode(CAM_PIN_FLASH_LED, OUTPUT);
  digitalWrite(CAM_PIN_FLASH_LED, LOW);
  Serial.begin(115200);
  delay(300);
  g_health.resetReason = resetReasonName();
  Serial.println();
  Serial.println("==================================================");
  Serial.printf(" URRT camera board  %s  (reset: %s)\n", CAM_FW_VERSION, g_health.resetReason);
  Serial.printf(" PSRAM %s (%lu bytes free)\n", psramFound() ? "found" : "NOT FOUND",
                static_cast<unsigned long>(ESP.getFreePsram()));

  g_cameraOk = cameraBegin() && visionBegin();
  g_health.cameraOk = g_cameraOk;
  if (g_cameraOk) {
    Serial.printf(" camera OK: sensor %s, PID %s, 320x240 RGB565\n", g_health.sensor, g_health.pid);
  } else {
    if (!g_cameraError[0]) strlcpy(g_cameraError, "vision buffers could not be allocated", sizeof(g_cameraError));
    Serial.printf(" CAMERA FAILED: %s. Check docs/05_pin_maps.md before changing anything.\n", g_cameraError);
  }
  panBegin();
  if (PAN_SERVO_FITTED) {
    Serial.printf(" pan servo on GPIO%d, goes home %.0f deg, range %.0f..%.0f deg, then holds for the S3\n",
                  PAN_PIN, boardlink::kPanHomeDeg, boardlink::kPanMinDeg, boardlink::kPanMaxDeg);
  } else {
    Serial.println(" pan servo: not fitted (PAN_SERVO_FITTED = false)");
  }
  Serial.println("==================================================");

  linkBegin();
  vTaskPrioritySet(nullptr, CAM_LOOP_TASK_PRIORITY);  // see config_cam.h "Task layout"
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CAM_HOSTNAME);
  WiFi.setSleep(false);
  tryNextNetwork(millis());
  httpBegin();
  if (g_cameraOk) visionStartTask();
}

void loop() {
  const uint32_t now = millis();
  wifiPoll(now);
  g_health.fps = visionStats().fps;
  linkPoll(now, g_health);
  panUpdate(now, linkConfig());
  if (g_otaOn) ArduinoOTA.handle();  // blocks while an update is being received
  delay(5);
}

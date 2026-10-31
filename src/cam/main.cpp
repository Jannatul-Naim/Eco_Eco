// URRT rover — ESP32-CAM vision board.
//
// Observes only: MJPEG stream (port 81), QR decode and motion detection. The
// results go to the S3 over the UART link as observations. This board has no
// motor wiring and the link protocol has no drive command.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#include "cam_link.h"
#include "config_cam.h"
#include "vision.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/cam/secrets.h missing: using secrets.example.h (S3 bench AP only). Copy and edit it."
#include "secrets.example.h"
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
  l["cfg_fresh"] = cfg.fresh;
  l["qr"] = cfg.qr;
  l["motion"] = cfg.motion;
  l["pan"] = cfg.pan == PanInfo::Moving ? "moving" : (cfg.pan == PanInfo::Settled ? "settled" : "absent");
  l["pan_epoch"] = cfg.panEpoch;
  JsonObject q = doc["qr"].to<JsonObject>();
  q["attempts"] = v.qrAttempts;
  q["decoded"] = v.qrDecoded;
  q["last_ms"] = v.lastQrMs;
  JsonObject m = doc["motion"].to<JsonObject>();
  m["detected"] = v.motionDetected;
  m["cx"] = v.motionCx;
  m["area"] = v.motionArea;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["free_psram"] = ESP.getFreePsram();
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
  char html[512];
  snprintf(html, sizeof(html),
           "<!doctype html><meta charset=utf-8><title>URRT cam</title>"
           "<body style='background:#000;color:#fc0;font-family:sans-serif'><h1>URRT camera board</h1>"
           "<p>sensor %s (PID %s) %s</p><p><a style='color:#fc0' href='/status'>/status</a> | "
           "<a style='color:#fc0' href='/gray.jpg'>/gray.jpg</a> (what the QR decoder sees) | "
           "<a style='color:#fc0' href='http://%s:%u/stream'>stream</a></p>",
           g_health.sensor, g_health.pid, g_cameraOk ? "OK" : g_cameraError, g_health.ip, STREAM_PORT);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
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

void route(httpd_handle_t server, const char* uri, esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = HTTP_GET;
  u.handler = handler;
  httpd_register_uri_handler(server, &u);
}

void httpBegin() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = HTTP_PORT;
  cfg.ctrl_port = 32768;
  if (httpd_start(&g_ctrlServer, &cfg) == ESP_OK) {
    route(g_ctrlServer, "/", indexHandler);
    route(g_ctrlServer, "/status", statusHandler);
    route(g_ctrlServer, "/gray.jpg", grayHandler);
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

void wifiPoll(uint32_t nowMs) {
  const bool up = WiFi.status() == WL_CONNECTED;
  if (up && !g_wifiUp) {
    strlcpy(g_health.ip, WiFi.localIP().toString().c_str(), sizeof(g_health.ip));
    Serial.printf("Wi-Fi: connected to \"%s\" ip %s rssi %d\n", WiFi.SSID().c_str(), g_health.ip, WiFi.RSSI());
    Serial.printf("stream: http://%s:%u/stream  status: http://%s/status\n", g_health.ip, STREAM_PORT, g_health.ip);
    MDNS.end();
    if (MDNS.begin(CAM_HOSTNAME)) MDNS.addService("http", "tcp", HTTP_PORT);
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
  Serial.println("==================================================");

  linkBegin();
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
  delay(5);
}

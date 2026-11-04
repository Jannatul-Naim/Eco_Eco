// URRT rover — OPTIONAL audio bridge (ESP32 DevKit, INMP441 + MAX98357A).
//
// Two-way audio for the teleoperated audio task:
//   mic     INMP441 -> DC blocker -> 16 kHz PCM -> WebSocket binary frames -> dashboard speaker
//   speaker dashboard microphone -> WebSocket binary frames -> gain limiter -> MAX98357A
// The S3 only receives a UDP status heartbeat from this board. There is no
// message path from here to the motors, and the S3 accepts no commands from it.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <AsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <driver/i2s.h>
#include <math.h>

#include "config_audio.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/audio/secrets.h missing: using secrets.example.h (S3 bench AP only). Copy and edit it."
#include "secrets.example.h"
#endif

constexpr const char* AUDIO_FW_VERSION = "audio 0.2.0";
// Flip if the mic level stays at 0 with the INMP441 L/R pin tied to GND
// (some ESP-IDF releases swap the slot that ONLY_LEFT reads).
constexpr bool MIC_READ_RIGHT_SLOT = false;

namespace {
AsyncWebServer g_server(AUDIO_HTTP_PORT);
AsyncWebSocket g_ws("/audio");
AsyncUDP g_udp;
bool g_micOk = false;
bool g_spkOk = false;

volatile int g_micLevel = 0;  // 0..100
volatile float g_speakerGain = SPEAKER_GAIN;
uint32_t g_listeners[AUDIO_MAX_CLIENTS] = {};
portMUX_TYPE g_listenMux = portMUX_INITIALIZER_UNLOCKED;

int16_t g_ring[SPEAKER_RING_SAMPLES];
size_t g_ringHead = 0, g_ringTail = 0;  // head: write, tail: read
portMUX_TYPE g_ringMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t g_lastSpeechMs = 0;
volatile bool g_speakerOn = false;

// ---------------------------------------------------------------- I2S

bool micBegin() {
  i2s_config_t c = {};
  c.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  c.sample_rate = AUDIO_SAMPLE_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  c.channel_format = MIC_READ_RIGHT_SLOT ? I2S_CHANNEL_FMT_ONLY_RIGHT : I2S_CHANNEL_FMT_ONLY_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 4;
  c.dma_buf_len = MIC_CHUNK_SAMPLES;
  if (i2s_driver_install(I2S_NUM_0, &c, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = MIC_PIN_BCLK;
  p.ws_io_num = MIC_PIN_WS;
  p.data_out_num = I2S_PIN_NO_CHANGE;
  p.data_in_num = MIC_PIN_SD;
  return i2s_set_pin(I2S_NUM_0, &p) == ESP_OK;
}

bool speakerBegin() {
  pinMode(SPK_PIN_SD, OUTPUT);
  digitalWrite(SPK_PIN_SD, LOW);
  i2s_config_t c = {};
  c.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate = AUDIO_SAMPLE_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;  // same sample on both slots: works for any SD-pin mix setting
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 6;
  c.dma_buf_len = 256;
  c.tx_desc_auto_clear = true;  // underrun plays silence, not the last buffer
  if (i2s_driver_install(I2S_NUM_1, &c, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = SPK_PIN_BCLK;
  p.ws_io_num = SPK_PIN_LRC;
  p.data_out_num = SPK_PIN_DIN;
  p.data_in_num = I2S_PIN_NO_CHANGE;
  return i2s_set_pin(I2S_NUM_1, &p) == ESP_OK;
}

int levelFromRms(float rms) {
  if (rms < 1.0f) return 0;
  const float db = 20.0f * log10f(rms / 32768.0f);  // -90..0 dBFS
  const int lvl = static_cast<int>((db + 60.0f) * 100.0f / 60.0f);
  return lvl < 0 ? 0 : (lvl > 100 ? 100 : lvl);
}

void micTask(void*) {
  static int32_t raw[MIC_CHUNK_SAMPLES];
  static int16_t pcm[MIC_CHUNK_SAMPLES];
  // DC blocker state: y[n] = x[n] - x[n-1] + R * y[n-1] (~12 Hz cutoff at 16 kHz)
  float xPrev = 0.0f;
  float yPrev = 0.0f;
  constexpr float R = 0.995f;

  for (;;) {
    size_t got = 0;
    if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, pdMS_TO_TICKS(100)) != ESP_OK || got == 0) continue;
    const size_t n = got / sizeof(int32_t);
    double sumSq = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const float x = static_cast<float>(raw[i] >> MIC_SHIFT);
      const float y = x - xPrev + R * yPrev;
      xPrev = x;
      yPrev = y;

      int32_t s = static_cast<int32_t>(y);
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      pcm[i] = static_cast<int16_t>(s);
      sumSq += static_cast<double>(s) * s;
    }
    g_micLevel = levelFromRms(sqrtf(static_cast<float>(sumSq / n)));
    uint32_t ids[AUDIO_MAX_CLIENTS];
    portENTER_CRITICAL(&g_listenMux);
    memcpy(ids, g_listeners, sizeof(ids));
    portEXIT_CRITICAL(&g_listenMux);
    for (uint32_t id : ids) {
      if (id == 0) continue;
      AsyncWebSocketClient* c = g_ws.client(id);
      if (c && c->canSend()) c->binary(reinterpret_cast<const uint8_t*>(pcm), n * sizeof(int16_t));
    }
  }
}

size_t ringPop(int16_t* out, size_t max) {
  portENTER_CRITICAL(&g_ringMux);
  size_t n = 0;
  while (n < max && g_ringTail != g_ringHead) {
    out[n++] = g_ring[g_ringTail];
    g_ringTail = (g_ringTail + 1) % SPEAKER_RING_SAMPLES;
  }
  portEXIT_CRITICAL(&g_ringMux);
  return n;
}

void ringPush(const uint8_t* data, size_t len) {
  const size_t n = len / 2;
  const float gain = g_speakerGain;
  portENTER_CRITICAL(&g_ringMux);
  for (size_t i = 0; i < n; ++i) {
    const int16_t s = static_cast<int16_t>(data[2 * i] | (data[2 * i + 1] << 8));
    const size_t next = (g_ringHead + 1) % SPEAKER_RING_SAMPLES;
    if (next == g_ringTail) break;  // full: drop the rest rather than add delay
    float scaled = static_cast<float>(s) * gain;
    if (scaled > 32767.0f) scaled = 32767.0f;
    if (scaled < -32768.0f) scaled = -32768.0f;
    g_ring[g_ringHead] = static_cast<int16_t>(scaled);
    g_ringHead = next;
  }
  portEXIT_CRITICAL(&g_ringMux);
  g_lastSpeechMs = millis();
}

void speakerTask(void*) {
  static int16_t mono[256];
  static int16_t stereo[512];
  for (;;) {
    const size_t n = ringPop(mono, 256);
    if (n == 0) {
      if (g_speakerOn && millis() - g_lastSpeechMs > SPEAKER_IDLE_OFF_MS) {
        digitalWrite(SPK_PIN_SD, LOW);
        g_speakerOn = false;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (!g_speakerOn) {
      digitalWrite(SPK_PIN_SD, HIGH);
      g_speakerOn = true;
    }
    for (size_t i = 0; i < n; ++i) stereo[2 * i] = stereo[2 * i + 1] = mono[i];
    size_t written = 0;
    i2s_write(I2S_NUM_1, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }
}

// ---------------------------------------------------------------- WebSocket

void setListener(uint32_t id, bool on) {
  portENTER_CRITICAL(&g_listenMux);
  for (uint32_t& slot : g_listeners) {
    if (slot == id) slot = 0;
  }
  if (on) {
    for (uint32_t& slot : g_listeners) {
      if (slot == 0) {
        slot = id;
        break;
      }
    }
  }
  portEXIT_CRITICAL(&g_listenMux);
}

int clientCount() { return static_cast<int>(g_ws.count()); }

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      if (g_ws.count() > AUDIO_MAX_CLIENTS) {
        client->close(1013, "too many audio clients");
        return;
      }
      Serial.printf("audio client #%lu from %s\n", static_cast<unsigned long>(client->id()),
                    client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      setListener(client->id(), false);
      break;
    case WS_EVT_DATA: {
      const AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
      if (info->opcode == WS_BINARY) {
        if (g_spkOk) ringPush(data, len);
      } else if (info->opcode == WS_TEXT && info->final && info->index == 0 && len < 64) {
        JsonDocument d;
        if (!deserializeJson(d, data, len)) {
          if (d["listen"].is<bool>()) setListener(client->id(), d["listen"].as<bool>());
          if (d["vol"].is<int>()) {
            const int v = d["vol"].as<int>();
            if (v >= 0 && v <= 100) g_speakerGain = v / 100.0f;
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

void sendHeartbeat() {
  char msg[96];
  const int n = snprintf(msg, sizeof(msg), "{\"v\":1,\"t\":\"audio_hb\",\"lvl\":%d,\"cl\":%d,\"spk\":%d}", g_micLevel,
                         clientCount(), g_speakerOn ? 1 : 0);
  // Broadcast: works on the arena network and on the S3 bench AP without knowing the S3's IP.
  g_udp.broadcastTo(reinterpret_cast<uint8_t*>(msg), n, AUDIO_HB_UDP_PORT);
}

void httpBegin() {
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  g_ws.onEvent(onWsEvent);
  g_server.addHandler(&g_ws);
  g_server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["fw"] = AUDIO_FW_VERSION;
    doc["uptime_ms"] = millis();
    doc["mic_ok"] = g_micOk;
    doc["speaker_ok"] = g_spkOk;
    doc["mic_level"] = g_micLevel;
    doc["speaker_gain"] = static_cast<int>(g_speakerGain * 100);
    doc["clients"] = clientCount();
    doc["speaker_active"] = g_speakerOn;
    doc["sample_rate"] = AUDIO_SAMPLE_RATE;
    doc["format"] = "s16le mono";
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });
  g_server.on("/volume", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (req->hasParam("val")) {
      const int v = req->getParam("val")->value().toInt();
      if (v >= 0 && v <= 100) {
        g_speakerGain = v / 100.0f;
        req->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
    req->send(400, "application/json", "{\"error\":\"val must be 0..100\"}");
  });
  g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html",
              "<!doctype html><meta charset=utf-8><title>URRT audio</title><body style='background:#000;color:#fc0;"
              "font-family:sans-serif'><h1>URRT audio bridge</h1><p>Use the rover dashboard (Audio panel). "
              "<a style='color:#fc0' href='/status'>/status</a></p>");
  });
  g_server.begin();
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
    Serial.printf("Wi-Fi: connected to \"%s\" ip %s; audio socket ws://%s/audio\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(AUDIO_HOSTNAME)) MDNS.addService("http", "tcp", AUDIO_HTTP_PORT);
  } else if (!up && g_wifiUp) {
    g_netTryMs = nowMs;
    Serial.println("Wi-Fi: connection lost");
  }
  g_wifiUp = up;
  if (!up && nowMs - g_netTryMs > WIFI_TRY_MS) tryNextNetwork(nowMs);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("==================================================");
  Serial.printf(" URRT audio bridge  %s  (OPTIONAL, no motor authority)\n", AUDIO_FW_VERSION);
  g_micOk = micBegin();
  g_spkOk = speakerBegin();
  // Driver start only: I2S cannot tell whether an INMP441 or MAX98357A is wired.
  // A missing mic shows as "mic level 0".
  Serial.printf(" I2S drivers: mic %s | speaker %s | %lu Hz s16le mono\n", g_micOk ? "started" : "FAILED",
                g_spkOk ? "started" : "FAILED", static_cast<unsigned long>(AUDIO_SAMPLE_RATE));
  Serial.println("==================================================");

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(AUDIO_HOSTNAME);
  WiFi.setSleep(false);
  tryNextNetwork(millis());
  httpBegin();
  if (g_micOk) xTaskCreatePinnedToCore(micTask, "mic", 6144, nullptr, 3, nullptr, 1);
  if (g_spkOk) xTaskCreatePinnedToCore(speakerTask, "speaker", 4096, nullptr, 3, nullptr, 1);
}

void loop() {
  static uint32_t lastHb = 0, lastCleanup = 0, lastPrint = 0;
  const uint32_t now = millis();
  wifiPoll(now);
  if (g_wifiUp && now - lastHb >= AUDIO_HB_PERIOD_MS) {
    lastHb = now;
    sendHeartbeat();
  }
  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    g_ws.cleanupClients(AUDIO_MAX_CLIENTS);
  }
  if (now - lastPrint >= 5000) {
    lastPrint = now;
    Serial.printf("mic level %d | clients %d | speaker %s\n", g_micLevel, clientCount(), g_speakerOn ? "on" : "off");
  }
  delay(10);
}

#include "net.h"

#include <ArduinoJson.h>
#include <AsyncUDP.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config_network.h"
#include "events.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/s3/secrets.h missing: using secrets.example.h (bench AP only). Copy and edit it."
#include "secrets.example.h"
#endif

namespace {
AsyncUDP g_udp;
AudioStatus g_audio = {};
portMUX_TYPE g_audioMux = portMUX_INITIALIZER_UNLOCKED;
bool g_apMode = false;

void onAudioPacket(AsyncUDPPacket& packet) {
  if (packet.length() > 200) return;
  JsonDocument doc;
  if (deserializeJson(doc, packet.data(), packet.length())) return;
  if ((doc["v"] | -1) != 1 || strcmp(doc["t"] | "", "audio_hb") != 0) return;
  // Status only. The audio board has no command path into the rover.
  AudioStatus a = {};
  a.online = true;
  strlcpy(a.ip, packet.remoteIP().toString().c_str(), sizeof(a.ip));
  a.lastMs = millis();
  a.micLevel = doc["lvl"] | 0;
  a.clients = doc["cl"] | 0;
  a.speakerActive = (doc["spk"] | 0) != 0;
  portENTER_CRITICAL(&g_audioMux);
  g_audio = a;
  portEXIT_CRITICAL(&g_audioMux);
}
}  // namespace

void netBegin() {
  WiFi.persistent(false);  // never write credentials to flash at runtime
  WiFi.setHostname(ROVER_HOSTNAME);
  bool connected = false;
  if (strlen(WIFI_STA_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    Serial.printf("Wi-Fi: joining \"%s\"", WIFI_STA_SSID);
    const uint32_t t0 = millis();
    while (millis() - t0 < WIFI_STA_CONNECT_TIMEOUT_MS) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }
  if (connected) {
    WiFi.setSleep(false);  // lower, steadier latency for telemetry
    g_events.add(EvLevel::Info, "Wi-Fi STA %s ip %s rssi %d", WIFI_STA_SSID, WiFi.localIP().toString().c_str(),
                 WiFi.RSSI());
  } else if (WIFI_SOFTAP_FALLBACK) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_SOFTAP_CHANNEL);
    g_apMode = true;
    g_events.add(EvLevel::Warn, "Wi-Fi STA failed; bench AP \"%s\" at %s", WIFI_AP_SSID,
                 WiFi.softAPIP().toString().c_str());
  } else {
    g_events.add(EvLevel::Error, "Wi-Fi STA failed and AP fallback disabled; retrying in background");
  }
  if (MDNS.begin(ROVER_HOSTNAME)) MDNS.addService("http", "tcp", HTTP_PORT);
  if (g_udp.listen(AUDIO_HB_UDP_PORT)) g_udp.onPacket(onAudioPacket);
}

NetInfo netInfo() {
  NetInfo n = {};
  if (g_apMode) {
    n.mode = "ap";
    strlcpy(n.ssid, WIFI_AP_SSID, sizeof(n.ssid));
    strlcpy(n.ip, WiFi.softAPIP().toString().c_str(), sizeof(n.ip));
    n.rssi = 0;
  } else if (WiFi.status() == WL_CONNECTED) {
    n.mode = "sta";
    strlcpy(n.ssid, WiFi.SSID().c_str(), sizeof(n.ssid));
    strlcpy(n.ip, WiFi.localIP().toString().c_str(), sizeof(n.ip));
    n.rssi = WiFi.RSSI();
  } else {
    n.mode = "down";
  }
  return n;
}

AudioStatus audioStatus(uint32_t nowMs) {
  portENTER_CRITICAL(&g_audioMux);
  AudioStatus a = g_audio;
  portEXIT_CRITICAL(&g_audioMux);
  a.online = a.lastMs != 0 && nowMs - a.lastMs <= AUDIO_HB_STALE_MS;
  return a;
}

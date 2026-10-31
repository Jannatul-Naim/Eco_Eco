// ESP32-S3 main controller — network behaviour. Credentials live in secrets.h.
#pragma once

#include <stdint.h>

// The S3 first tries to join the arena / lab network (station mode).
constexpr uint32_t WIFI_STA_CONNECT_TIMEOUT_MS = 15000;
// If that fails, it can open its own access point for BENCH work.
// The rulebook only allows organiser-assigned wireless equipment in the arena:
// set this to false for competition builds.
constexpr bool WIFI_SOFTAP_FALLBACK = true;
constexpr uint8_t WIFI_SOFTAP_CHANNEL = 6;
constexpr const char* ROVER_HOSTNAME = "urrt-rover";

constexpr uint16_t HTTP_PORT = 80;
// Camera board serves its MJPEG stream here (separate httpd instance).
constexpr uint16_t CAM_STREAM_PORT = 81;
// Optional audio board broadcasts a status heartbeat to this UDP port.
constexpr uint16_t AUDIO_HB_UDP_PORT = 4211;
constexpr uint32_t AUDIO_HB_STALE_MS = 3000;

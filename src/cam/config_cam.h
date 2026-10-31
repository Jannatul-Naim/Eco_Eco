// ESP32-CAM vision board — pins, camera and vision settings.
//
// *** PROVISIONAL *** Assumes the AI-Thinker ESP32-CAM layout. The camera
// module is marked "RHYX M21-45"; its sensor is NOT confirmed (GC2145-family
// is only a hypothesis). The firmware reads the sensor ID at boot and reports
// it on serial, in /status and in every heartbeat to the S3. If the reported
// sensor or the camera init fails, STOP and check docs/05_pin_maps.md.
//
// This board only observes: video stream, QR decode, motion detection.
// It has no motor wiring and its protocol has no drive command.
#pragma once

#include <stdint.h>

// ---- AI-Thinker camera connector
constexpr int CAM_PIN_PWDN = 32;
constexpr int CAM_PIN_RESET = -1;
constexpr int CAM_PIN_XCLK = 0;
constexpr int CAM_PIN_SIOD = 26;
constexpr int CAM_PIN_SIOC = 27;
constexpr int CAM_PIN_D7 = 35;
constexpr int CAM_PIN_D6 = 34;
constexpr int CAM_PIN_D5 = 39;
constexpr int CAM_PIN_D4 = 36;
constexpr int CAM_PIN_D3 = 21;
constexpr int CAM_PIN_D2 = 19;
constexpr int CAM_PIN_D1 = 18;
constexpr int CAM_PIN_D0 = 5;
constexpr int CAM_PIN_VSYNC = 25;
constexpr int CAM_PIN_HREF = 23;
constexpr int CAM_PIN_PCLK = 22;
constexpr int CAM_PIN_FLASH_LED = 4;  // kept OFF

// ---- UART link to the S3 (leave the microSD slot EMPTY: it shares these pins)
// GPIO12 is avoided on purpose: it is a strapping pin (flash voltage).
constexpr int LINK_PIN_RX = 13;  // <- S3 GPIO41 (S3 TX)
constexpr int LINK_PIN_TX = 14;  // -> S3 GPIO42 (S3 RX)
constexpr uint32_t LINK_HB_PERIOD_MS = 200;
constexpr uint32_t LINK_CFG_STALE_MS = 1500;  // no S3 config -> detectors off

// ---- Capture. RGB565 works on both OV2640 and GC2145-family sensors; GC2145
// has no hardware JPEG, so the stream is JPEG-encoded in software.
constexpr uint32_t CAM_XCLK_HZ = 20000000;
constexpr uint16_t CAM_W = 320;  // FRAMESIZE_QVGA
constexpr uint16_t CAM_H = 240;
constexpr uint8_t CAM_FB_COUNT = 2;
constexpr bool CAM_VFLIP = false;
constexpr bool CAM_HMIRROR = false;
// esp32-camera delivers RGB565 high byte first. Flip only if /gray.jpg looks scrambled.
constexpr bool CAM_RGB565_HIGH_BYTE_FIRST = true;

// ---- MJPEG stream (port 81) and control/status server (port 80)
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t STREAM_PORT = 81;
constexpr uint8_t STREAM_JPEG_QUALITY = 60;  // 1..100, higher = better/larger
constexpr uint32_t STREAM_MAX_FPS = 12;

// ---- QR
constexpr bool QR_ALLOW_EMBEDDED_IPV4 = true;  // accept "Server: 10.0.0.5" as well as "10.0.0.5"
constexpr uint32_t QR_RESEND_SAME_MS = 1000;   // repeat an identical result at most this often
constexpr size_t QR_TEXT_MAX = 48;              // characters forwarded to the S3

// ---- Motion (frame differencing on an 80x60 luma image)
constexpr uint16_t MOTION_W = 80;
constexpr uint16_t MOTION_H = 60;
constexpr uint8_t MOTION_PIXEL_THRESHOLD = 22;  // luma difference counted as change
constexpr uint16_t MOTION_MIN_PIXELS = 8;       // fewer changed cells = no target
constexpr float MOTION_GLOBAL_FRAC = 0.35f;     // more = exposure jump / camera moved
constexpr uint8_t MOTION_SETTLE_FRAMES = 2;     // frames skipped after the pan settles

// ---- Wi-Fi
constexpr uint32_t WIFI_TRY_MS = 10000;
constexpr const char* CAM_HOSTNAME = "urrt-cam";

// ESP32-CAM vision board — pins, camera and vision settings.
//
// *** PROVISIONAL *** Assumes the AI-Thinker ESP32-CAM layout. The camera
// module is marked "RHYX M21-45"; its sensor is NOT confirmed (GC2145-family
// is only a hypothesis). The firmware reads the sensor ID at boot and reports
// it on serial, in /status and in every heartbeat to the S3. If the reported
// sensor or the camera init fails, STOP and check docs/05_pin_maps.md.
//
// This board runs the video stream, QR decode, motion detection and the
// camera-pan servo. It has no drive-motor wiring and its protocol has no
// drive command.
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
// Bench page (camera alone, no S3): its settings lapse this long after the page
// stops refreshing them, and are ignored whenever the S3 is talking.
constexpr uint32_t BENCH_KEEPALIVE_MS = 3000;

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

// ---- Task layout. loop() sends the link heartbeats, reads the S3's config and
// steps the pan servo; at the default priority 1 it starved behind the vision
// task (4) and the stream's software JPEG encoding, heartbeats came up to 1.3 s
// late and the S3 saw the link flap. loop() sleeps 5 ms per pass, so a high
// priority costs little. The HTTP/stream servers run on core 0, vision on core 1.
constexpr unsigned CAM_LOOP_TASK_PRIORITY = 6;
constexpr int CAM_HTTP_CORE = 0;

// ---- MJPEG stream (port 81) and control/status server (port 80)
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t STREAM_PORT = 81;
constexpr uint8_t STREAM_JPEG_QUALITY = 60;  // 1..100, higher = better/larger
constexpr uint32_t STREAM_MAX_FPS = 12;

// ---- QR
// Vision task stack. quirc_decode() alone puts ~8.9 KB on the stack; check
// "vision_stack_free" in /status after a real decode before lowering this.
constexpr uint32_t VISION_TASK_STACK_BYTES = 24576;
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

// ---- Camera-pan servo (MG996R, 180° positional type). Signal on GPIO2, the only free pin that neither
// stops the boot (GPIO12) nor pulses at power-up (GPIO15). GPIO2 is a strapping
// pin: nothing may pull it HIGH at reset. Power the servo from its own 5 V
// supply (>= 3 A: an MG996R stalls at ~2.5 A) with a common GND, never from
// this board's 5V pin. PROVISIONAL until
// docs/06 stage 6 confirms no twitch at boot. Angle limits: boardlink::kPan*.
constexpr bool PAN_SERVO_FITTED = true;
constexpr int PAN_PIN = 2;
constexpr uint8_t PAN_LEDC_CHANNEL = 4;  // LEDC timer 2; timer 0 drives the camera XCLK
constexpr uint32_t PAN_PWM_HZ = 50;
constexpr uint8_t PAN_PWM_BITS = 16;
constexpr float SERVO_US_AT_0_DEG = 500.0f;     // nominal; VERIFY in stage 6
constexpr float SERVO_US_AT_180_DEG = 2400.0f;  // nominal; VERIFY in stage 6
constexpr float SERVO_MAX_DEG_PER_S = 120.0f;
constexpr uint32_t SERVO_SETTLE_MS = 350;       // no position feedback: time-based settle
// Scan positions (QR search, and motion search while no target is seen).
constexpr float PAN_SCAN_DEG[] = {90.0f, 65.0f, 45.0f, 65.0f, 90.0f, 115.0f, 135.0f, 115.0f};
constexpr uint32_t PAN_SCAN_DWELL_MS = 1500;    // time at each position after settling

// ---- Motion tracking (pan mode "track")
constexpr float MOTION_RECENTER_CX = 0.45f;     // |cx| beyond this -> nudge the pan toward the target
constexpr float MOTION_RECENTER_STEP_DEG = 10.0f;
// +1: increasing servo angle turns the camera toward image-right. VERIFY in stage 7.
constexpr float MOTION_PAN_DIRECTION = 1.0f;
// No detection for this long -> target lost, resume scanning. Must exceed one
// recentring move: slew + SERVO_SETTLE_MS + MOTION_SETTLE_FRAMES frames.
constexpr uint32_t PAN_TRACK_LOST_MS = 1000;

// ---- Wi-Fi
// Wireless firmware updates over ArduinoOTA (port 3232), only with a password
// set in secrets.h. Turn off for competition if the organisers ask.
constexpr bool CAM_OTA_ENABLED = true;
constexpr uint32_t WIFI_TRY_MS = 10000;
constexpr const char* CAM_HOSTNAME = "urrt-cam";

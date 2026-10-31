// OPTIONAL ESP32 DevKit audio bridge — pins and audio settings.
//
// *** PROVISIONAL *** Assumes a 38-pin ESP32-DevKitC (ESP32-WROOM-32). The
// INMP441 microphone and MAX98357A amplifier are provisional parts; label the
// audio task optional until the team confirms it (docs/03_electrical.md).
//
// This board relays audio only. It has no motor wiring and no message path
// to the S3 other than a status heartbeat the S3 displays.
#pragma once

#include <stdint.h>

// ---- INMP441 I2S microphone (I2S_NUM_0, receive). L/R pin to GND = left channel.
constexpr int MIC_PIN_BCLK = 26;  // INMP441 SCK
constexpr int MIC_PIN_WS = 25;    // INMP441 WS
constexpr int MIC_PIN_SD = 34;    // INMP441 SD (input-only GPIO is fine for data in)

// ---- MAX98357A I2S amplifier (I2S_NUM_1, transmit)
constexpr int SPK_PIN_BCLK = 27;  // MAX98357A BCLK
constexpr int SPK_PIN_LRC = 32;   // MAX98357A LRC
constexpr int SPK_PIN_DIN = 33;   // MAX98357A DIN
constexpr int SPK_PIN_SD = 23;    // MAX98357A SD: HIGH = on, LOW = shutdown (silences hiss when idle)

// ---- Audio format on the WebSocket: 16-bit signed little-endian PCM, mono.
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
constexpr size_t MIC_CHUNK_SAMPLES = 320;     // 20 ms per WebSocket frame
constexpr int MIC_SHIFT = 14;                 // 32-bit I2S slot -> 16-bit (INMP441 data is 24-bit, MSB first)
constexpr float SPEAKER_GAIN = 0.8f;          // 0..1 applied to received PCM
constexpr size_t SPEAKER_RING_SAMPLES = 16000;  // 1 s of buffered speech
constexpr uint32_t SPEAKER_IDLE_OFF_MS = 400;   // amp shut down after this much silence

// ---- Network
constexpr uint16_t AUDIO_HTTP_PORT = 80;      // WebSocket at ws://<ip>/audio
constexpr uint16_t AUDIO_HB_UDP_PORT = 4211;  // must match src/s3/config_network.h
constexpr uint32_t AUDIO_HB_PERIOD_MS = 500;
constexpr uint32_t WIFI_TRY_MS = 10000;
constexpr const char* AUDIO_HOSTNAME = "urrt-audio";
constexpr uint8_t AUDIO_MAX_CLIENTS = 2;

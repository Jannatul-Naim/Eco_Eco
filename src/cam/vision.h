// Capture loop, QR decoding (quirc) and frame-difference motion detection.
// Colour and shape detection are intentionally NOT part of this project.
#pragma once

#include <Arduino.h>
#include <esp_camera.h>

struct VisionStats {
  float fps;
  uint32_t frames;
  uint32_t qrAttempts;
  uint32_t qrDecoded;
  uint32_t lastQrMs;       // last decode time in ms (processing cost)
  bool motionDetected;
  float motionCx;
  float motionArea;
};

bool visionBegin();          // allocates buffers; call after esp_camera_init
void visionStartTask();
VisionStats visionStats();

// Stream support: the vision task publishes RGB565 frames into a shared
// buffer only while a stream client is connected.
void streamClientDelta(int delta);
// Encodes the latest published frame as JPEG if newer than *seq.
// Returns false if there is no new frame. Caller must free(*jpg).
bool streamEncodeLatest(uint32_t* seq, uint8_t** jpg, size_t* len);
// Debug: grayscale image the QR decoder sees, as JPEG. Caller must free(*jpg).
bool debugGrayJpeg(uint8_t** jpg, size_t* len);

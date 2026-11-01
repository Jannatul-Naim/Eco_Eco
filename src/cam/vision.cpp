#include "vision.h"

#include <img_converters.h>

#include "cam_link.h"
#include "config_cam.h"
#include "ipv4.h"
#include "pan.h"
#include "quirc.h"

namespace {
constexpr size_t kPixels = static_cast<size_t>(CAM_W) * CAM_H;
constexpr size_t kMotionCells = static_cast<size_t>(MOTION_W) * MOTION_H;
constexpr int kBlockX = CAM_W / MOTION_W;
constexpr int kBlockY = CAM_H / MOTION_H;
static_assert(CAM_W % MOTION_W == 0 && CAM_H % MOTION_H == 0, "motion grid must divide the frame");

uint8_t* g_gray = nullptr;        // full-resolution luma, written by the vision task only
SemaphoreHandle_t g_grayLock = nullptr;
uint8_t* g_streamBuf = nullptr;   // latest RGB565 frame for the MJPEG stream
SemaphoreHandle_t g_streamLock = nullptr;
volatile uint32_t g_streamSeq = 0;
volatile int g_streamClients = 0;

struct quirc* g_qr = nullptr;
struct quirc_code g_code;          // ~4 KB, kept off the task stack
struct quirc_data g_data;          // ~9 KB

VisionStats g_stats = {};
portMUX_TYPE g_statsMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_visionTask = nullptr;

// Motion state (vision task only)
uint8_t g_motCur[kMotionCells];
uint8_t g_motPrev[kMotionCells];
bool g_motHavePrev = false;
uint32_t g_motEpoch = 0;
uint8_t g_motSettleLeft = 0;

// QR de-duplication
char g_qrLastText[QR_TEXT_MAX + 1] = "";
uint32_t g_qrLastSentMs = 0;

inline uint8_t luma(const uint8_t* p) {
  const uint16_t px = CAM_RGB565_HIGH_BYTE_FIRST ? static_cast<uint16_t>((p[0] << 8) | p[1])
                                                 : static_cast<uint16_t>((p[1] << 8) | p[0]);
  const uint32_t r = (px >> 11) << 3;
  const uint32_t g = ((px >> 5) & 0x3F) << 2;
  const uint32_t b = (px & 0x1F) << 3;
  return static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
}

void toGray(const uint8_t* rgb565) {
  for (size_t i = 0; i < kPixels; ++i) g_gray[i] = luma(rgb565 + 2 * i);
}

void downsample() {
  for (int y = 0; y < MOTION_H; ++y) {
    for (int x = 0; x < MOTION_W; ++x) {
      uint32_t sum = 0;
      for (int dy = 0; dy < kBlockY; ++dy) {
        const uint8_t* row = g_gray + (y * kBlockY + dy) * CAM_W + x * kBlockX;
        for (int dx = 0; dx < kBlockX; ++dx) sum += row[dx];
      }
      g_motCur[y * MOTION_W + x] = static_cast<uint8_t>(sum / (kBlockX * kBlockY));
    }
  }
}

void resetMotion(uint8_t settleFrames) {
  g_motHavePrev = false;
  g_motSettleLeft = settleFrames;
}

void runMotion(uint32_t nowMs) {
  // While the camera turns every pixel changes: drop the baseline and wait
  // until the pan has settled (new epoch) plus a few frames.
  const PanStatus pan = panStatus();
  if (pan.state == boardlink::PanState::Moving) {
    resetMotion(MOTION_SETTLE_FRAMES);
    return;
  }
  if (g_motHavePrev && pan.epoch != g_motEpoch) resetMotion(MOTION_SETTLE_FRAMES);
  if (g_motSettleLeft > 0) {
    --g_motSettleLeft;
    return;
  }
  downsample();
  if (!g_motHavePrev) {
    memcpy(g_motPrev, g_motCur, kMotionCells);
    g_motHavePrev = true;
    g_motEpoch = pan.epoch;
    linkSendMotion(false, 0.0f, 0.0f, 0.0f, g_motEpoch, true);
    return;
  }
  uint32_t changed = 0, sumX = 0, sumY = 0;
  for (int y = 0; y < MOTION_H; ++y) {
    for (int x = 0; x < MOTION_W; ++x) {
      const int i = y * MOTION_W + x;
      if (abs(static_cast<int>(g_motCur[i]) - static_cast<int>(g_motPrev[i])) > MOTION_PIXEL_THRESHOLD) {
        ++changed;
        sumX += x;
        sumY += y;
      }
    }
  }
  memcpy(g_motPrev, g_motCur, kMotionCells);
  const float area = static_cast<float>(changed) / kMotionCells;
  bool detected = changed >= MOTION_MIN_PIXELS;
  bool baseline = false;
  if (area > MOTION_GLOBAL_FRAC) {  // exposure jump or the whole camera moved: not a target
    detected = false;
    baseline = true;
  }
  float cx = 0.0f, cy = 0.0f;
  if (detected) {
    cx = ((static_cast<float>(sumX) / changed + 0.5f) / MOTION_W) * 2.0f - 1.0f;
    cy = ((static_cast<float>(sumY) / changed + 0.5f) / MOTION_H) * 2.0f - 1.0f;
  }
  linkSendMotion(detected, cx, cy, area, g_motEpoch, baseline);
  panMotionSeen(detected, cx, nowMs);
  portENTER_CRITICAL(&g_statsMux);
  g_stats.motionDetected = detected;
  g_stats.motionCx = cx;
  g_stats.motionArea = area;
  portEXIT_CRITICAL(&g_statsMux);
}

// Copies the payload as printable ASCII. Quotes and backslashes become '?'
// so the JSON on the UART link never grows through escaping.
void payloadText(const quirc_data& d, char* out, size_t cap) {
  size_t n = 0;
  for (int i = 0; i < d.payload_len && n + 1 < cap; ++i) {
    const char c = static_cast<char>(d.payload[i]);
    out[n++] = (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') ? c : '?';
  }
  out[n] = '\0';
}

void runQr(uint32_t nowMs) {
  int w = 0, h = 0;
  uint8_t* img = quirc_begin(g_qr, &w, &h);
  memcpy(img, g_gray, kPixels);
  const uint32_t t0 = millis();
  quirc_end(g_qr);
  const int count = quirc_count(g_qr);
  bool decoded = false;
  for (int i = 0; i < count && !decoded; ++i) {
    quirc_extract(g_qr, i, &g_code);
    quirc_decode_error_t err = quirc_decode(&g_code, &g_data);
    if (err == QUIRC_ERROR_DATA_ECC) {  // the code may be seen mirrored
      quirc_flip(&g_code);
      err = quirc_decode(&g_code, &g_data);
    }
    if (err != QUIRC_SUCCESS) continue;
    decoded = true;

    char text[QR_TEXT_MAX + 1];
    payloadText(g_data, text, sizeof(text));
    char ip[16] = "";
    const char* raw = reinterpret_cast<const char*>(g_data.payload);
    const size_t rawLen = static_cast<size_t>(g_data.payload_len);
    const ipv4::Result r = QR_ALLOW_EMBEDDED_IPV4 ? ipv4::extractUnique(raw, rawLen, ip) : ipv4::validate(raw, rawLen, ip);
    const bool ok = r == ipv4::Result::kOk;
    portENTER_CRITICAL(&g_statsMux);
    g_stats.qrAtMs = nowMs;
    g_stats.qrOk = ok;
    strlcpy(g_stats.qrText, text, sizeof(g_stats.qrText));
    strlcpy(g_stats.qrIp, ok ? ip : "", sizeof(g_stats.qrIp));
    strlcpy(g_stats.qrErr, ok ? "" : ipv4::resultName(r), sizeof(g_stats.qrErr));
    portEXIT_CRITICAL(&g_statsMux);
    if (!strcmp(text, g_qrLastText) && nowMs - g_qrLastSentMs < QR_RESEND_SAME_MS) continue;
    strlcpy(g_qrLastText, text, sizeof(g_qrLastText));
    g_qrLastSentMs = nowMs;
    linkSendQr(ok, ok ? ip : "", text, ok ? "" : ipv4::resultName(r));
    Serial.printf("QR \"%s\" -> %s %s\n", text, ok ? "ip" : "rejected", ok ? ip : ipv4::resultName(r));
  }
  portENTER_CRITICAL(&g_statsMux);
  g_stats.qrAttempts++;
  if (decoded) g_stats.qrDecoded++;
  g_stats.lastQrMs = millis() - t0;
  portEXIT_CRITICAL(&g_statsMux);
}

void visionTask(void*) {
  uint32_t lastFrameMs = millis();
  float fps = 0.0f;
  bool wasMotion = false;
  for (;;) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (fb->format != PIXFORMAT_RGB565 || fb->width != CAM_W || fb->height != CAM_H || fb->len < kPixels * 2) {
      esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (g_streamClients > 0 && xSemaphoreTake(g_streamLock, 0) == pdTRUE) {
      memcpy(g_streamBuf, fb->buf, kPixels * 2);
      g_streamSeq = g_streamSeq + 1;
      xSemaphoreGive(g_streamLock);
    }
    if (xSemaphoreTake(g_grayLock, pdMS_TO_TICKS(200)) == pdTRUE) {
      toGray(fb->buf);
      xSemaphoreGive(g_grayLock);
    }
    esp_camera_fb_return(fb);

    const uint32_t now = millis();
    const LinkConfig cfg = linkConfig();
    if (cfg.motion) {
      runMotion(now);
    } else if (wasMotion) {
      resetMotion(0);
    }
    wasMotion = cfg.motion;
    if (cfg.qr) runQr(now);

    const uint32_t dt = now - lastFrameMs;
    lastFrameMs = now;
    if (dt > 0) fps = fps == 0.0f ? 1000.0f / dt : fps * 0.9f + (1000.0f / dt) * 0.1f;
    portENTER_CRITICAL(&g_statsMux);
    g_stats.fps = fps;
    g_stats.frames++;
    if (!cfg.motion) g_stats.motionDetected = false;
    portEXIT_CRITICAL(&g_statsMux);
  }
}

void* bigAlloc(size_t n) {
  void* p = psramFound() ? ps_malloc(n) : malloc(n);
  if (p) memset(p, 0, n);
  return p;
}
}  // namespace

bool visionBegin() {
  g_gray = static_cast<uint8_t*>(bigAlloc(kPixels));
  g_streamBuf = static_cast<uint8_t*>(bigAlloc(kPixels * 2));
  g_grayLock = xSemaphoreCreateMutex();
  g_streamLock = xSemaphoreCreateMutex();
  g_qr = quirc_new();
  if (!g_gray || !g_streamBuf || !g_grayLock || !g_streamLock || !g_qr) return false;
  return quirc_resize(g_qr, CAM_W, CAM_H) == 0;
}

void visionStartTask() {
  // Core 1: Wi-Fi and the HTTP servers run on core 0. Stack: quirc_decode()
  // keeps a ~8.9 KB struct datastream on the stack, so 8 KB overflowed (and
  // rebooted the camera) the moment a real QR code was decoded.
  xTaskCreatePinnedToCore(visionTask, "vision", VISION_TASK_STACK_BYTES, nullptr, 4, &g_visionTask, 1);
}

uint32_t visionStackFreeBytes() {
  return g_visionTask ? uxTaskGetStackHighWaterMark(g_visionTask) : 0;  // bytes on ESP32
}

VisionStats visionStats() {
  portENTER_CRITICAL(&g_statsMux);
  const VisionStats s = g_stats;
  portEXIT_CRITICAL(&g_statsMux);
  return s;
}

void streamClientDelta(int delta) {
  portENTER_CRITICAL(&g_statsMux);
  g_streamClients = g_streamClients + delta;
  if (g_streamClients < 0) g_streamClients = 0;
  portEXIT_CRITICAL(&g_statsMux);
}

bool streamEncodeLatest(uint32_t* seq, uint8_t** jpg, size_t* len) {
  if (!g_streamLock || xSemaphoreTake(g_streamLock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  bool ok = false;
  if (g_streamSeq != *seq) {
    ok = fmt2jpg(g_streamBuf, kPixels * 2, CAM_W, CAM_H, PIXFORMAT_RGB565, STREAM_JPEG_QUALITY, jpg, len);
    *seq = g_streamSeq;
  }
  xSemaphoreGive(g_streamLock);
  return ok;
}

bool debugGrayJpeg(uint8_t** jpg, size_t* len) {
  if (!g_grayLock || xSemaphoreTake(g_grayLock, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  const bool ok = fmt2jpg(g_gray, kPixels, CAM_W, CAM_H, PIXFORMAT_GRAYSCALE, 80, jpg, len);
  xSemaphoreGive(g_grayLock);
  return ok;
}

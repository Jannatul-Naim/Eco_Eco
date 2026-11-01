// Camera side of the UART link: receives detector configuration and the pan
// mode from the S3, sends heartbeats (with the pan status) and observations.
#pragma once

#include <Arduino.h>

#include "rover_protocol.h"

// Who decides what the camera does: the S3 over the link, or (only while the
// S3 is silent) the bench page on the camera's own web server.
enum class CfgSource : uint8_t { None, S3, Bench };

struct LinkConfig {
  bool qr = false;
  bool motion = false;
  boardlink::PanMode panMode = boardlink::PanMode::Hold;
  float panDeg = boardlink::kPanHomeDeg;  // used in Goto
  float pulseUs = 0.0f;                   // used in Pulse
  CfgSource source = CfgSource::None;
  bool fresh = false;  // S3 config within LINK_CFG_STALE_MS, or bench within BENCH_KEEPALIVE_MS
};

inline const char* cfgSourceName(CfgSource s) {
  return s == CfgSource::S3 ? "s3" : (s == CfgSource::Bench ? "bench" : "none");
}

struct CamHealth {
  const char* sensor;
  char pid[8];
  float fps;
  char ip[16];
  const char* resetReason;
  bool cameraOk;
};

void linkBegin();
void linkPoll(uint32_t nowMs, const CamHealth& health);
// The S3's config while it talks. Otherwise the bench settings while the bench
// page keeps refreshing them; otherwise detectors off and pan Hold.
LinkConfig linkConfig();
// Bench mode, for testing the camera without the S3. Thread-safe.
void linkSetBench(const LinkConfig& c);
// Observations. Thread-safe; called from the vision task.
void linkSendQr(bool ok, const char* ip, const char* text, const char* err);
void linkSendMotion(bool detected, float cx, float cy, float area, uint32_t epoch, bool baseline);

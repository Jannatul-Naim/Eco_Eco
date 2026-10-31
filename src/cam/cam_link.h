// Camera side of the UART link: receives detector configuration and pan
// state from the S3, sends heartbeats and observations.
#pragma once

#include <Arduino.h>

enum class PanInfo : uint8_t { Absent, Moving, Settled };

struct LinkConfig {
  bool qr = false;
  bool motion = false;
  PanInfo pan = PanInfo::Absent;
  uint32_t panEpoch = 0;
  bool fresh = false;  // config received within LINK_CFG_STALE_MS
};

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
LinkConfig linkConfig();
// Observations. Thread-safe; called from the vision task.
void linkSendQr(bool ok, const char* ip, const char* text, const char* err);
void linkSendMotion(bool detected, float cx, float cy, float area, uint32_t epoch, bool baseline);

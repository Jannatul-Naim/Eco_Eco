// UART link to the ESP32-CAM. Receives observations (heartbeat, QR, motion)
// and sends the camera configuration (which detectors run, pan state).
//
// SAFETY: received messages can only update the CamState struct below.
// Nothing in this module can reach the motor outputs.
#pragma once

#include <Arduino.h>

#include "rover_protocol.h"
#include "rover_types.h"

struct CamState {
  bool everSeen = false;
  uint32_t lastRxMs = 0;       // any valid message
  uint32_t lastHbMs = 0;
  char ip[16] = "";
  char sensor[16] = "";
  char pid[8] = "";
  float fps = 0.0f;
  uint32_t camUptimeMs = 0;
  bool qrArmed = false;        // as reported by the camera
  bool motArmed = false;
  uint32_t rxOk = 0;
  uint32_t rxBad = 0;
  uint32_t versionMismatch = 0;

  uint32_t qrSeq = 0;          // increments for every QR message
  uint32_t qrMs = 0;
  bool qrValid = false;
  char qrText[65] = "";
  char qrIp[16] = "";
  char qrErr[16] = "";

  uint32_t motSeq = 0;
  uint32_t motMs = 0;
  bool motDetected = false;
  float motCx = 0.0f;
  float motCy = 0.0f;
  float motArea = 0.0f;
  uint32_t motEpoch = 0;       // pan epoch the camera's baseline belongs to
};

struct CamRequest {
  bool qr = false;
  bool motion = false;
  PanState pan = PanState::Absent;
  float panDeg = 0.0f;
  uint32_t panEpoch = 0;
};

class CamLink {
 public:
  void begin();
  void poll(uint32_t nowMs);
  // Sends immediately when the request changed, otherwise every CAM_CFG_PERIOD_MS.
  void request(const CamRequest& req, uint32_t nowMs);
  const CamState& state() const { return st_; }
  bool fresh(uint32_t nowMs) const;
  const char* linkName(uint32_t nowMs) const;

 private:
  void handleLine(const char* line, size_t len, uint32_t nowMs);
  void send(const CamRequest& req);
  boardlink::LineAssembler rx_;
  CamState st_;
  CamRequest lastSent_;
  uint32_t lastSendMs_ = 0;
  uint32_t txSeq_ = 0;
};

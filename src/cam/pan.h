// Camera-pan servo (MG996R; any 50 Hz positional hobby servo), driven from
// PAN_PIN by the LEDC peripheral. The S3 picks the mode over the link; scanning
// and target following run here, next to the motion detector, so a recentring
// move needs no round trip.
#pragma once

#include <Arduino.h>

#include "cam_link.h"
#include "rover_protocol.h"

struct PanStatus {
  boardlink::PanState state;  // Absent when PAN_SERVO_FITTED is false
  boardlink::PanMode mode;    // mode being applied (Hold while the S3 is silent)
  float angle;
  float target;
  float pulseUs;
  uint32_t epoch;             // increments at each new movement
  bool tracking;              // Track mode with a recent motion detection
};

void panBegin();
// Applies the S3's request and steps the servo. Call often from loop().
void panUpdate(uint32_t nowMs, const LinkConfig& cfg);
// Thread-safe.
PanStatus panStatus();
// Vision task: a motion frame was evaluated against a baseline taken after the
// last settle.
void panMotionSeen(bool detected, float cx, uint32_t nowMs);

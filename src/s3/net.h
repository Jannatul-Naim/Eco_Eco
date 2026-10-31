// Wi-Fi bring-up and the optional audio-board heartbeat listener.
#pragma once

#include <Arduino.h>

struct NetInfo {
  const char* mode;   // "sta", "ap", "down"
  char ssid[33];
  char ip[16];
  int rssi;
};

struct AudioStatus {
  bool online;
  char ip[16];
  uint32_t lastMs;
  int micLevel;      // 0..100 as reported by the audio board
  int clients;
  bool speakerActive;
};

void netBegin();
NetInfo netInfo();
AudioStatus audioStatus(uint32_t nowMs);

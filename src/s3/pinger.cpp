#include "pinger.h"

#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>

void Pinger::onSuccess(void* hdl, void* arg) {
  Pinger* self = static_cast<Pinger*>(arg);
  uint32_t elapsed = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
  portENTER_CRITICAL(&self->mux_);
  self->res_.received++;
  self->res_.rttMs = elapsed;
  portEXIT_CRITICAL(&self->mux_);
}

void Pinger::onTimeout(void* hdl, void* arg) {
  (void)hdl;
  (void)arg;
}

void Pinger::onEnd(void* hdl, void* arg) {
  Pinger* self = static_cast<Pinger*>(arg);
  uint32_t sent = 0, received = 0;
  esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &sent, sizeof(sent));
  esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
  portENTER_CRITICAL(&self->mux_);
  self->res_.sent = sent;
  self->res_.received = received;
  self->res_.state = received > 0 ? PingState::Reply : PingState::Timeout;
  self->res_.finishedMs = millis();
  portEXIT_CRITICAL(&self->mux_);
  esp_ping_delete_session(hdl);
}

bool Pinger::start(const char* ip, uint32_t count, uint32_t timeoutMs) {
  if (busy()) return false;
  PingResult fresh;
  strncpy(fresh.target, ip, sizeof(fresh.target) - 1);
  fresh.startedMs = millis();

  ip_addr_t target;
  if (!ipaddr_aton(ip, &target)) {
    fresh.state = PingState::Error;
    snprintf(fresh.error, sizeof(fresh.error), "bad address");
  } else {
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count = count;
    cfg.timeout_ms = timeoutMs;
    cfg.interval_ms = 1000;
    esp_ping_callbacks_t cbs = {};
    cbs.cb_args = this;
    cbs.on_ping_success = onSuccess;
    cbs.on_ping_timeout = onTimeout;
    cbs.on_ping_end = onEnd;
    esp_ping_handle_t handle = nullptr;
    fresh.state = PingState::Running;
    portENTER_CRITICAL(&mux_);
    res_ = fresh;
    portEXIT_CRITICAL(&mux_);
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &handle);
    if (err == ESP_OK) err = esp_ping_start(handle);
    if (err == ESP_OK) return true;
    if (handle) esp_ping_delete_session(handle);
    fresh.state = PingState::Error;
    snprintf(fresh.error, sizeof(fresh.error), "ping start failed: %s", esp_err_to_name(err));
  }
  fresh.finishedMs = millis();
  portENTER_CRITICAL(&mux_);
  res_ = fresh;
  portEXIT_CRITICAL(&mux_);
  return false;
}

PingResult Pinger::result() const {
  portENTER_CRITICAL(&mux_);
  PingResult r = res_;
  portEXIT_CRITICAL(&mux_);
  return r;
}

bool Pinger::busy() const {
  portENTER_CRITICAL(&mux_);
  const bool b = res_.state == PingState::Running;
  portEXIT_CRITICAL(&mux_);
  return b;
}

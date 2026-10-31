#include "console.h"

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "events.h"
#include "net.h"
#include "rover.h"

namespace {
char g_line[96];
size_t g_len = 0;

// "drive" from the console repeats the command until the deadline so the
// teleop timeout does not stop a deliberate bench test.
struct {
  bool active = false;
  float l = 0, r = 0;
  uint32_t untilMs = 0;
  uint32_t lastSentMs = 0;
} g_drive;

RoverSnapshot g_snap;

void printResult(const CmdResult& r) { Serial.printf("%s (%d): %s\n", r.ok ? "OK" : "REJECTED", r.http, r.message); }

template <typename Fn>
void withLock(Fn fn) {
  RoverLock lock(100);
  if (!lock.ok()) {
    Serial.println("rover busy, retry");
    return;
  }
  fn();
}

void printSonar(const RoverSnapshot& s) {
  for (int i = 0; i < 4; ++i) {
    const SonarReading& r = s.sonar[i];
    Serial.printf("  sonar %-12s fitted=%d status=%-15s mm=%5d age=%lums\n", SONAR_NAME[i], s.sonarFitted[i],
                  sonarStatusName(r.status), r.mm, static_cast<unsigned long>(r.tMs ? s.nowMs - r.tMs : 0));
  }
  Serial.printf("  reverse allowed=%d (%s)\n", s.reverseAllowed, s.reverseReason);
}

void printStatus() {
  withLock([] { g_rover.snapshot(&g_snap); });
  const RoverSnapshot& s = g_snap;
  const NetInfo n = netInfo();
  Serial.printf("mode=%s state=%s mission=%s detail=\"%s\"\n", modeName(s.mode), stateName(s.state),
                missionName(s.mission), s.detail);
  Serial.printf("estop active=%d hw_sense=%s source=%s | motors armed=%d L=%.2f R=%.2f\n", s.estopActive,
                s.estopSense, s.estopSource, s.motorsArmed, s.outLeft, s.outRight);
  Serial.printf("battery wired=%d v=%.2f | motor bus wired=%d v=%.2f\n", s.battWired, s.battV, s.busWired, s.busV);
  printSonar(s);
  Serial.printf("pan present=%d state=%s angle=%.1f target=%.1f pulse=%.0fus\n", s.panPresent, panStateName(s.pan),
                s.panDeg, s.panTarget, s.panPulseUs);
  Serial.printf("camera link=%s ip=%s sensor=%s pid=%s fps=%.1f rx_ok=%lu rx_bad=%lu\n", s.camLink, s.cam.ip,
                s.cam.sensor, s.cam.pid, s.cam.fps, static_cast<unsigned long>(s.cam.rxOk),
                static_cast<unsigned long>(s.cam.rxBad));
  Serial.printf("qr text=\"%s\" ip=%s valid=%d | ping state=%s target=%s sent=%lu recv=%lu rtt=%lums\n",
                s.cam.qrText, s.cam.qrIp, s.cam.qrValid, pingStateName(s.ping.state), s.ping.target,
                static_cast<unsigned long>(s.ping.sent), static_cast<unsigned long>(s.ping.received),
                static_cast<unsigned long>(s.ping.rttMs));
  Serial.printf("wifi %s ssid=%s ip=%s rssi=%d | heap=%lu\n", n.mode, n.ssid, n.ip, n.rssi,
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

void i2cScan() {
  withLock([] {
    g_rover.snapshot(&g_snap);
    if (g_snap.motorsArmed) {
      Serial.println("refused: motors armed (switch to idle first)");
      return;
    }
    Serial.printf("I2C scan on SDA=%d SCL=%d:\n", PIN_I2C_SDA, PIN_I2C_SCL);
    int found = 0;
    for (uint8_t a = 1; a < 127; ++a) {
      g_i2c.beginTransmission(a);
      if (g_i2c.endTransmission() == 0) {
        Serial.printf("  device at 0x%02X%s\n", a,
                      a == PCA9685_I2C_ADDR ? "  <- expected PCA9685" : (a == 0x70 ? "  (PCA9685 all-call)" : ""));
        ++found;
      }
    }
    Serial.printf("%d device(s) found\n", found);
  });
}

void printEvents() {
  EventRecord recs[EventLog::kCapacity];
  const size_t n = g_events.recent(recs, EventLog::kCapacity);
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("  #%lu %8lums %-5s %s\n", static_cast<unsigned long>(recs[i].seq),
                  static_cast<unsigned long>(recs[i].ms), evLevelName(recs[i].level), recs[i].text);
  }
}

void help() {
  Serial.println(
      "commands:\n"
      "  status | sonar | i2cscan | events | wifi\n"
      "  estop | reset | ack\n"
      "  mode idle | mode teleop | mode auto <traverse|proximity|qr_ping|motion>\n"
      "  start | abort\n"
      "  drive <left> <right> [ms]   (teleop only; -1..1, default 500 ms, max 3000 ms)\n"
      "  stop\n"
      "  pan <deg> | pulse <us>       (pulse only in IDLE, for calibration)\n"
      "  qr                           (teleop: decode QR + on-board ping)\n"
      "  motion on|off                (teleop: camera motion detection)");
}

void execute(char* line) {
  char* argv[5] = {};
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 5; tok = strtok(nullptr, " \t")) argv[argc++] = tok;
  if (argc == 0) return;
  const char* cmd = argv[0];

  if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
    help();
  } else if (!strcmp(cmd, "status")) {
    printStatus();
  } else if (!strcmp(cmd, "sonar")) {
    withLock([] { g_rover.snapshot(&g_snap); });
    printSonar(g_snap);
  } else if (!strcmp(cmd, "i2cscan")) {
    i2cScan();
  } else if (!strcmp(cmd, "events")) {
    printEvents();
  } else if (!strcmp(cmd, "wifi")) {
    const NetInfo n = netInfo();
    Serial.printf("wifi %s ssid=%s ip=%s rssi=%d\n", n.mode, n.ssid, n.ip, n.rssi);
  } else if (!strcmp(cmd, "estop")) {
    g_drive.active = false;
    g_rover.requestEstop();
    withLock([] { printResult(g_rover.engageEstop(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "reset")) {
    withLock([] { printResult(g_rover.resetEstop(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "ack")) {
    withLock([] { printResult(g_rover.ackFault(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "mode") && argc >= 2) {
    Mode m;
    Mission mission = Mission::None;
    if (!parseMode(argv[1], &m) || (argc >= 3 && !parseMission(argv[2], &mission))) {
      Serial.println("usage: mode idle|teleop|auto <mission>");
      return;
    }
    const bool given = argc >= 3;
    g_drive.active = false;
    withLock([&] { printResult(g_rover.setMode(m, mission, given, CmdSource::Serial)); });
  } else if (!strcmp(cmd, "start")) {
    withLock([] { printResult(g_rover.startMission(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "abort")) {
    withLock([] { printResult(g_rover.abortMission(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "drive") && argc >= 3) {
    const float l = atof(argv[1]);
    const float r = atof(argv[2]);
    uint32_t ms = argc >= 4 ? static_cast<uint32_t>(atoi(argv[3])) : 500;
    if (ms > 3000) ms = 3000;
    CmdResult res{false, 503, "busy"};
    withLock([&] { res = g_rover.drive(l, r, CmdSource::Serial); });
    printResult(res);
    if (res.ok) {
      // Field by field: GCC 8 (C++11 rules) does not aggregate-initialise a
      // struct that has default member initialisers.
      g_drive.active = true;
      g_drive.l = l;
      g_drive.r = r;
      g_drive.untilMs = millis() + ms;
      g_drive.lastSentMs = millis();
    }
  } else if (!strcmp(cmd, "stop")) {
    g_drive.active = false;
    withLock([] { printResult(g_rover.drive(0, 0, CmdSource::Serial)); });
  } else if (!strcmp(cmd, "pan") && argc >= 2) {
    const float deg = atof(argv[1]);
    withLock([&] { printResult(g_rover.setPan(deg, CmdSource::Serial)); });
  } else if (!strcmp(cmd, "pulse") && argc >= 2) {
    const float us = atof(argv[1]);
    withLock([&] { printResult(g_rover.setPanPulse(us, CmdSource::Serial)); });
  } else if (!strcmp(cmd, "qr")) {
    withLock([] { printResult(g_rover.triggerQrPing(CmdSource::Serial)); });
  } else if (!strcmp(cmd, "motion") && argc >= 2) {
    const bool on = !strcmp(argv[1], "on");
    withLock([&] { printResult(g_rover.setMotionDetect(on, CmdSource::Serial)); });
  } else {
    Serial.println("unknown command; type 'help'");
  }
}
}  // namespace

void consoleLoop(uint32_t nowMs) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      g_line[g_len] = '\0';
      if (g_len > 0) {
        Serial.printf("> %s\n", g_line);
        execute(g_line);
      }
      g_len = 0;
    } else if (g_len + 1 < sizeof(g_line)) {
      g_line[g_len++] = c;
    }
  }
  if (g_drive.active) {
    if (static_cast<int32_t>(nowMs - g_drive.untilMs) >= 0) {
      g_drive.active = false;
      withLock([] { g_rover.drive(0, 0, CmdSource::Serial); });
      Serial.println("console drive finished");
    } else if (nowMs - g_drive.lastSentMs >= 100) {
      g_drive.lastSentMs = nowMs;
      CmdResult res{false, 503, "busy"};
      withLock([&] { res = g_rover.drive(g_drive.l, g_drive.r, CmdSource::Serial); });
      if (!res.ok) {
        g_drive.active = false;
        printResult(res);
      }
    }
  }
}

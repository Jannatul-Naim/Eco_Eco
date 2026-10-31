# 8. Status and assumptions

Last updated: 31 Oct 2026. Update this page whenever a stage in
[07_test_checklist.md](07_test_checklist.md) passes or a part is confirmed.

## 8.1 Compiled (software only)

Built with PlatformIO Core 6.2.0, platform `espressif32 @ 6.13.0` (Arduino-ESP32 2.0.17):

| Target | Result | RAM | Flash |
|---|---|---|---|
| `s3_main` (ESP32-S3) | SUCCESS | 17.3 % | 72.5 % of 1.25 MB app partition |
| `cam` (ESP32-CAM) | SUCCESS | 24.7 % | 29.3 % of 3 MB (`huge_app`) |
| `audio` (ESP32 DevKit, optional) | SUCCESS | 24.8 % | 66.3 % |
| `native` unit tests | 6 / 6 passed | — | — |

The dashboard (`data/`) was rendered in Chromium with **sample** telemetry at 1366×680, 1920×960 and 375 px
(phone). Laptop sizes fit one screen without page scrolling, it had no script errors, and the controls
locked as expected in AUTO. It has not been
connected to a real rover. A worst-case telemetry frame (every string at its maximum length)
serialises to 2,715 bytes, inside the S3's 4,096-byte WebSocket buffer.

Fixes made while completing the project:

- The shared protocol namespace `link` collided with POSIX `link()` from `<unistd.h>` and broke
  the S3 build. Renamed to `boardlink`.
- `console.cpp` used brace assignment on a struct with default member initialisers, which
  GCC 8 rejects. Changed to field-by-field assignment.
- The camera board's `main.cpp` and `vision.cpp`, the whole audio firmware, the dashboard and
  all documents were missing from the generated project; they are now written.
- `POST /api/mode {"mode":"auto"}` without a mission (the rulebook's contract body) now arms
  `qr_ping` instead of answering 400.
- PlatformIO's own Python needed `intelhex` for esptool (`~/.platformio/penv/bin/pip install intelhex`).

## 8.2 Physically tested

**Nothing yet.** No firmware has been uploaded, no board has been powered with this code, and
no wiring exists. Fill this in from the checklist:

| Stage | Passed on | Notes |
|---|---|---|
| 1 S3 USB boot | | |
| 2 E-stop | | |
| 3 Wi-Fi + dashboard | | |
| 4 Sonars | | |
| 5 I²C | | |
| 6 SG90 | | |
| 7 Camera | | |
| 8 Motors raised | | |
| 9 Audio | | |
| 10 Dashboard + gamepad | | |
| 11 Missions | | |
| 12 Integrated safety | | |
| 13 Floor run | | |

## 8.3 Uncertain or unknown

| Item | Current assumption | What resolves it |
|---|---|---|
| S3 board identity ("ESP32 23 2022 V1.3") | DevKitC-1-compatible header, maybe a YD-ESP32-S3 | photos of both sides + header silkscreen (stage 1) |
| S3 flash size / PSRAM | 4 MB flash used, PSRAM not used | boot banner; works on 4/8/16 MB modules |
| ESP32-CAM layout | AI-Thinker | camera init success at stage 7 |
| Camera sensor (RHYX M21-45) | GC2145-family hypothesis, RGB565 at 320×240 | PID printed at boot |
| RGB565 byte order | high byte first | `/gray.jpg` check |
| Motors | Pololu #4744 baseline; the team's notes list "25GA 300 rpm" | label/datasheet of the motors actually fitted |
| Motor drivers | MD30C baseline; the team's notes list BTS7960 × 2 or a 10 A dual H-bridge | which drivers are actually fitted (the 10 A dual is too small for the baseline motors) |
| Battery and charger | 3S Li-ion with BMS ≥ 25 A (recommended, not chosen) | purchase |
| Sonar count | 1 owned (front-center fitted in config), 4 planned | purchase; enable each in `SONAR_FITTED` |
| PCA9685 address | 0x40 | `i2cscan` |
| SG90 pulse endpoints | 500 / 2400 µs nominal, soft range 45–135° | stage 6 calibration |
| Pan direction vs image | `MOTION_PAN_DIRECTION = +1` | stage 7/11 check |
| Proximity controller gains | `PROX_MIN_DUTY 0.12`, `PROX_GAIN_PER_MM 0.0015` | floor tuning, stage 13 |
| INMP441 slot | left (`MIC_READ_RIGHT_SLOT = false`) | stage 9 level check |
| E-stop and contactor parts | recommended only | purchase + stage 2 |
| Arena network | unknown until the technical briefing | set `WIFI_STA_*` in all `secrets.h`, `WIFI_SOFTAP_FALLBACK = false` |

## 8.4 Known limits (by design)

- **Traverse cannot end as COMPLETE.** There are no wheel encoders and no lane-end marker
  sensor, and the firmware does not declare success from a timer. Pololu #4754 (the encoder
  version of the motor) or a downward line/marker sensor would make it verifiable.
- **Colour and shape detection are out of scope** (team decision). The rulebook allows ×4 for
  it in AUTO, so it is a possible later addition.
- **Touch target and box smash** have no sensor or actuator support and are not claimed.
- The camera stream allows **one viewer at a time**.
- Talk-back audio needs the dashboard on `http://localhost` (browser secure-context rule).
- The S3 bench access point must be **disabled for competition** (rule 1.3.1).

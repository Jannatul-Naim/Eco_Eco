# 8. Status and assumptions

Last updated: 1 Nov 2026. Update this page whenever a stage in
[07_test_checklist.md](07_test_checklist.md) passes or a part is confirmed.

## 8.1 Compiled (software only)

Built with PlatformIO Core 6.2.0, platform `espressif32 @ 6.13.0` (Arduino-ESP32 2.0.17):

| Target | Result | RAM | Flash |
|---|---|---|---|
| `s3_main` (ESP32-S3) | SUCCESS | 17.2 % | 71.2 % of 1.25 MB app partition |
| `cam` (ESP32-CAM) | SUCCESS | 24.7 % | 29.4 % of 3 MB (`huge_app`) |
| `audio` (ESP32 DevKit, optional) | SUCCESS | 24.8 % | 66.3 % |
| `native` unit tests | 7 / 7 passed | — | — |

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
- Native-USB build: log writes use a 1 ms timeout so a USB host that stops reading cannot stall
  the control loop. A 0 ms timeout is unsafe in Arduino-ESP32 2.0.17 (the retry counter underflows).
- Found on the real rover: `connection.ssid`, `connection.ip` and `audio.ip` came out as garbage
  bytes. ArduinoJson 7.4 keeps only a pointer to any `const char[N]`, as if it were a string
  literal, and these came from local copies that no longer existed at serialisation. The invalid
  UTF-8 made browsers drop the `/ws` telemetry socket about once a second, so the dashboard fell
  back to 1 s polling. `web.cpp` now passes them as `const char*`, which ArduinoJson copies.
- **Team decision, 1 Nov 2026: the SG90 pan servo moved from the S3 (via a PCA9685) to the
  ESP32-CAM (IO2, LEDC 50 Hz).** The camera now sweeps for QR codes and follows motion targets
  itself; the S3 only picks the pan mode over the link (protocol version 2). The PCA9685, the S3's
  I²C bus and `i2cscan` are gone; S3 GPIO8/9 are spare. Bring-up stages 5–7 were rewritten.
- Camera **bench mode** (1 Nov 2026): `http://<cam-ip>/` runs QR, motion and pan on the camera
  alone while the S3 is silent, so the camera tasks can be tested before the link is wired.
  Checked on the board: `POST /bench` is applied (`source: bench`), the QR decoder ran while
  switched on, and the camera serves the page. The 3 s lapse after closing the page is not
  checked yet.
- **Found on the camera: a real QR code crashed it.** `quirc_decode()` keeps a ~8.9 KB buffer on
  the stack and the vision task had 8 KB, so the camera rebooted as soon as a code was decoded
  (the team's QR test showed the code sharp in the stream, 0 decoded, and a fresh uptime). The
  task now has 24 KB (`VISION_TASK_STACK_BYTES`), and `/status` reports `vision_stack_free`.
  After the fix the camera stays up through a decode (stack low-water mark 14.5 KB free of 24 KB).
- **QR decoding checked offline on real camera frames** (1 Nov 2026): the vendored quirc, built on
  the PC with the camera's flags, decoded 14 of the 36 frames where the team held a code
  (`127.0.0.1`, which the firmware correctly rejects as `not_unicast`). Decoding a half-size copy
  never helped on real frames, so it was not added. A code that fills the whole picture is at the
  edge of what the fixed-focus lens resolves; about half the picture height works better.
- **Found with the link wired: the camera link flapped** (`camera link lost: no heartbeat for
  1010 ms` / `camera link up` every ~1.3 s) and the dashboard picture went on and off. The
  camera's `loop()` (heartbeats, link, pan) ran at priority 1 on the same core as the vision task
  and the stream's software JPEG encoding, so heartbeats came up to 1.3 s late. Fixes: the
  dashboard keeps the stream whenever the camera IP is known; the S3 logs link losses; the camera
  raises `loop()` to priority 6 and runs its HTTP/stream servers on core 0. The camera part is
  built but needs one USB flash.
- **Found on the dashboard: RESET E-STOP engaged the E-stop.** ESPAsyncWebServer matches a plain
  route like `/api/estop` as `/api/estop` *or* `/api/estop/...`, and it was registered before
  `/api/estop/reset`, so the reset request ran the E-stop handler. All POST command routes now
  use `AsyncURIMatcher::exact()`. Tested on the S3: E-STOP → ESTOP, RESET → IDLE. The S3 also
  reports its last `reset_reason` (banner and `/api/status`).
- **Dashboard rearranged (2 Nov 2026):** four columns (video and drive; task control, power and connection; task readings; events, link and commands) and a light/dark theme switch. On the real rover at 1366×680: no page scroll, no column overflow, telemetry 76 ms. On windows under 760 px tall the secondary timestamp rows are hidden so all controls still fit.
- **Found while testing: the console `status` command could crash the S3.** When the rover lock was busy it printed "rover busy" and then read an empty snapshot, and a null text pointer rebooted the board (LoadProhibited in `strlen`). `status` and `sonar` now stop there. The same command sequence no longer crashes, though the busy case did not recur in that retest.
- **Camera wireless updates (OTA):** partition table `min_spiffs.csv` (two 1.9 MB app slots),
  ArduinoOTA on port 3232 with `CAM_OTA_PASSWORD` from `src/cam/secrets.h`, and
  `tools/cam_ota.sh`. **Verified 1 Nov 2026:** USB-flashed once on the MB, then
  `tools/cam_ota.sh 192.168.0.207` updated it over Wi-Fi (`Result: OK`, camera back after a
  software reset). Camera updates no longer need the MB board.

## 8.2 Physically tested

No stage has passed yet and no wiring exists. On 31 Oct 2026 the S3 firmware (`s3_main`) and
the dashboard file system were **uploaded** to the team's board (esptool: hash verified). The
serial monitor stayed blank. On 1 Nov the board turned out to be on its CH343 bridge port, where
`s3_main` prints, and the `s3_main_nativeusb` build flashed in between never printed there. Esptool reported: ESP32-S3 (QFN56) rev v0.2, embedded 8 MB
PSRAM, USB-Serial/JTAG, MAC fc:01:2c:d1:5c:6c.

On 1 Nov 2026 the rover (the `s3_main_nativeusb` build with the 0 ms timeout, uptime 5 h 47 min) was
read over the network from the team's PC on Ethernet. `/api/events` showed it had joined the team's
network "Neonis" as 192.168.0.139 (RSSI −65), and `urrt-rover.local` resolved to that address. Boot
events: PCA9685 not found at 0x40 (expected, nothing on I²C); BOOT → ESTOP because the E-stop
sense input reads open (the intended fail-safe with nothing wired); "rear sonar not fitted";
"boot complete". Outputs were disabled. The front-center sonar reported a steady ~1,555 mm `ok`
with an occasional 3 mm / `echo_stuck_high`, although the team confirmed **nothing is wired**: the
echo input (GPIO11, plain `INPUT`) was floating. The echo pins now use the internal pull-down, so
an unwired or broken echo line reads `no_echo` (not fresh: blocks AUTO and traverse). The
dashboard loaded from the rover, but its `/ws` socket kept dropping (bug above).

Later on 1 Nov 2026, all three boards were uploaded from the team's PC
(all hashes verified). Only USB is connected; nothing else is wired.

- **S3** (CH343 bridge port, `s3_main` + dashboard file system). Serial boot log: banner with MAC
  fc:01:2c:d1:5c:6c, 16 MB flash, log on UART0; Wi-Fi STA Neonis 192.168.0.139; same boot events
  as above. `sonar` now shows front_center `no_echo` (pull-down fix confirmed). `/api/status` and
  `/api/sensors` are valid UTF-8 with `ssid: Neonis` (JSON fix confirmed).
- **Audio DevKit** (CP2102): boots, I²S drivers start, joins Neonis as 192.168.0.218, serves HTTP.
  The S3 shows `audio.online = true`, ip 192.168.0.218 (UDP heartbeat works). Mic level 0 because
  no INMP441 is wired.
- **ESP32-CAM** (on the team's ESP32-CAM-MB, which enumerates as an FTDI FT232R and has **no
  auto-reset**: IO0 + RST by hand to flash, RST to run). Boot log: PSRAM found (4 MB),
  `camera OK: sensor GC2145, PID 0x2145, 320x240 RGB565`, joins Neonis as 192.168.0.207.
  `/status` reports 13.7 fps; `/gray.jpg` and the MJPEG stream on port 81 serve 320×240 frames.
  The lens saw a dark, flat scene, so image orientation and byte order are not judged yet. The
  serial shows `cam_hal: EV-EOF-OVF` about every 5 s: the driver dropping a frame while its
  buffers are full. Harmless.

Fill this in from the checklist:

| Stage | Passed on | Notes |
|---|---|---|
| 1 S3 USB boot | | boot log OK on UART0; board identified from the team's photo |
| 2 E-stop | | latches ESTOP at boot with the sense input open, as designed; no switch wired yet |
| 3 Wi-Fi + dashboard | | joins Neonis; dashboard `live · 79 ms` over `/ws`, no console errors; battery divider not fitted |
| 4 Sonars | | front-center wired (2 Nov 2026), bench setup on 3.3 V with no divider: 181/181 valid readings in 30 s, follows a moving object, open-room reading steady at about 1830 mm (1829–1834). With a loose wire it read `no_echo` 31/31, never a fake number. Not passed: no ruler test yet, and the final test needs 5 V with the divider. Rear sonar not fitted |
| 5 Camera + link | | **link steady** (1 Nov 2026, after the camera task fix): heartbeat age ≤ 200 ms, 0 stale samples in 30 s; S3 shows GC2145, IP, ~14 fps; stream serves frames. Camera powered from the DevKit VIN on the bench |
| 6 Pan servo on camera | | MG996R wired to IO2 (2 Nov 2026): camera boots normally with it, follows the dashboard slider, moves and holds (180° type, team observation), no restarts over 14 moves. Sweep 45–135°: smooth, no buzzing (team observation), no restarts or link drops. Rough angles only: nominal 500/2400 µs kept. Camera not mounted on the servo yet, so bracket and cable checks and fine calibration are still to do |
| 7 Camera tasks | | **QR → ping works end to end** (1 Nov 2026, TELEOP): camera decoded `192.168.0.197`, sent it over the link, the S3 re-validated it and pinged it from the rover: reply in 4 ms. Loopback `127.0.0.1` correctly rejected. Bench mode: QR and motion detection work. Motion through the S3 (2 Nov 2026, TELEOP): a hand wave registers; camera at rest gave 0 detections in 55 samples over 8 s (0.0 % changed), so no false alarms from noise. Not tested yet: AUTO `qr_ping` to COMPLETE. **AUTO motion is not passed:** three runs reported COMPLETE in about 5 s, but the camera was not on the servo and the trigger was never explained (motion locked on 0.2–0.3 s after START, while TELEOP detection stayed clean). Retest with the camera mounted, recording frames |
| 8 Motors raised | | |
| 9 Audio | | board runs and reports to the S3; mic/speaker not wired |
| 10 Dashboard + gamepad | | |
| 11 Missions | | |
| 12 Integrated safety | | |
| 13 Floor run | | |

## 8.3 Uncertain or unknown

| Item | Current assumption | What resolves it |
|---|---|---|
| S3 board identity ("ESP32 23 2022 V1.3") | **resolved** (photo, 1 Nov 2026): ESP32-S3-N16R8 module, DevKitC-1 header labels; all mapped pins present | — |
| S3 flash size / PSRAM | **resolved**: 16 MB flash (banner), 8 MB embedded PSRAM (esptool); the build uses the 4 MB layout and no PSRAM | — |
| ESP32-CAM layout | **resolved**: AI-Thinker pins work (camera init OK, PSRAM found) | — |
| Camera sensor (RHYX M21-45) | **resolved**: GC2145, PID 0x2145, RGB565 at 320×240 | — |
| QR density the camera can read | at 320×240, version 1 (21×21) codes decode well at ~half the picture height (≈ 8 px per module); a version-4 (33×33) code from a phone app was found but not decodable (≈ 5 px per module). Version 2 is marginal | the referee's actual code; if it is denser than version 1–2, add a higher-resolution QR mode |
| RGB565 byte order | high byte first; likely right (a flat scene came out smooth, not speckled) | `/gray.jpg` of a scene with detail |
| Motors | Pololu #4744 baseline; the team's notes list "25GA 300 rpm" | label/datasheet of the motors actually fitted |
| Motor drivers | MD30C baseline; the team's notes list BTS7960 × 2 or a 10 A dual H-bridge | which drivers are actually fitted (the 10 A dual is too small for the baseline motors) |
| Battery and charger | 3S Li-ion with BMS ≥ 25 A (recommended, not chosen) | purchase |
| Sonar count | 1 owned (front-center fitted in config), 4 planned | purchase; enable each in `SONAR_FITTED` |
| Pan servo on ESP32-CAM IO2 | **resolved** (2 Nov 2026): boots normally with the servo connected | — |
| MG996R type and signal | **resolved** (2 Nov 2026): moves and holds, so 180° positional; works on the 3.3 V signal | — |
| Servo pulse endpoints | 500 / 2400 µs nominal (`src/cam/config_cam.h`), range 45–135° | stage 6 calibration |
| Pan direction vs image | `MOTION_PAN_DIRECTION = +1` (`src/cam/config_cam.h`) | stage 7 step 5 |
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

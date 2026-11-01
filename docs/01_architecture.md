# 1. Architecture

## 1.1 Boards and authority

```
                         arena / lab Wi-Fi (organiser-assigned channel)
        ┌───────────────────────────┬───────────────────────────┬──────────────────────┐
        │                           │                           │                      │
 ┌──────┴───────┐  HTTP + WS  ┌─────┴──────────────┐   MJPEG    │   UDP 4211 status     │
 │ Operator     │◄───────────►│ ESP32-S3 MAIN      │◄───────────┼──── heartbeat ───┐    │
 │ laptop       │  (teleop    │  * motor authority │            │                  │    │
 │ browser +    │  rejected   │  * E-stop latch    │  UART 115200 (CRC-8 lines)   │    │
 │ gamepad      │  in AUTO)   │  * sonar, missions │◄──────────►┌─────────────┐ ┌──┴────────┐
 └──────────────┘             │  * ICMP ping       │ obs. + pan │ ESP32-CAM   │ │ AUDIO     │
                              │                    │ mode only  │ stream, QR, │ │ (optional)│
                              └──┬────────────┬────┘            │ motion, pan │ │ mic + spk │
                                 │PWM/DIR     │GPIO             └──────┬──────┘ └───────────┘
                          ┌──────┴──┐         └─ 4× HC-SR04            │IO2
                          │2× MD30C │            (1k/1k8 dividers) MG996R camera pan
                          └────┬────┘
                     4 motors (2 per side, in parallel)
           motor and servo power pass through the DC contactor that the E-stop opens
```

The rules this layout enforces:

- **The S3 is the only motor authority.** The camera link has no drive message type; the only
  thing the S3 sends the camera is which detectors to run and a pan mode for the camera's own pan servo. The S3's
  camera parser only writes into `CamState`, and nothing reads `CamState` to produce motor
  output except the S3's own mission code. The audio board can only send a status heartbeat.
- **Perception and decisions run on board in AUTO.** QR decoding, motion detection, pan scanning
  and target following run on the ESP32-CAM; mission logic, the ping and sonar control run on the
  S3. The laptop is not involved: commands from it
  are rejected while `mode == auto` (`Rover::drive`, `setPan`), except E-stop and abort.
- **The hardware E-stop does not depend on software.** It opens the motor-power contactor
  directly. The S3 also reads a second NC contact block and latches `ESTOP`. A software E-stop
  (dashboard, console, gamepad B button) cuts the PWM outputs and latches the same state, but it
  is an extra layer, not a substitute.

## 1.2 S3 state machine

```mermaid
stateDiagram-v2
  [*] --> BOOT
  BOOT --> IDLE: E-stop released
  BOOT --> ESTOP: E-stop pressed / sense wire open
  IDLE --> TELEOP: POST /api/mode teleop
  IDLE --> AUTO_READY: POST /api/mode auto + mission
  TELEOP --> IDLE: mode idle
  TELEOP --> AUTO_READY: mode auto
  AUTO_READY --> AUTO_RUNNING: POST /api/mission/start
  AUTO_RUNNING --> AUTO_BLOCKED: front obstacle (traverse)
  AUTO_BLOCKED --> AUTO_RUNNING: path clear
  AUTO_RUNNING --> AUTO_COMPLETE: mission verified by sensors
  AUTO_RUNNING --> AUTO_FAILED: timeout / not verifiable / abort
  AUTO_BLOCKED --> AUTO_FAILED: still blocked after 3 s
  AUTO_RUNNING --> SAFE_STOP: stale sonar / camera link lost
  AUTO_COMPLETE --> AUTO_RUNNING: START again
  AUTO_FAILED --> AUTO_RUNNING: START again
  SAFE_STOP --> IDLE: POST /api/fault/ack
  ESTOP --> IDLE: hardware released + POST /api/estop/reset
  note right of ESTOP: reachable from every state:\nhardware button, software E-stop,\nmotor bus lost while armed,\ncontrol loop starved 200 ms
```

- Motor outputs are **disabled** in BOOT, IDLE, ESTOP, SAFE_STOP, AUTO_READY, AUTO_COMPLETE and
  AUTO_FAILED. They follow commands only in TELEOP (fresh operator commands) or
  AUTO_RUNNING/AUTO_BLOCKED (mission code).
- Releasing the hardware E-stop never restarts motion. The firmware stays in ESTOP until an
  explicit reset, then goes to IDLE. The contactor also needs the ARM button
  ([03_electrical.md](03_electrical.md) §3.4).

## 1.3 Safety layers on the S3

| Layer | Behaviour | Constant |
|---|---|---|
| Boot | all driver inputs driven LOW before PWM attaches; 10 kΩ pull-downs on the board | `motors.cpp` |
| Teleop timeout | no fresh drive command for 300 ms → stop | `TELEOP_CMD_TIMEOUT_MS` |
| Teleop cap | duty ≤ 60 % | `TELEOP_MAX_DUTY` |
| Slew limit | accel 2.5 /s, decel 6 /s | `MOTOR_ACCEL_PER_S` |
| Motor voltage cap | duty ≤ 12.0 V / V<sub>batt</sub> (0.95 when the battery is not measured) | `MOTOR_RATED_V` |
| Rear interlock | backward component removed unless the rear sonar is fresh and ≥ 200 mm | `REAR_CLEARANCE_MM` |
| Sonar freshness | readings older than 400 ms are not trusted; a driving mission → SAFE_STOP | `SONAR_STALE_MS` |
| Camera freshness | heartbeat older than 1 s → camera missions SAFE_STOP | `CAM_LINK_STALE_MS` |
| Control-loop watchdog | task watchdog 3 s → reboot (outputs off); lock starved 200 ms → E-stop | `CONTROL_WDT_TIMEOUT_S` |
| Motor bus sense (optional) | bus lost while armed → ESTOP | `MOTOR_BUS_SENSE_FITTED` |

## 1.4 Missions (AUTO)

All missions start only from AUTO_READY, after the team has declared autonomy to the referee.

| Mission | Sensors | Ends COMPLETE when | Ends FAILED / SAFE_STOP when |
|---|---|---|---|
| `traverse` | front sonars (FC required, FL/FR optional) | **never**: no encoder or lane-end sensor can verify the lane end | 30 s timeout; blocked > 3 s; sonar stale → SAFE_STOP |
| `proximity` | front-center sonar (median filtered) | 5 consecutive readings inside 100 ± 20 mm while stopped | 20 s timeout; too close with reverse blocked |
| `qr_ping` | camera (QR) sweeping its pan, on-board ICMP | valid IPv4 decoded **and** ping reply | 30 s with no valid QR; no reply (requests still sent and logged); camera lost → SAFE_STOP |
| `motion` | camera frame differencing, camera-side pan sweep + recentring | target detected continuously for 5 s (gaps ≤ 1 s) | 60 s timeout; camera lost → SAFE_STOP |

**QR + ping detail.** The camera runs quirc on the 320×240 luma image only while the S3
requests it. Every decoded payload is checked with `ipv4::extractUnique`: exactly one distinct
unicast IPv4 address must appear in the text. The S3 validates the address **again** before it
sends a single ICMP echo request (`PING_COUNT = 1`; the rulebook needs one logged request). The
referee's server log is the proof, so a missing reply is reported as "sent, no reply" and not
as a success.

**Motion detail.** The S3 puts the camera in pan mode `track`; everything else runs on the
camera, next to the detector. While nothing is detected the camera sweeps `PAN_SCAN_DEG`. Every
pan move increments the camera's *pan epoch*; the detector drops its frame-difference baseline
while the servo moves, waits `MOTION_SETTLE_FRAMES` after it settles, then takes a new baseline
tagged with the epoch, so pan motion is never mistaken for a target. While a target is seen the
camera nudges the pan 10° toward it when |cx| > 0.45, with no round trip to the S3. The S3 only
times the mission: detections with gaps ≤ 1 s for 5 s → COMPLETE.

## 1.5 Threads and timing (S3)

| Task | Core | Period | Does |
|---|---|---|---|
| `control` | 1 | 10 ms (`vTaskDelayUntil`) | E-stop, sensors, missions, drive output, camera config |
| Arduino `loop` | 1 | ~5 ms | WebSocket telemetry (75 ms), serial console |
| AsyncTCP | 0 | event driven | HTTP and WebSocket handlers |
| Wi-Fi / lwIP / ping | 0 | event driven | network stack, `esp_ping` session |

One FreeRTOS mutex (`RoverLock`) guards the `Rover` object. Handlers take it with a 20–50 ms
timeout and answer `503 rover busy` rather than block. The software E-stop first cuts the
outputs **without** the lock (`requestEstop`, which uses an atomic flag) and then latches.

## 1.6 ESP32-CAM pipeline

`vision` task (core 1): grab a 320×240 RGB565 frame (PSRAM, `CAMERA_GRAB_LATEST`) → copy it for
the stream if a viewer is connected → convert to luma → run motion and/or QR if the S3 asked
for them → return the frame. The JPEG encoding for the stream happens in the stream server's
own task, so a slow browser cannot slow the detectors. If the S3 stops sending its config for
1.5 s, both detectors switch off.

# 2. Protocols and API

## 2.1 Dashboard API on the S3 (port 80)

Every response is JSON with `"ok": true|false`. Errors carry `"error"`; command results carry
`"message"` plus the resulting `mode`, `state` and `mission`. CORS is open (`*`) and `OPTIONS`
preflights are answered, so a dashboard served from `http://localhost` can call the rover.

### Rulebook contract (Table 6)

| Method | Path | Body | Result |
|---|---|---|---|
| GET | `/api/status` | — | connection, battery, mode, mission, state, E-stop, motors |
| GET | `/api/sensors` | — | every sensor with its own `t_ms` time stamp |
| POST | `/api/mode` | `{"mode":"teleop"\|"auto"\|"idle"}` + optional `"mission":"traverse"\|"proximity"\|"qr_ping"\|"motion"` | `200` changed, `409` refused (E-stop, safe stop, mission running) |
| POST | `/api/drive` | `{"left":-1..1,"right":-1..1}` | `200` accepted, **`409` "drive rejected: AUTO mode active"** in AUTO, `409` when not in TELEOP |
| POST | `/api/estop` | `{}` | always `200`: outputs cut at once, latch on the next 10 ms tick |
| POST | `/api/estop/reset` | `{}` | `200` → IDLE; `409` while the hardware button is still pressed |
| GET | `/api/camera/stream` | — | `302` redirect to `http://<cam-ip>:81/stream` (MJPEG) |
| WS | `/ws` | see §2.2 | telemetry every 75 ms, events, acks |

`{"mode":"auto"}` without a mission re-arms the last mission. If none was ever chosen it arms
`qr_ping`, which never turns the wheels. Arming never moves anything; `POST /api/mission/start`
does.

### Extra endpoints

| Method | Path | Body | Notes |
|---|---|---|---|
| POST | `/api/mission/start` | `{}` | from AUTO_READY / COMPLETE / FAILED |
| POST | `/api/mission/abort` | `{}` | AUTO_RUNNING → AUTO_FAILED. Rulebook: the task is then scored as teleop |
| POST | `/api/fault/ack` | `{}` | SAFE_STOP → IDLE |
| POST | `/api/servo` | `{"angle":deg}` or `{"pulse_us":us}` | the servo is on the ESP32-CAM: needs the camera link. Angle is clamped to 45–135° and rejected in AUTO; raw pulse only in IDLE (calibration) |
| POST | `/api/task/qr_ping` | `{}` | TELEOP: decode one QR and ping it from the rover (×1 scoring) |
| POST | `/api/motion_detect` | `{"enabled":true\|false}` | TELEOP: run the camera motion detector |
| GET | `/api/events` | — | last 32 events, oldest first |
| GET | `/api/config` | — | firmware limits and which hardware is declared fitted |

### Example `/api/status`

```json
{
  "ok": true, "proto": 1, "fw": "s3-main 0.1.0", "uptime_ms": 125400,
  "connection": {"wifi": "sta", "ssid": "ARENA", "ip": "10.0.0.20", "rssi": -55,
                 "hostname": "urrt-rover", "ws_clients": 1},
  "mode": "teleop", "mode_changed_ms": 98000, "mission": "proximity",
  "state": "TELEOP", "state_changed_ms": 98000,
  "detail": "teleoperation (http); waiting for drive commands", "controls_locked": false,
  "estop": {"active": false, "hw_pressed": false, "hw_sense": "released",
            "source": "none", "reset_allowed": false},
  "battery": {"wired": false, "v": null, "pct": null, "low": null},
  "motor_bus": {"wired": false, "v": null, "present": null},
  "motors": {"armed": true, "left": 0.22, "right": -0.1, "driver": "MD30C", "teleop_max": 0.6},
  "pinmap": "PROVISIONAL", "free_heap": 201234
}
```

Values that are not measured are `null`, never invented: for example the battery while the
divider is not fitted.

### Example `/api/sensors` (abridged)

```json
{
  "ok": true, "t_ms": 125400,
  "sonar": [{"id": "front_center", "fitted": true, "status": "ok", "mm": 312, "t_ms": 125380, "age_ms": 20}, "..."],
  "rear_interlock": {"reverse_allowed": false, "reason": "rear sonar not fitted", "clearance_mm": 200},
  "proximity": {"target_mm": 100, "tol_mm": 20, "front_mm": 310, "in_window": false, "t_ms": 125380},
  "camera": {"link": "ok", "ip": "10.0.0.21", "stream_url": "http://10.0.0.21:81/stream", "fps": 11.8,
             "sensor": "GC2145", "pid": "0x2145", "hb_age_ms": 120, "t_ms": 125280, "rx_ok": 5000, "rx_bad": 2},
  "pan": {"present": true, "mode": "goto", "state": "settled", "angle": 90, "target": 90,
          "pulse_us": 1450, "tracking": false, "soft_min": 45, "soft_max": 135, "t_ms": 125400},
  "qr": {"requested": false, "camera_armed": false, "text": "Ping server: 10.0.0.5", "ip": "10.0.0.5",
         "valid": true, "error": "", "t_ms": 120000, "last_valid_ip": "10.0.0.5", "last_valid_t_ms": 120000},
  "ping": {"state": "reply", "target": "10.0.0.5", "sent": 1, "received": 1, "rtt_ms": 14, "error": "", "t_ms": 120400},
  "motion": {"enabled": true, "camera_armed": true, "detected": true, "cx": 0.35, "cy": -0.1, "area": 0.04,
             "tracking": true, "tracked_ms": 3200, "goal_ms": 5000, "t_ms": 125350},
  "audio": {"online": true, "ip": "10.0.0.22", "mic_level": 42, "clients": 0, "speaker_active": false, "t_ms": 125100}
}
```

All `t_ms` values are milliseconds since the S3 booted, on the same clock as `uptime_ms`.

## 2.2 WebSocket `/ws`

Server → dashboard:

| `type` | When | Payload |
|---|---|---|
| `telemetry` | every 75 ms (`TELEMETRY_PERIOD_MS`) while a client is connected | `seq`, `ev_seq`, `status` (= `/api/status`), `sensors` (= `/api/sensors`) |
| `events` | when a new event is logged, and on connect | `items`: up to 20 `{seq, t_ms, level, text}`, oldest first |
| `ack` | drive rejected (max 2 per s), or E-stop accepted | `{cmd, ok, error?}` |
| `pong` | reply to `ping` | `{c, t_ms}`: `c` echoes the client's value |

Dashboard → server (text frames ≤ 256 bytes):

| `type` | Payload | Effect |
|---|---|---|
| `drive` | `{"left":l,"right":r}` | same rules as `POST /api/drive`; send at 10–20 Hz while driving |
| `estop` | — | software E-stop |
| `ping` | `{"c":number}` | round-trip time measurement |

A slow client makes the server skip a telemetry frame instead of queueing it
(`availableForWriteAll`). The rulebook's polling-time mark uses the median interval, which the
dashboard shows under Connection.

## 2.3 S3 ↔ ESP32-CAM UART link

- 115200 8N1, 3.3 V, S3 GPIO41 (TX) → CAM GPIO13 (RX), CAM GPIO14 (TX) → S3 GPIO42 (RX), common GND.
- One message per line: `<json>*<CRC-8 as 2 hex digits>\n`. CRC-8 uses polynomial 0x07, init
  0x00, over the JSON bytes only. Maximum line length is 240 bytes; longer lines are discarded
  whole (`boardlink::LineAssembler`).
- Every object has `"v": 2` (protocol version; 2 since the pan servo moved to the camera) and `"t"` (type). A version mismatch is logged
  once and the message is ignored.
- **There is no message type that carries a motor or drive command.** Unknown types are
  counted as `rx_bad` and dropped. `cfg` carries only the pan mode for the camera's own pan servo.

| Dir | `t` | Fields | Rate |
|---|---|---|---|
| CAM→S3 | `hb` | `s` seq, `up` uptime ms, `ip`, `sen` sensor name, `pid`, `fps`, `qr`/`mot` detectors armed (0/1), `cam` camera OK, `pm` pan mode applied, `pan` (`absent`/`moving`/`settled`), `ang` angle, `tgt` target, `pul` pulse µs, `trk` (1 = following a motion target) | 5 Hz |
| CAM→S3 | `qr` | `s`, `ok` (0/1), `ip` (valid IPv4 or ""), `txt` (≤ 48 printable chars, `"` and `\` replaced by `?`), `err` (`not_ipv4`, `not_unicast`, `no_ipv4`, `ambiguous`) | per decoded code; the same text is repeated at most once per second |
| CAM→S3 | `mot` | `s`, `det` (0/1), `cx`,`cy` (−1..1, image centre = 0), `a` changed-area fraction, `ep` the camera's pan epoch of the baseline, `base` (1 = a new baseline was taken) | per processed frame while armed |
| S3→CAM | `cfg` | `s`, `qr` (0/1), `mot` (0/1), `pm` pan mode (`hold`/`goto`/`scan`/`track`/`pulse`; unknown = `hold`), `ang` goto angle, `pul` raw pulse µs | on change, else every 200 ms |

Example line: `{"v":2,"t":"cfg","s":12,"qr":1,"mot":0,"pm":"scan","ang":90.0,"pul":0}*6D`

If the S3 stops sending `cfg` for 1.5 s, the camera turns both detectors off and holds the pan.

Pan modes the S3 chooses: `hold` in E-stop, safe stop and missions that do not use the pan;
`scan` while the AUTO `qr_ping` mission searches; `track` during the AUTO `motion` mission (the
camera recentres on the target itself and sweeps while it sees none); `pulse` for calibration in
IDLE; otherwise `goto` the operator's angle.

## 2.4 ESP32-CAM HTTP

| Port | Path | Content |
|---|---|---|
| 80 | `/` | **bench page**: stream, QR / motion / pan controls and live results, for testing the camera without the S3 |
| 80 | `POST /bench?qr=0\|1&mot=0\|1&pm=hold\|goto\|scan\|track&ang=deg` | bench settings. Used only while the S3 is silent, and dropped 3 s after the page stops refreshing them (it refreshes every second) |
| 80 | `/status` | JSON: sensor, PID, fps, frames, link config (`source`: `s3` / `bench` / `none`), last QR (`last_text`, `last_ip`, `last_err`, `age_ms`), pan servo (mode, state, angle, pulse, tracking), QR and motion counters, heap/PSRAM |
| 80 | `/gray.jpg` | the luma image the QR decoder sees (use it to check focus and byte order) |
| 81 | `/stream` | `multipart/x-mixed-replace` MJPEG, ≤ 12 fps, one viewer at a time |

## 2.5 Optional audio board

- **Heartbeat to the S3:** UDP broadcast to port 4211 every 500 ms:
  `{"v":1,"t":"audio_hb","lvl":0..100,"cl":clients,"spk":0|1}`. The S3 uses it for display only.
- **Audio socket:** `ws://<audio-ip>/audio`, at most 2 clients.
  - Client → board, text: `{"listen":true|false}` to opt in to the microphone stream.
  - Client → board, text: `{"vol":0..100}` to dynamically adjust rover speaker output volume.
  - Board → client, binary: 16 kHz, 16-bit signed little-endian mono PCM, 320 samples (20 ms) per frame (DC offset filtered).
  - Client → board, binary: the same format, played on the speaker (1 s ring buffer; excess is dropped, soft-clamped).
- `GET /status` on port 80: mic/speaker init, level, speaker_gain, clients.
- `POST /volume?val=0..100` on port 80: set rover speaker volume.

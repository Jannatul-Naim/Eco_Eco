# 4. Dashboard

The dashboard is a single window in black and gold (`data/index.html`, `style.css`,
`app.js`, under 60 KB, no external libraries, works without internet). The S3 serves it from
LittleFS, so on the arena network you open `http://<rover-ip>/` or `http://urrt-rover.local/`.

## 4.1 Layout

On a laptop or desktop (window at least 1100 × 600 px) everything sits in **one screen with no
page scrolling**, so the driver never has to scroll while driving:

| Area | Contents |
|---|---|
| Top bar | team name; **status strip** with state, mission and the reason (turns **gold** in AUTO = "controls locked", **red** on E-stop with how to clear it); link pill with the median telemetry interval; mode pill; **E-STOP** (also Space, Esc, gamepad B/Circle) |
| Left column | **Control**: mode, **time of the last mode change** (rover clock and local clock), state change time, IDLE / TELEOP, mission picker, ARM AUTO / START / ABORT, RESET E-STOP / ACK FAULT · **Power & E-stop** · **Connection** (rover address, Wi-Fi, telemetry median, round trip, firmware) |
| Centre column | **Camera**: live stream filling the free height, fps + heartbeat badge, link badge, pan slider ◀ ▶ CENTER, pan mode · state (the servo is on the camera, so the pan needs the camera link), sensor, camera IP. The picture comes straight from the camera over Wi-Fi and stays up through short link blips; it needs the camera's IP, which the S3 learns over the link · **Drive**: hold-to-drive pad, commanded and actual left/right output, max-command and deadzone sliders, gamepad status, ENABLE / DISARM, left-stick or tank |
| Right column | **Sonar** (four tiles + 100 ± 20 mm window + reverse interlock) · **QR & ping** · **Motion** (target position, 5 s progress) · **Audio** (optional) · **Events** |

The side columns scroll on their own if a short screen cannot hold them all. The camera, drive
controls, mode buttons and E-STOP never move. On a tablet the camera and drive go on top with the
other two columns below; on a phone everything stacks in one scrolling column, camera and drive first.

Every sensor value shows a rover time stamp (`T+mm:ss.s`) and its age, as the rulebook asks.

Controls follow the firmware state. In AUTO the drive pad, gamepad and pan are disabled and the
status strip says so. The firmware rejects drive commands in AUTO even if someone bypasses the
page: `POST /api/drive` returns `409`.

## 4.2 Driving

- **Pad / keyboard:** hold to drive, release to stop. The page sends commands at 20 Hz; the
  rover stops by itself if they pause for 300 ms.
- **Gamepad** (Xbox or PlayStation style, connected to the PC):
  1. Plug it in and press any button, so the browser exposes it.
  2. Switch the rover to TELEOP and press **ENABLE GAMEPAD**.
  3. Hold **RT / R2** (the deadman) and steer with the left stick (arcade) or both sticks (tank).
  4. Releasing RT/R2 stops the rover and leaves the gamepad armed. Any of these **disarms** it
     until you press ENABLE again: controller disconnected, the dashboard tab loses focus or is
     hidden, E-stop or safe stop, AUTO mode, telemetry stale for 1 s.
  5. **B / Circle** triggers the software E-stop while the tab has focus.
- Mapping depends on the browser reporting the **standard** gamepad layout. The Drive panel shows
  the mapping; if it is not `standard`, ENABLE stays disabled. Chrome and Edge report standard
  mapping for most Xbox and DualShock/DualSense pads over USB. Bluetooth and Firefox vary.
- The tab must stay **focused**. Browsers stop delivering gamepad input to background tabs, and
  the page disarms on blur anyway.

## 4.3 Serving the dashboard from the laptop (needed for talk-back audio)

Browsers only allow microphone access on a **secure context**: `https://` or `http://localhost`.
A page served by the rover at `http://192.168.x.x` is not secure, so HOLD TO TALK is refused
there. Listening still works. To talk:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory data
```

Then open `http://localhost:8000/?rover=<rover-ip>` (or type the rover IP in the Connection
panel; it is remembered). The page then calls the rover's API, WebSocket, camera and audio board
across origins; the S3 sends the CORS headers that allow this. Use Chrome or Edge; mic capture
was not tested in other browsers.

## 4.4 Dashboard Review checklist (rulebook Table 7)

| Mark | How to show it |
|---|---|
| Polling time (10) | Connection → "Telemetry median" should read about 75 ms (≤ 100 ms = full marks) |
| Correct endpoints (10) | `curl` each endpoint in [02_protocol_api.md](02_protocol_api.md) §2.1 |
| Socket used (10) | the link pill says "live" and the events update without reloading |
| Intuitive UI/UX (10) | one page, mode and E-stop always visible, controls grey out when not allowed |
| Repo and commits (10) | README, docs, schematic, regular commits from several members |
| Mode switch + control (20) | TELEOP → drive → ARM AUTO (controls lock) → START → ABORT |
| All sensors (15) | every fitted sensor shows live with a time stamp |
| Camera (10) | stream visible with the fps badge |
| E-stop (5) | E-STOP stops the rover, the banner shows, RESET clears it |

Note: only **one** browser can watch the camera stream at a time (ESP32-CAM limit). Close other
viewers before the review.

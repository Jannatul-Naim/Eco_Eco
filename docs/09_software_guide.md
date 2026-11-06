# 9. Software guide: how the code works, from the beginning

This is the one page to read before you touch the code. It explains what each board does, how
to build and upload, every PlatformIO command the project uses, how the firmware works inside,
where to change things, and the problems the team has already hit. Deeper detail lives in the
other docs; links are at the end.

Every command on this page was run on the team's Linux PC on 6 Nov 2026 and worked, except where
a section says otherwise.

## 9.1 The big picture

The rover has three boards. Each one has its own folder and its own firmware.

| Board | Folder | Its job | Talks to |
|---|---|---|---|
| **ESP32-S3** (main) | `src/s3/` | Drives the motors, reads the E-stop and sonars, runs the missions, serves the dashboard | Laptop over Wi-Fi; camera over 3 wires |
| **ESP32-CAM** | `src/cam/` | Video stream, QR decoding, motion detection, pan servo | S3 over 3 wires; laptop over Wi-Fi (video only) |
| **ESP32 DevKit** (optional) | `src/audio/` | Two-way audio: microphone and speaker | Laptop over Wi-Fi; sends a small status message to the S3 |
| Laptop | `data/` | The dashboard web page, served by the S3 | Everything above |

```
                    Wi-Fi                          3 wires (UART, 115200)
   Laptop browser <-------> ESP32-S3  <--------------------------------> ESP32-CAM
   (dashboard)     /ws, /api   |   GPIO41 TX ---> IO13 RX                 |  pan servo on IO2
         ^                     |   GPIO42 RX <--- IO14 TX                 |
         |                     |   GND ---------- GND                     |
         |  video :81/stream   +--> motor drivers, sonars, E-stop sense   |
         +-------------------------------------------------------------- +
         |  audio ws://.../audio
         +------> ESP32 DevKit (optional) ---- UDP status heartbeat ----> ESP32-S3
```

Three rules hold everywhere in the code:

1. **Only the S3 can move the wheels.** The camera and audio boards have no message that can
   reach the motors. The link protocol has no drive command at all.
2. **The hardware E-stop does not depend on any code.** The switch cuts the contactor coil
   directly. The firmware only *reads* a second pole of the switch to show and latch the state.
3. **When in doubt, stop.** Stale sensors, a lost link, a missing wire or a slow loop all end in
   zero motor output, never in "keep going".

## 9.2 Words used on this page

| Word | Meaning here |
|---|---|
| Firmware | The program that runs on a board. Built on the PC, then uploaded over USB or Wi-Fi |
| PlatformIO (pio) | The build tool. It downloads the compiler and libraries and talks to the boards |
| Environment (env) | One build recipe in `platformio.ini`, for example `s3_main` or `cam` |
| Upload / flash | Copy the built firmware onto the board |
| Serial monitor | A text window that shows what the board prints, and lets you type console commands |
| LittleFS | A small file system on the S3's flash. The dashboard files (`data/`) live there |
| OTA | Over-the-air: uploading firmware over Wi-Fi instead of USB. The camera supports it |
| STA / AP | Wi-Fi station (joins a network) / access point (makes its own network) |
| mDNS | Lets you use `urrt-rover.local` instead of an IP address |
| WebSocket | A connection that stays open, so the S3 can push telemetry every 75 ms |
| UART | A simple serial wire link. The S3 and the camera talk over one |
| PWM | Pulses that set motor power (20 kHz) or servo angle (50 Hz) |
| FreeRTOS task | A thread on the ESP32. The S3 runs a separate task for the control loop |
| Watchdog | A timer that restarts the chip if the code stops checking in |
| ISR | Interrupt handler. The sonar uses one to time the echo pulse |
| E-stop latch | Once in ESTOP, the rover stays there until someone resets it on purpose |

## 9.3 Set up a PC (once)

### Install PlatformIO Core 6

Ubuntu's `apt install platformio` gives version 4.3.4, which **cannot build this project**.
Install the official version instead:

```bash
curl -fsSL -o /tmp/get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
```

```bash
python3 /tmp/get-platformio.py
```

Or install the PlatformIO extension in VS Code, which installs the same thing.

On Linux, run every `pio` command through **`tools/pio.sh`**. It finds the right PlatformIO and
refuses the broken 4.3.4. On Windows and macOS, type `pio` instead of `tools/pio.sh`, or use the
VS Code buttons.

Check it:

```bash
tools/pio.sh --version
```

Expected: `PlatformIO Core, version 6.x`. The team PC has 6.2.0.

### Get the code

```bash
git clone https://github.com/Jannatul-Naim/Eco_Eco.git
```

```bash
cd Eco_Eco
```

### Serial port permission (Linux only)

If uploads fail with "Permission denied" on `/dev/ttyACM0` or `/dev/ttyUSB0`, add yourself to the
`dialout` group, then log out and back in:

```bash
sudo usermod -aG dialout $USER
```

### Wi-Fi settings (secrets)

Each board reads its Wi-Fi name and password from a `secrets.h` file that is **never committed**
(it is in `.gitignore`). Make one per board from the example:

```bash
cp src/s3/secrets.example.h src/s3/secrets.h
```

```bash
cp src/cam/secrets.example.h src/cam/secrets.h
```

```bash
cp src/audio/secrets.example.h src/audio/secrets.h
```

Then edit each `secrets.h`: put the lab or arena network in `WIFI_STA_SSID` and
`WIFI_STA_PASSWORD`. For the camera, also set `CAM_OTA_PASSWORD` (8 or more characters) so you can
update it over Wi-Fi later. Without `secrets.h` the build still works but prints a warning, and the
board falls back to the example values.

Never paste a real password into a document, a chat or a commit.

### First build (no board needed)

```bash
tools/pio.sh run -e s3_main
```

The first build downloads the ESP32 toolchain and libraries, which takes a few minutes. Later
builds take seconds. Expected end: `[SUCCESS]`.

## 9.4 Every PlatformIO command you need

### The environments

| Env | Board | Upload over | Notes |
|---|---|---|---|
| `s3_main` | ESP32-S3 | USB, the "COM"/UART port (CH343, `/dev/ttyACM0` on the team PC) | The normal one. Console on this port |
| `s3_main_nativeusb` | ESP32-S3 | USB, the native "USB" port | Same firmware, console on the native USB port |
| `cam` | ESP32-CAM | USB through the ESP32-CAM-MB board (`/dev/ttyUSB0`) | First flash, or if OTA is broken |
| `cam_ota` | ESP32-CAM | Wi-Fi | Use `tools/cam_ota.sh`, not this env directly |
| `audio` | ESP32 DevKit | USB (CP2102, `/dev/ttyUSB1` on the team PC) | Optional board |
| `native` | the PC itself | — | Unit tests of the shared code |

Port names are **examples**: yours may differ. List them with `tools/pio.sh device list`. On
Windows they look like `COM5`, on macOS like `/dev/cu.usbserial-…`.

### Build, upload, monitor

| What | Command |
|---|---|
| Build one board | `tools/pio.sh run -e s3_main` |
| Build the three boards | `tools/pio.sh run` (default: `s3_main`, `cam`, `audio`) |
| Upload firmware | `tools/pio.sh run -e s3_main -t upload --upload-port /dev/ttyACM0` |
| Upload the dashboard files (S3 only) | `tools/pio.sh run -e s3_main -t uploadfs --upload-port /dev/ttyACM0` |
| Open the serial monitor | `tools/pio.sh device monitor -e s3_main -p /dev/ttyACM0` |
| Leave the monitor | Ctrl+C |
| List serial ports | `tools/pio.sh device list` |
| Update the camera over Wi-Fi | `tools/cam_ota.sh 192.168.0.207` (or no IP: tries `urrt-cam.local`) |
| Run the unit tests | `tools/pio.sh test -e native` |
| Delete build output | `tools/pio.sh run -e s3_main -t clean` |
| Wipe the whole flash | `tools/pio.sh run -e s3_main -t erase --upload-port /dev/ttyACM0` |
| Show the project settings | `tools/pio.sh project config` |
| More detail when something fails | add `-v` to any `run` command |

Notes:

- **`upload` and `uploadfs` are separate.** `upload` writes the program. `uploadfs` writes the
  `data/` folder (dashboard) into LittleFS. After changing anything in `data/`, run `uploadfs`. After
  changing anything in `src/`, run `upload`. A wipe (`erase`) removes both.
- **Close the serial monitor before uploading.** Only one program can use a port at a time.
- `device monitor -e s3_main` picks up the speed (115200) and two filters from `platformio.ini`:
  timestamps, and an **exception decoder** that turns a crash backtrace into file names and line
  numbers. It only decodes correctly if the board runs the build that is on your PC.
- **On the team's S3, opening the serial port restarts the board.** That is normal for this board,
  but it means a monitor started *after* a problem cannot show it. Start the monitor first if you
  are chasing a crash.

### Uploading to each board

| Board | How to get it into upload mode |
|---|---|
| ESP32-S3 | Nothing to do: the CH343 port resets it automatically. Back online about 3 s after upload |
| ESP32-CAM on the MB board | The team's MB board has **no auto-reset**. Hold **IO0**, press and release **RST**, release IO0, then upload. Afterwards press **RST** alone to run the new code |
| ESP32-CAM over Wi-Fi | Needs a firmware with OTA already on it and `CAM_OTA_PASSWORD` set. Run `tools/cam_ota.sh <camera-ip>` |
| ESP32 DevKit | Usually automatic. If it says "Failed to connect", hold **BOOT** while the upload starts |

### A first full setup, in order

1. S3: `upload`, then `uploadfs`, then open the monitor and type `status`.
2. Camera: on the MB board, IO0 + RST, `tools/pio.sh run -e cam -t upload --upload-port /dev/ttyUSB0`,
   then RST. From now on, update it with `tools/cam_ota.sh`.
3. Audio (optional): `tools/pio.sh run -e audio -t upload --upload-port /dev/ttyUSB1`.
4. Open `http://urrt-rover.local/` or the S3's IP address in Chrome or Edge.

## 9.5 Where everything is

```
Eco_Eco/
├── platformio.ini        build recipes (environments), library versions, board settings
├── src/
│   ├── s3/               main controller firmware (section 9.6)
│   ├── cam/              camera firmware (section 9.7)
│   └── audio/            optional audio firmware (section 9.9)
├── lib/
│   ├── rover_common/     code shared by S3 and camera: link framing, IPv4 check (section 9.8)
│   └── quirc/            QR decoder library (vendored, ISC licence)
├── data/                 dashboard: index.html, app.js, style.css (section 9.10)
├── test/test_common/     unit tests for lib/rover_common, run on the PC
├── tools/
│   ├── pio.sh            runs the right PlatformIO
│   ├── cam_ota.sh        camera update over Wi-Fi
│   ├── serve_dashboard.js   serves data/ from the laptop on port 8000 (Node.js)
│   └── make_electrical_pdf.py   builds the electrical PDF from the pin config
├── audio_comm/           separate WebRTC audio system for a laptop or phone (Node.js, section 9.9)
├── docs/                 all documentation, including the electrical PDF
└── Project Guide Line/   the rulebooks and the team's original notes
```

Configuration files you are expected to edit (code reads these; change values, not logic):

| File | What it sets |
|---|---|
| `src/s3/config_pins.h` | Every S3 GPIO |
| `src/s3/config_hardware.h` | What is fitted: sonars, motor driver type, motor invert, E-stop sense, voltage dividers |
| `src/s3/config_tuning.h` | Timing, speed limits, mission parameters |
| `src/s3/config_network.h` | Host name, ports, bench access point on/off |
| `src/cam/config_cam.h` | Camera pins, image settings, QR and motion thresholds, servo calibration |
| `src/audio/config_audio.h` | Audio pins, sample rate, volume |
| `src/*/secrets.h` | Wi-Fi names and passwords (never committed) |

## 9.6 How the S3 firmware works

### Start-up (`src/s3/main.cpp`, `setup()`)

1. Open the serial port at 115200 and print the banner (firmware version, reset reason, chip, MAC).
2. Create the rover lock, then `g_rover.begin()`: set up motor outputs (all off), sonars, E-stop
   input, voltage sensing, camera link.
3. Mount LittleFS. If it fails, an event says to run `uploadfs`, and the page at `/` explains it.
4. `netBegin()`: join the Wi-Fi in `secrets.h` for up to 15 s. If that fails and
   `WIFI_SOFTAP_FALLBACK` is true, open the bench access point instead. Start mDNS as
   `urrt-rover.local`.
5. `webBegin()`: start the web server, API and WebSocket.
6. Start the **control task** with a 3 s watchdog.

The state starts at BOOT, then IDLE with motor outputs disabled. If the E-stop sense wire is open,
it goes straight to ESTOP. That is the fail-safe.

### Two loops run at the same time

| Loop | Where | How often | What it does |
|---|---|---|---|
| Control task | core 1, priority 5 | every 10 ms | `g_rover.tick()`: read inputs, run the state machine, set motor outputs |
| Arduino `loop()` | the other task | about every 5 ms | `webLoop()` (telemetry to the dashboard) and `consoleLoop()` (serial commands) |

Both touch the same `Rover` object, so every access goes through **`RoverLock`**, a mutex. The web
server and console take the lock for a moment, copy what they need into a `RoverSnapshot`, and
let go. They never hold it while sending data.

Two guards watch the control task:

- If it cannot get the lock for **200 ms** (20 ticks in a row), it requests an E-stop.
- If it stops running for **3 s**, the task watchdog restarts the chip. Outputs come back up off.

### One control tick (`Rover::tick()` in `rover.cpp`)

1. Read the E-stop input, the voltage sense, the sonars, and any messages from the camera.
2. Latch ESTOP if a software E-stop was requested, if the hardware switch is OFF (or its wire is
   open), or if motor power disappeared while armed.
3. Depending on the state:
   - **TELEOP:** use the last drive command if it is less than 300 ms old, otherwise stop.
   - **AUTO_RUNNING / AUTO_BLOCKED:** run the mission's step function.
   - Anything else: target zero.
4. `applyDrive()`: apply caps (60 % in teleop, the 12 V cap from the battery voltage), ramp
   limits (0 to full in 400 ms, faster when slowing), and the rear-reverse interlock, then write the
   PWM.
5. Tell the camera what to do: which detectors to run and which pan mode to use.

`rover.cpp` is the **only** code that decides motor output.

### States

| State | Meaning | How you leave it |
|---|---|---|
| BOOT | Starting up | Automatic |
| IDLE | Outputs off | Choose TELEOP or AUTO |
| TELEOP | Following the operator's drive commands | IDLE, AUTO, E-stop |
| AUTO_READY | Mission chosen, controls locked, waiting | START, or back to IDLE |
| AUTO_RUNNING | Mission running on the rover | Finishes, ABORT, E-stop, fault |
| AUTO_BLOCKED | Traverse paused: something in front | Path clears, or FAILED after 3 s |
| AUTO_COMPLETE / AUTO_FAILED | Mission ended | START again, or change mode |
| ESTOP | Latched stop | Switch the E-stop ON, then RESET E-STOP (dashboard, `reset`, or `POST /api/estop/reset`) |
| SAFE_STOP | A fault, such as the camera link lost during a mission | ACK FAULT, or mode IDLE |

### Modes and missions

`POST /api/mode` with `{"mode":"auto","mission":"qr_ping"}` (or the dashboard) arms a mission.
Arming never moves anything; **START** does.

| Mission | What it does | Ends as | Needs before START |
|---|---|---|---|
| `traverse` | Drives forward at 35 %, steers away from side sonars, pauses if something is within 250 mm in front | Always FAILED: no encoder or lane-end sensor can prove it arrived | Front sonar fitted with a fresh reading |
| `proximity` | Drives to 100 ± 20 mm from the wall, slowing as it gets close | COMPLETE after 5 steady readings in the window, FAILED after 20 s | Front sonar fitted with a fresh reading |
| `qr_ping` | Camera sweeps; on a valid IPv4 QR code the S3 checks the address again and sends one ping | COMPLETE on a ping reply, FAILED on no reply or 30 s without a QR | Camera link up |
| `motion` | Camera tracks a moving target with the pan servo | COMPLETE after 5 s of tracking (gaps under 1 s allowed), FAILED after 60 s | Camera link up |

In AUTO, the laptop's drive and pan commands are refused; only E-stop and ABORT work.

### Safety layers in the firmware

| Layer | What happens |
|---|---|
| Hardware E-stop | Cuts motor power in hardware. Firmware sees it on GPIO21 and latches ESTOP |
| Open sense wire | Treated exactly like the E-stop being OFF |
| Software E-stop | Dashboard button, Space/Esc, gamepad B, console `estop`: cuts PWM at once and latches |
| Teleop timeout | No fresh drive command for 300 ms: stop |
| Duty caps | 60 % in teleop; 0.95 when the battery voltage is not measured |
| Rear interlock | Reverse refused unless the rear sonar sees at least 200 mm. No rear sonar means no reverse |
| Starved loop | 200 ms without a control tick: E-stop |
| Task watchdog | 3 s hang: chip restarts with outputs off |
| Lost camera link | During a camera mission: SAFE_STOP |
| Web load | Under memory pressure the web server answers 503 instead of crashing; the E-stop request is never refused |

### The files in `src/s3/`

| File | What is in it |
|---|---|
| `main.cpp` | `setup()`, the control task, `loop()` |
| `rover.h`, `rover.cpp` | The state machine, missions, drive shaping. The heart of the rover |
| `rover_types.h` | States, modes, missions and their names |
| `motors.*` | PWM output for MD30C or BTS7960 drivers |
| `sonar.*` | HC-SR04 timing: one sensor per 40 ms slot, echo timed by an interrupt, median of the last 3 |
| `estop_input.*` | Reads the E-stop sense pin. OFF acts at once; ON must be stable for 100 ms |
| `power_sense.*` | Battery and motor-bus voltage, when the dividers are fitted |
| `cam_link.*` | The camera UART: sends the configuration, parses heartbeat, QR and motion messages |
| `pinger.*` | Sends the ICMP ping for the QR task |
| `web.*` | Web server, `/api/...`, WebSocket `/ws`, telemetry every 75 ms |
| `net.*` | Wi-Fi join, bench access point, mDNS, the audio board's status heartbeat (UDP 4211) |
| `console.*` | The serial console commands |
| `events.*` | The event log (last 32 events) shown on the dashboard and by `events` |
| `config_*.h`, `secrets.h` | Settings (section 9.5) |

### Sonar measurement

The S3 pulses Trig, then an interrupt records when Echo goes HIGH and when it goes LOW. Distance in
mm = echo time in µs × 346 / 2000, minus the face-to-bumper offset. Only one sensor fires per
40 ms slot, so they cannot hear each other. The front sensor is sampled every other slot. Missions
use the median of the last 3 good readings and ignore any older than 400 ms.

### The web API (S3, port 80)

| Method and path | What it does |
|---|---|
| `GET /` | The dashboard (from LittleFS) |
| `GET /api/status` | State, mode, mission, E-stop, motors, power, connection |
| `GET /api/sensors` | Sonars, camera, pan, QR, ping, motion, audio |
| `GET /api/events` | The event log |
| `GET /api/config` | Firmware version and fixed settings |
| `GET /api/camera/stream` | Redirects to the camera's stream |
| `POST /api/mode` | `{"mode":"idle"\|"teleop"\|"auto","mission":...}` |
| `POST /api/mission/start`, `/api/mission/abort` | Start or abort the armed mission |
| `POST /api/drive` | `{"left":-1..1,"right":-1..1}`, TELEOP only |
| `POST /api/estop`, `/api/estop/reset` | Software E-stop, and reset after the switch is ON |
| `POST /api/fault/ack` | Acknowledge a SAFE_STOP |
| `POST /api/servo` | `{"angle":deg}` or `{"pulse_us":us}` (pulse only in IDLE, for calibration) |
| `POST /api/task/qr_ping` | Teleop QR decode and ping |
| `POST /api/motion_detect` | `{"enabled":true\|false}`, teleop motion detection |
| `WS /ws` | Telemetry pushed every 75 ms, events, and the page's drive, E-stop and ping messages |

Answers are JSON with `"ok": true|false`. A refusal says why, for example
`409 drive rejected: not in teleoperation`. `503 rover busy, retry` means the lock or memory was
busy for a moment. Full details: [02_protocol_api.md](02_protocol_api.md).

Try it from the PC:

```bash
curl http://urrt-rover.local/api/status
```

```bash
curl -X POST -H 'Content-Type: application/json' -d '{"mode":"idle"}' http://urrt-rover.local/api/mode
```

### Serial console commands

Open the monitor (section 9.4), then type a command and press Enter.

| Command | What it does |
|---|---|
| `help` or `?` | List the commands |
| `info` | Banner: firmware version, reset reason, chip, MAC, flash size |
| `status` | State, mode, E-stop, motors, sonars, camera |
| `sonar` | Sonar readings only |
| `events` | Recent events |
| `wifi` | Wi-Fi mode, network, IP, signal |
| `estop` / `reset` / `ack` | Software E-stop / reset it / acknowledge a fault |
| `mode idle`, `mode teleop`, `mode auto qr_ping` | Change mode (missions: traverse, proximity, qr_ping, motion) |
| `start` / `abort` | Start or abort the armed mission |
| `drive 0.2 0.2 1000` | TELEOP only: left and right duty (−1 to 1) for up to 3000 ms |
| `stop` | Stop driving |
| `pan 90` | Move the camera pan servo (45–135°) |
| `pulse 1500` | Raw servo pulse in µs, IDLE only, for calibration |
| `qr` | TELEOP: decode a QR code and ping it |
| `motion on` / `motion off` | TELEOP: camera motion detection |

## 9.7 How the camera firmware works (`src/cam/`)

| File | What is in it |
|---|---|
| `main.cpp` | Start-up, Wi-Fi, OTA, the two web servers, `loop()` |
| `vision.*` | The vision task: capture, QR decoding, motion detection, frames for the stream |
| `pan.*` | The pan servo: hold, goto, scan, track, pulse |
| `cam_link.*` | The UART to the S3 |
| `bench_page.h` | The test page at `http://<camera-ip>/` |
| `config_cam.h` | Pins and every camera setting |

How it runs:

1. Start-up initialises the camera (320×240, RGB565). The banner prints the sensor
   (`GC2145, PID 0x2145` on the team's board). If it says `CAMERA FAILED`, check the pin map before
   changing anything.
2. The **vision task** (24 KB stack, its own core) loops over frames:
   - **QR**, when the S3 asks: converts to greyscale and runs the quirc decoder. The text must
     contain exactly one usable IPv4 address (`192.168.0.197`, or `Server: 10.0.0.5`). Loopback,
     `0.x` and multicast addresses are rejected. The result goes to the S3, which **checks it
     again** before pinging.
   - **Motion**, when the S3 asks: shrinks the frame to 80×60, compares it with a reference frame,
     and reports the centre of what changed. If more than 35 % changed it assumes the camera itself
     moved or the light jumped, and ignores that frame. After every pan move it waits 2 frames for a
     fresh reference.
3. The **pan servo** follows the mode the S3 sends: `hold`, `goto` (manual angle), `scan` (sweep
   for QR), `track` (follow motion), `pulse` (calibration). It moves at up to 120°/s, stays within
   45–135°, and waits 350 ms to settle, because the servo has no position feedback.
4. `loop()` handles the link: a **heartbeat every 200 ms** (IP, fps, pan state), QR and motion
   results as they happen. If the S3 says nothing for 1.5 s, detectors turn off.
5. Two web servers: port 80 for `/`, `/status`, `/gray.jpg` (what the QR decoder sees) and
   `POST /bench`; port 81 for `/stream` (MJPEG, up to 12 fps, **one viewer at a time**).

**Bench mode:** while the S3 is silent (not connected), the page at `http://<camera-ip>/` can
switch QR, motion and pan on by itself, for testing the camera alone. Its settings lapse after 3 s
without the page, and the S3 always wins once it is connected.

**Wi-Fi:** the camera tries the network in `secrets.h` first, then the S3's bench access point,
10 s each. OTA listens on port 3232 when `CAM_OTA_PASSWORD` is set.

## 9.8 The shared code (`lib/rover_common/`) and the tests

- **`rover_protocol.h`** defines the S3–camera link. Each message is one line of JSON followed by
  `*` and a 2-digit CRC-8, for example `{"v":2,"t":"hb",...}*3F`. A line with a bad CRC or an
  unknown type is dropped and counted as `rx_bad` on the dashboard; a wrong protocol version is
  dropped and logged once. Camera to S3: `hb`
  (heartbeat), `qr`, `mot`. S3 to camera: `cfg` (which detectors, which pan mode). There is no
  drive message. The shared pan limits (45°, 90°, 135°) are here too, so both boards agree.
- **`ipv4.h`** is the strict IPv4 check used by both boards.
- **`test/test_common/`** has 7 unit tests for these two files. They run on the PC:

```bash
tools/pio.sh test -e native
```

Expected: 7 `PASSED`. Run them after any change in `lib/rover_common/`.

## 9.9 Audio: two separate systems

**The audio firmware (`src/audio/`, optional DevKit board):** the INMP441 microphone is read over
I²S at 16 kHz and sent to the dashboard as 20 ms WebSocket frames on `ws://<audio-ip>/audio`. Speech
from the dashboard comes back the same way to the MAX98357A amplifier, which switches off after
400 ms of silence. Every 500 ms it broadcasts a small status message on UDP port 4211, which the S3
shows as "audio online". It has `/status` and `POST /volume`. At most 2 clients can connect. It
cannot send anything to the motors.

**`audio_comm/` (Alif's WebRTC system):** a separate Node.js app for two-way audio between a
control-panel page and a rover page on a laptop or phone, without the DevKit board. Its own
[README](../audio_comm/README.md) explains it. In short:

```bash
cd audio_comm
```

```bash
npm install
```

```bash
npm start
```

Then open `http://localhost:3000/control-panel.html` and `http://localhost:3000/rover.html`. For
another device on the LAN, run `node generate-cert.js` first and use `https://<your-ip>:3443/...`.
Node.js is not installed on the team's main PC, so this part was not run there.

**Talking needs a secure page.** Browsers only allow the microphone on `https://` or
`http://localhost`. The dashboard served by the rover (`http://192.168.x.x`) can listen but not
talk. To talk, serve `data/` from the laptop with either of these:

```bash
python3 -m http.server 8000 --bind 127.0.0.1 --directory data
```

```bash
node tools/serve_dashboard.js
```

Then open `http://localhost:8000/?rover=<rover-ip>`.

## 9.10 The dashboard (`data/`)

| File | What is in it |
|---|---|
| `index.html` | The four-column layout and every control |
| `style.css` | Colours (dark and light theme), layout for small and large screens |
| `app.js` | Connection, telemetry, controls, gamepad, audio, commands |

How it connects: the page opens the WebSocket `/ws` and receives telemetry every 75 ms. If the
socket drops it polls `/api/status` and `/api/sensors` until the socket is back. Commands go as
`POST /api/...`; drive commands go over the socket at 20 Hz. The video comes straight from the
camera (`:81/stream`), not through the S3.

Controls:

| Input | Action |
|---|---|
| Hold the on-screen pad, or W A S D / arrow keys | Drive (TELEOP only). Release stops |
| Space or Esc | Software E-stop |
| Gamepad | Press ENABLE GAMEPAD, then hold **RT/R2** to drive with the left stick (or tank with both). **B/Circle** = E-stop |
| THEME button | Dark or light, remembered in this browser |

The gamepad disarms itself if the tab loses focus, the controller unplugs, the rover leaves
TELEOP, or telemetry is late. Details: [04_dashboard.md](04_dashboard.md).

**After editing anything in `data/`**, upload it, then reload the page with Ctrl+Shift+R:

```bash
tools/pio.sh run -e s3_main -t uploadfs --upload-port /dev/ttyACM0
```

## 9.11 Where to change things

| You want to… | Edit | Then |
|---|---|---|
| Change the Wi-Fi network | `src/*/secrets.h` | Upload that board |
| Move a pin | `src/s3/config_pins.h` (or `config_cam.h`, `config_audio.h`) | Upload; update `docs/05_pin_maps.md`; rebuild the electrical PDF (`python3 tools/make_electrical_pdf.py` stops with an error until its drawings match) |
| Fit another sonar | `SONAR_FITTED` in `config_hardware.h` | Upload |
| Use BTS7960 drivers | `MOTOR_DRIVER_TYPE` in `config_hardware.h` | Upload |
| Fix a motor side spinning backwards | `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT` in `config_hardware.h` | Upload |
| Change teleop top speed | `TELEOP_MAX_DUTY` in `config_tuning.h` | Upload |
| Tune the proximity stop | `PROX_*` in `config_tuning.h` | Upload, test with the wheels raised first |
| Correct sonar distances | `SONAR_FACE_TO_BUMPER_MM`, `SONAR_SCALE` in `config_hardware.h` | Upload, check with a ruler |
| Turn on battery voltage sensing | Solder the divider, then `BATTERY_SENSE_FITTED` | Upload, calibrate `BATTERY_CAL_GAIN` |
| Calibrate the servo | `SERVO_US_AT_0_DEG`, `SERVO_US_AT_180_DEG` in `config_cam.h` (find values with `pulse` in IDLE) | `tools/cam_ota.sh` |
| Flip the camera image | `CAM_VFLIP`, `CAM_HMIRROR` in `config_cam.h` | `tools/cam_ota.sh` |
| Tune motion sensitivity | `MOTION_*` in `config_cam.h` | `tools/cam_ota.sh` |
| Change dashboard text or layout | `data/` | `uploadfs` |
| Add a console command | `execute()` in `src/s3/console.cpp`, and the `help()` text | Upload |
| Add an API route | `webBegin()` in `src/s3/web.cpp`: use `postRoute()` for commands so they run under the lock | Upload; add it to `docs/02_protocol_api.md` |
| Turn off the bench access point for competition | `WIFI_SOFTAP_FALLBACK = false` in `config_network.h` | Upload |

Never put motor logic anywhere except `rover.cpp`, and never add a link message that can drive.

## 9.12 Everyday workflow

1. `git pull` before you start, so you have your teammates' work.
2. Edit, then build: `tools/pio.sh run -e s3_main`.
3. Upload, open the monitor, test on the bench with the wheels raised.
4. Write down what you saw. A test only counts if someone saw it on the real board;
   `docs/08_status.md` keeps the record.
5. Commit **from your own GitHub account**, in small steps with clear messages. The rulebook gives
   marks for regular commits from more than one team member:

```bash
git add src/s3/config_tuning.h
```

```bash
git commit -m "Tuning: lower teleop top speed to 50 %"
```

```bash
git push
```

Never commit a `secrets.h`. `git status` should never list one.

## 9.13 Problems the team has already hit

| Symptom | Cause | Fix |
|---|---|---|
| `pio` crashes on start | Ubuntu's PlatformIO 4.3.4 | Use `tools/pio.sh` (section 9.3) |
| Upload: "Failed to connect", "Wrong boot mode" on the camera | The MB board has no auto-reset | Hold IO0, press RST, release, upload, then RST |
| Upload: "port busy" or "Permission denied" | Monitor still open, or not in `dialout` | Close the monitor; `sudo usermod -aG dialout $USER` |
| The dashboard stopped loading after a reboot | The router gave the S3 a new IP (it moved from .139 to .138) | Use `http://urrt-rover.local/`, or `wifi` in the console, or set a fixed lease in the router |
| Page says "Dashboard files not uploaded" | LittleFS empty or wiped | `uploadfs` |
| Old dashboard after uploading | Browser cache | Ctrl+Shift+R |
| Boots straight into ESTOP | E-stop sense wire open (GPIO21) | Check the switch and wire. On the bench only, with nothing that moves connected, a jumper from GPIO21 to GND stands in for it |
| RESET E-STOP refused | The switch is still OFF or the wire is open | Switch it ON first; check the wire |
| Camera link `never` | Wires 41/42/GND, camera power, or the camera crashed | Check the 3 wires; look at `http://<camera-ip>/status` |
| No video, but the link is fine | The stream already has a viewer | Close the other tab; only one viewer at a time |
| Sonar `no_echo` | Echo wire loose or sensor unpowered | Re-seat the wires |
| Sonar jumps around at long range | Out-of-spec 3.3 V supply, suspected noise | Use 5 V with the divider and capacitors (electrical PDF, Sheet 5) |
| Opening the monitor restarted the S3 | Normal on this board | Start the monitor before the test, not after |
| S3 restarted under heavy web load | Fixed on 2 Nov 2026 (WebSocket queue) | Keep firmware current |
| A crash | Firmware fault | Start the monitor first; the exception decoder shows file and line of the backtrace |

## 9.14 Read next

| For | Read |
|---|---|
| Boards, states, missions in more depth | [01_architecture.md](01_architecture.md) |
| Every API field and link message | [02_protocol_api.md](02_protocol_api.md) |
| Wiring, parts, E-stop circuit | [03_electrical.md](03_electrical.md) and the [electrical PDF](Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf) |
| Using the dashboard | [04_dashboard.md](04_dashboard.md) |
| Every GPIO | [05_pin_maps.md](05_pin_maps.md) |
| Bringing up hardware stage by stage | [06_bringup_guide.md](06_bringup_guide.md) |
| Pass/fail sheet | [07_test_checklist.md](07_test_checklist.md) |
| What is tested and what is not | [08_status.md](08_status.md) |

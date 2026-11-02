# Eco eco — URRT rescue rover

Team **Eco eco**'s rover for the URRT Robo Cup (UIU Rescue Rover Team). This repository holds the
software and autonomy side: firmware for three boards, the operator dashboard, and the
electrical handoff the build needs.

> **Safety first.** The red mushroom E-stop cuts motor power in hardware. Nothing in this
> repository replaces it. Follow [docs/06_bringup_guide.md](docs/06_bringup_guide.md) stage by
> stage. The first stage runs with the motor drivers and motors **disconnected**.

## What runs where

| Board | Folder | Job | Motor authority |
|---|---|---|---|
| ESP32-S3 (main) | `src/s3/` | drive, E-stop latch, 4× sonar, missions, dashboard + API, on-board ICMP ping; picks the camera's pan mode | **the only one** |
| ESP32-CAM | `src/cam/` | MJPEG stream, QR decode + IPv4 check, motion detection, MG996R camera pan (scan + target following) | none (observations + its own pan servo) |
| ESP32 DevKit (optional) | `src/audio/` | two-way audio relay (INMP441 mic, MAX98357A speaker) | none (status heartbeat only) |
| Operator laptop | `data/` (served by the S3) | dashboard, gamepad teleop | rejected while AUTO is active |

Shared code lives in `lib/rover_common/` (UART framing with CRC-8, strict IPv4 validation).
`lib/quirc/` is a vendored copy of the ISC-licensed quirc QR decoder.

## Rulebook coverage (Software & Autonomous Rulebook V1.3)

| Rule / task | Where | Status |
|---|---|---|
| Teleop and autonomous modes, controls locked in AUTO | `src/s3/rover.cpp` `setMode`, `drive` | compiled |
| QR decode → ping from the rover's own computer (×4 in AUTO) | cam `vision.cpp` + S3 `runQrPing`, `pinger.cpp` | compiled |
| Sonar proximity stop at 100 ± 20 mm | S3 `runProximity` | compiled |
| Motion tracking for 5 s | cam `runMotion` + `pan.cpp` (sweep, recentre) + S3 `runMotion` (5 s timer) | compiled |
| Continuous traverse | S3 `runTraverse` | compiled; **can only end as FAILED**: no encoders or lane-end sensor fitted |
| Two-way audio (teleop only) | `src/audio/` + dashboard Audio panel | compiled, optional hardware |
| Colour and shape detection | — | **out of scope by team decision** (it is worth ×4 if added later) |
| Touch target, box smash | — | not implemented (no sensor or actuator for them) |
| Dashboard contract: `/api/status`, `/api/sensors`, `/api/mode`, `/api/drive`, `/api/estop`(+`/reset`), `/api/camera/stream`, `WS /ws` | `src/s3/web.cpp` | compiled |
| Dashboard functions: mode switch with last-change time, control, all sensors with timestamps, camera with fps, E-stop, rover status | `data/` | rendered in a browser with sample data |
| Schematic / wiring diagram and pin map in the repo | [docs/Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf](docs/Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf), [docs/03_electrical.md](docs/03_electrical.md), [docs/05_pin_maps.md](docs/05_pin_maps.md) | drawn, **power side not built yet**; the built circuit must match it (M&E 3.4) |

"Compiled" means the code builds. [docs/08_status.md](docs/08_status.md) lists what has also been
tested on the real boards. Wiring drawings are in [docs/diagrams/](docs/diagrams/).

## Quick start

PlatformIO Core **6.x** is required. Ubuntu's `apt` package (4.3.4) cannot read this
project. `tools/pio.sh` finds a working Core:

```bash
tools/pio.sh run -e s3_main
```

```bash
tools/pio.sh run -e cam
```

```bash
tools/pio.sh test -e native
```

Wi-Fi credentials: copy each `src/<board>/secrets.example.h` to `secrets.h` and fill it in.
`secrets.h` is git-ignored.

Uploading, the dashboard file system and serial monitoring are covered step by step in
[docs/06_bringup_guide.md](docs/06_bringup_guide.md). The serial ports in the docs
(`/dev/ttyACM0`, `/dev/ttyUSB0`, `COM5`) are only examples. Use the port that
`tools/pio.sh device list` reports.

## Documents

1. [Architecture](docs/01_architecture.md): boards, authority, state machine, missions
2. [Protocols and API](docs/02_protocol_api.md): HTTP/WebSocket API, inter-board UART, audio
3. [Electrical handoff](docs/03_electrical.md): BOM, power plan, E-stop circuit, wiring
4. [Dashboard](docs/04_dashboard.md): using it, gamepad, audio, serving it locally
5. [Pin maps](docs/05_pin_maps.md): every GPIO, per board
6. [Bring-up guide](docs/06_bringup_guide.md): staged commissioning with exact commands
7. [Test checklist](docs/07_test_checklist.md): ordered pass/fail sheet
8. [Status and assumptions](docs/08_status.md): what is compiled, tested and still unknown
9. [Electrical schematic and build guide (PDF)](docs/Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf): for the electrical team: full schematic, wire schedule, pin maps, BOM, test sheet

## Repository rules for the Dashboard Review

The rulebook (3.6.3) marks commit quality: small, regular commits with clear messages from more
than one team member. A single large upload before the deadline gets low marks. Commit each
stage as you finish it, from the account of the person who did the work. The repository is
frozen at the M4 deadline.

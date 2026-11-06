# 6. Bring-up guide

You flash and test the boards yourself; nothing here is done remotely. Work through the stages
**in order** and do not skip one. At the end of each stage, send back what the **"Send back"**
line asks for: the real serial log, photo or measurement. The next stage is adjusted to what you
actually saw. "Expected" lists what the firmware is written to print. It is not a recorded
result; none of this has run on your hardware yet.

**Three kinds of success, kept separate:**

1. *Build success*: `pio run` prints `SUCCESS`. This says nothing about hardware.
2. *Upload success*: esptool prints `Hash of data verified` and the board reboots.
3. *Physical test success*: you observed the behaviour on the real rover and wrote it down in
   [07_test_checklist.md](07_test_checklist.md).

## Stage 0: toolchain and build (no hardware)

PlatformIO Core **6.x** is required. `/usr/bin/pio` from Ubuntu's apt (4.3.4) crashes on this
project. Do not delete it; the wrapper skips it:

```bash
tools/pio.sh --version
```

Expected: `PlatformIO Core, version 6.x.x`. If it reports none, install Core 6 with the official
installer (the command is printed by the script).

Build everything and run the host tests:

```bash
tools/pio.sh run -e s3_main -e cam -e audio
```

```bash
tools/pio.sh test -e native
```

Expected: three `SUCCESS` lines and `6 test cases: 6 succeeded`. The `secrets.h missing`
warnings are fine for now. If a build stops with `No module named 'intelhex'`, PlatformIO's own
Python is missing a package:

```bash
~/.platformio/penv/bin/pip install intelhex
```

**Send back:** the summary table from both commands.

## Stage 1: S3 on USB only, motor power disconnected

**Before you start:** battery unplugged. Motor drivers **not** connected to any supply. Motors
not connected to drivers. Nothing connected to the S3 except the USB cable.

1. Photograph both sides of the S3 board and its header silkscreen. Compare it with
   [05_pin_maps.md](05_pin_maps.md) §5.1. If any listed GPIO is missing or labelled
   differently, **stop here** and send the photos.
2. Find the port:

   ```bash
   tools/pio.sh device list
   ```

   `/dev/ttyACM0`, `/dev/ttyUSB0` and `COM5` in this guide are **examples**. Use the port
   listed for your board, and pick the build that matches it:

   | `device list` shows | Port | Build to use |
   |---|---|---|
   | `USB VID:PID=303A:1001`, "USB JTAG/serial debug unit" | the S3's own (native) USB | `s3_main_nativeusb` |
   | a CP210x / CH340 / CH343 bridge (`1a86:55d3` "USB Single Serial" is a CH343 and shows up as `/dev/ttyACM0`) | "COM"/"UART" bridge | `s3_main` |

   Both builds upload over either port. Only the serial log goes to one or the other, so with the
   wrong build the monitor stays blank. The team's S3 is on its CH343 bridge port, so the commands
   below use `s3_main`. Swap in `s3_main_nativeusb` if you move the cable to the native port.
3. Create the S3 secrets (bench AP for now):

   ```bash
   cp src/s3/secrets.example.h src/s3/secrets.h
   ```

   Edit `src/s3/secrets.h` and set `WIFI_AP_PASSWORD` to something 8–63 characters long.
4. Upload the firmware and the dashboard file system:

   ```bash
   tools/pio.sh run -e s3_main -t upload --upload-port /dev/ttyACM0
   ```

   ```bash
   tools/pio.sh run -e s3_main -t uploadfs --upload-port /dev/ttyACM0
   ```

5. Open the serial monitor:

   ```bash
   tools/pio.sh device monitor -e s3_main --port /dev/ttyACM0
   ```

   The board may print its banner before the monitor has opened, so type `info` and press Enter
   to print it again, then `events` for the boot events. Only one program can hold the port: close
   the monitor (Ctrl+C) before the next upload.

**Expected** (from the firmware source):

- a banner with `URRT rover main controller  s3-main 0.1.0`, the chip model (`ESP32-S3`), core
  count, CPU MHz, the MAC (as esptool prints it) and `PSRAM not enabled in this build` (the S3 does
  not use PSRAM even if the module has some)
- `pin map: PROVISIONAL`, `motor driver: MD30C | E-stop sense: fitted | battery sense: not fitted`
- `sonars fitted: FC=1 FL=0 FR=0 REAR=0`
- `motor outputs start DISABLED`
- an event `BOOT -> ESTOP: E-stop pressed (or sense wire open) at boot`. **This is correct**:
  GPIO21 is not wired yet, and an open sense wire counts as pressed (fail-safe).
- a warning that the rear sonar is not fitted, then Wi-Fi: with `WIFI_STA_SSID` set, an event
  `Wi-Fi STA <ssid> ip <address>`; otherwise (or if the join fails) the bench AP `URRT-ROVER`

Type `status` and press Enter, then `sonar`. With nothing wired the front-center sonar must say
`no_echo`. Any distance here is a fault: stop and report it.

**Send back:** the output of `info`, `events` and `status`.

## Stage 2: hardwired E-stop (motors still disconnected)

Build the circuit in [03_electrical.md](03_electrical.md) §3.4 on veroboard: E-stop switch pole A in
the contactor coil path, ARM button, hold path, flyback diode, pole B to S3 GPIO21. Leave the
driver branch fuses **F5/F6 out** and the drivers disconnected.

1. Battery in, E-stop switch ON. Measure the motor bus: **0 V** (contactor open at power-on).
2. Press ARM: the contactor clicks and the motor bus reads the battery voltage.
3. Switch the E-stop OFF: the motor bus drops to **0 V**.
4. Switch it back ON: the motor bus **stays at 0 V**.
5. On the S3 console, with the E-stop switch ON, type `reset`. Expected: `OK (200): E-stop reset; mode idle`.
   Switch it OFF: the event `-> ESTOP: hardware E-stop pressed (or sense wire open)` appears.
   Type `reset` while it is OFF: `REJECTED (409): hardware E-stop still pressed ...`.

**Send back:** the four multimeter readings and the console lines from step 5.

## Stage 3: Wi-Fi, dashboard and voltage sensing

1. Join the `URRT-ROVER` network from the laptop and open `http://192.168.4.1/`. The link pill
   should say `live` and Connection → Telemetry median should be close to 75 ms.
2. (Arena or lab network) Fill in `WIFI_STA_SSID` / `WIFI_STA_PASSWORD` in `src/s3/secrets.h` and
   re-upload. The boot log prints `Wi-Fi STA <ssid> ip <address>`. Use the IP or
   `http://urrt-rover.local/`. Any computer on the same router works, including one on Ethernet.
   On Linux, `getent hosts urrt-rover.local` prints the address. The network must be 2.4 GHz.
   For competition builds set `WIFI_SOFTAP_FALLBACK = false` in
   `src/s3/config_network.h` (rule 1.3.1: no unauthorised transmitters).
3. Optional, recommended: battery divider (100 k / 22 k) on GPIO1. Set `BATTERY_SENSE_FITTED = true`,
   re-upload, compare `status` with a multimeter at the battery, and set
   `BATTERY_CAL_GAIN = multimeter / reported`. Repeat for the motor bus on GPIO2 if fitted.

**Send back:** a dashboard screenshot and, if fitted, the multimeter vs reported voltages.

## Stage 4: sonars, one at a time (including the rear interlock)

For each sensor: wire it with its 1 k / 1.8 k echo divider, set its entry in `SONAR_FITTED`
(`src/s3/config_hardware.h`) to `true`, rebuild and upload, then type `sonar` on the console.

1. **Front-center first.** Hold a flat board square to the sensor at 100, 200 and 500 mm (ruler
   from the bumper edge). Note the reported mm each time.
2. If the sensor face sits behind the bumper, set `SONAR_FACE_TO_BUMPER_MM[0]` to that distance.
   If the readings are consistently off by a ratio, set `SONAR_SCALE[0]`.
3. Cover the sensor or point it at open space: status should become `out_of_range` or
   `no_echo`, never a made-up number. Unplug the Echo wire: the sonar goes stale or reports a fault.
4. **Rear:** with nothing behind, the dashboard Sonar card shows Reverse `allowed`. With a board
   at 150 mm (below `REAR_CLEARANCE_MM` = 200): `blocked: rear obstacle too close`.
5. Front-left and front-right the same way.

**No resistors yet (bench only):** power the sensor from the S3's 3V3 pin and connect Echo straight to
the S3 pin. On 3.3 V, Echo cannot exceed 3.3 V. Some HC-SR04s do not run on 3.3 V and will read
`no_echo`. This only proves the wiring and firmware path: the final test at 5 V needs the divider.
**With VCC on 5 V, never connect Echo without the divider.**

**Send back:** a table of ruler distance vs reported mm for every sensor, plus the step 3 and 4 observations.

## Stage 5: ESP32-CAM and its link to the S3

The camera board runs the stream, QR decoding, motion detection **and the pan servo**. The S3
tells it what to do over a 3-wire serial link, and learns the camera's IP the same way.

1. Copy `src/cam/secrets.example.h` to `src/cam/secrets.h` (same networks as the S3).
2. Put the board in flash mode (GPIO0 to GND, reset), then upload and monitor (its port
   differs from the S3's):

   ```bash
   tools/pio.sh run -e cam -t upload --upload-port /dev/ttyUSB0
   ```

   ```bash
   tools/pio.sh device monitor -e cam --port /dev/ttyUSB0
   ```

   Remove the GPIO0 jumper and reset. A plain FTDI adapter has no reset line to the board, so
   software cannot restart it: press RST yourself. Also, every esptool command leaves the chip
   in esptool's flasher, sometimes at 460800 baud. If the next upload fails with
   `No serial data received`, put it in flash mode again (GPIO0 to GND, press RST).
3. **Expected:** a banner with `PSRAM found`, then either `camera OK: sensor <name>, PID 0x....`
   or `CAMERA FAILED: <reason>`, then `pan servo on GPIO2 ...`. **If the camera failed, or
   the PID is not one you expected, stop and send the log plus a close photo of the camera module
   and its ribbon markings.** Then `Wi-Fi: connected ...` with the stream and status URLs.
4. Wire the link, **only these three wires** (both boards keep their own USB power):

   | ESP32-CAM | S3 |
   |---|---|
   | IO13 (link RX) | GPIO41 (TX) |
   | IO14 (link TX) | GPIO42 (RX) |
   | GND | GND |

   Check both boards' pin labels first; if the S3 has no pins marked 41/42, stop and send a
   photo. Keep the camera's microSD slot empty (it shares IO13/IO14).

   **Bench power:** the ESP32-CAM-MB covers IO13/IO14, so take the camera off it and power it
   by wire: camera **5V** → DevKit **VIN**, plus a camera GND → DevKit GND. The team's S3 board's
   5V pin does **not** output USB power (the camera stayed dark on it). Never use the camera's
   VCC or 3V3 pins as a power input. On the rover the camera gets 5 V from the logic buck.
5. **Expected:** the S3 logs `camera link up (<ip>)`. The dashboard Camera card shows link `ok`,
   the fps and the stream (close any other tab showing the stream: one viewer at a time). The
   pan line shows `goto · settled`.

**Wireless camera updates.** Once a firmware with OTA is on the camera (its boot log says
`OTA: ready on <ip> port 3232`), update it over Wi-Fi with no MB and no buttons:

```bash
tools/cam_ota.sh 192.168.0.207
```

The password comes from `CAM_OTA_PASSWORD` in `src/cam/secrets.h`; without one, OTA stays off.

**Camera alone (bench mode). Before the link is wired, open `http://<cam-ip>/` (the camera's
own page). It shows the stream and lets you switch QR decoding, motion detection and the pan
mode on and off, with live results. The page only works while the S3 is silent: once the link is
up, the camera follows the S3 and the page says so. Closing the page switches everything off
within 3 s. The team's ESP32-CAM-MB has IO0 and RST buttons but no auto-reset: hold IO0 and
press RST before an upload, then press RST alone to run the new firmware.

**Send back:** the camera boot log and the S3 `events` after wiring.

## Stage 6: MG996R pan servo on the ESP32-CAM (motors disconnected)

Wiring ([03_electrical.md](03_electrical.md) §3.7): servo **signal (orange) → ESP32-CAM IO2**,
servo **red → "servo 5 V" buck**, servo **brown → GND**, and that GND tied to the ESP32-CAM GND.
On the bench a separate 5 V supply rated **≥ 3 A** can stand in for the buck (an MG996R stalls at
about 2.5 A). **Never power the servo from the ESP32-CAM's or the DevKit's 5V/VIN pin:** a stalled
MG996R browns the boards out. **Detach the camera
from the bracket for the first test.**

1. Reset the camera with the servo connected. It must boot normally and the servo should go to
   about mid-travel (home 90°) once. If the board no longer boots or the servo twitches hard at
   power-up, stop and report it: IO2 is a strapping pin.
2. In IDLE on the S3 console, type `pulse 1500`. The servo should sit at about mid-travel. Then
   `pulse 1700`: it should move a little and **hold**. If it keeps turning, it is the 360°
   continuous-rotation version of the MG996R, which cannot hold an angle: stop and get the 180°
   type.
3. Step outward slowly: `pulse 1400`, `pulse 1300` … and `pulse 1600`, `pulse 1700` … Stop
   **before** the horn hits a mechanical stop or the servo buzzes against it. Note the last good
   pulse at each end. The firmware never goes beyond 500–2500 µs.
4. Set `SERVO_US_AT_0_DEG` / `SERVO_US_AT_180_DEG` in `src/cam/config_cam.h` from your
   measurements, upload the camera again, then try `pan 45`, `pan 90`, `pan 135` (or the
   dashboard slider).
5. Refit the camera. Move between the limits and check that the ribbon cable cannot snag or
   stretch. Widen `kPanMinDeg` / `kPanMaxDeg` (`lib/rover_common/src/rover_protocol.h`, used by
   both boards) only if it is clear.

**Send back:** the end pulses, the angles you settled on, and whether the cable is clear.

## Stage 7: camera tasks: image, QR, pan-aware motion

1. Point the camera at a scene with detail and open `http://<cam-ip>/gray.jpg`. If the image is
   scrambled noise but the stream looks right, flip `CAM_RGB565_HIGH_BYTE_FIRST`. If upside down
   or mirrored, set `CAM_VFLIP` / `CAM_HMIRROR` (all in `src/cam/config_cam.h`).
2. **QR:** show the camera a QR code holding an IPv4 address (any generator; use the laptop's own
   IP so the ping can succeed). TELEOP → SCAN QR + PING. The camera serial prints
   `QR "..." -> ip ...`, the dashboard shows the text and the IP, and the S3 pings it.
3. **QR search:** AUTO `qr_ping` → START with the code off to one side. The camera sweeps its pan
   (`scan`) until it decodes the code, then the S3 pings.
4. **Pan-aware motion:** TELEOP → DETECT ON. Wave a hand: Target `yes`. Then move the pan with the
   slider while nothing moves in view. There should be **no** detection while the servo moves; the
   camera takes a fresh baseline after it settles.
5. **Tracking:** AUTO `motion` → START. With nothing moving, the camera sweeps. Walk slowly across
   its view: the pan line shows `track · ... · tracking` and the camera turns to follow. If it
   turns **away** from the target, set `MOTION_PAN_DIRECTION = -1` in `src/cam/config_cam.h`.
   Keep moving for 5 s: the mission ends COMPLETE.

**Send back:** a `/gray.jpg` screenshot and the results of steps 2–5.

## Stage 8: motor drivers and motors, wheels raised

**The rover sits on a stand with all four wheels off the ground.** Someone holds a hand over
the E-stop for the whole stage.

1. Check each motor alone on a bench supply (or the driver at low duty) for direction, then
   wire the pairs per [03_electrical.md](03_electrical.md) §3.5. Insert F5/F6. Press ARM.
2. Console:

   ```
   mode teleop
   drive 0.2 0.2 800
   ```

   Both sides should turn **forward** at low speed for 0.8 s and then stop. If one side runs
   backward, set `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT`, rebuild and repeat.
3. `drive 0.2 -0.2 800`: the left side forward, the right side backward (spin right).
4. `drive -0.2 -0.2 800` with no rear sonar fitted: **no** backward motion, and the event
   `reverse blocked: rear sonar not fitted`.
5. Start `drive 0.3 0.3 3000` and press the E-stop mid-run: the wheels stop at once and the S3
   enters ESTOP.
6. Teleop timeout: from the dashboard, hold ▲ and then switch the laptop's Wi-Fi off. The
   wheels stop within about 0.3 s.
7. After 1 minute at `0.3`, touch-check the drivers and motors for heat.

**Send back:** the direction results, any invert changes, and the step 4–6 observations.

## Stage 9 (optional): audio bridge

1. Wire per [03_electrical.md](03_electrical.md) §3.9. Copy `src/audio/secrets.example.h` to `secrets.h`.
2. Upload and monitor:

   ```bash
   tools/pio.sh run -e audio -t upload --upload-port /dev/ttyUSB0
   ```

   ```bash
   tools/pio.sh device monitor -e audio --port /dev/ttyUSB0
   ```

3. **Expected:** `mic I2S0 OK | speaker I2S1 OK`, a Wi-Fi line with `ws://<ip>/audio`, and a
   `mic level` line every 5 s. Speak near the mic; the level should rise. If it stays at 0, set
   `MIC_READ_RIGHT_SLOT = true` in `src/audio/main.cpp`.
4. The dashboard Audio card shows `online`. LISTEN plays the rover mic. For HOLD TO TALK, serve
   the dashboard from localhost ([04_dashboard.md](04_dashboard.md) §4.3).

**Send back:** the boot log and whether listen and talk are understandable at 1 m.

## Stage 10: dashboard and gamepad (wheels still raised)

1. Go through every card in [04_dashboard.md](04_dashboard.md) §4.1.
2. Gamepad: connect it, TELEOP, ENABLE, hold RT/R2, steer. Check that each disarm condition in
   §4.2 works: unplug the controller, click another window, press B, ARM AUTO.
3. With AUTO armed, confirm that the pad, keys and gamepad do nothing, and that this returns 409:

   ```bash
   curl -s -X POST -H 'Content-Type: application/json' -d '{"left":0.3,"right":0.3}' http://<rover-ip>/api/drive
   ```

**Send back:** the browser, controller model, mapping shown, and the step 2–3 results.

## Stage 11: on-board missions

Declare each mission's purpose before starting, as you would to a referee.

1. **QR + ping** (no wheel motion). Stand-in for the referee server: on the laptop, log ICMP echo requests:

   ```bash
   sudo tcpdump -n -i any icmp
   ```

   Make a QR with the laptop's IP. ARM AUTO with "QR + ping", then START. Expected: the pan
   scans, the decode appears, one `ICMP echo request` from the rover's IP shows in tcpdump, and
   the state becomes `AUTO_COMPLETE` (or `AUTO_FAILED ... no reply` if the laptop firewall drops
   replies; the request in the log is what counts).
2. **Motion** (no wheel motion): AUTO "Motion 5 s", START, walk a target slowly across the view.
   Expected: the tracking bar reaches 5.0 s → `AUTO_COMPLETE`.
3. **Proximity**, wheels raised first: hold a board 400 mm in front of the sonar. The wheels turn
   forward. Move the board to 100 mm and the wheels stop. Then on the floor (stage 13).
4. **Traverse**, wheels raised: the wheels turn; a board at 200 mm in front → `AUTO_BLOCKED`, then
   `AUTO_FAILED` after 3 s. Without a lane-end sensor this mission cannot end as COMPLETE (by design).

**Send back:** the event log of each mission and the tcpdump line.

## Stage 12: integrated safety

With the wheels raised, during an AUTO mission:

- [ ] Press the E-stop: everything stops; releasing it restarts nothing.
- [ ] Unplug the camera UART during QR or motion: `SAFE_STOP: camera link lost ...`.
- [ ] Unplug the front sonar Echo during traverse or proximity: `SAFE_STOP: ... stale or faulty`.
- [ ] Close the dashboard: the mission continues (it is autonomous), and the E-stop still works.
- [ ] Send a drive command over HTTP: `409`.
- [ ] Software E-STOP from the dashboard: stops; RESET only after confirming it is safe.

**Send back:** the checklist with what you observed.

## Stage 13: controlled floor run

Only after stages 2, 4 (front-center and rear), 8 and 12 have passed and are recorded.

- Clear, flat area. One person's hand on the E-stop. Start with `Max command` at 30 %.
- Teleop first: forward, stop, turns.
- Then proximity on the floor against a wall, and measure the final gap with a ruler. Tune
  `PROX_MIN_DUTY` if the rover stalls short, or `PROX_GAIN_PER_MM` if it overshoots.
- Write every result in [07_test_checklist.md](07_test_checklist.md) and update
  [08_status.md](08_status.md).

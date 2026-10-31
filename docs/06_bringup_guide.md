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
   listed for your board. Plug into the port labelled "COM"/"UART" (USB-UART bridge).
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

5. Open the serial monitor and press the board's RST button:

   ```bash
   tools/pio.sh device monitor -e s3_main --port /dev/ttyACM0
   ```

**Expected** (from the firmware source):

- a banner with `URRT rover main controller  s3-main 0.1.0`, the chip model (`ESP32-S3`), core
  count, CPU MHz and the eFuse MAC
- `pin map: PROVISIONAL`, `motor driver: MD30C | E-stop sense: fitted | battery sense: not fitted`
- `sonars fitted: FC=1 FL=0 FR=0 REAR=0`
- `motor outputs start DISABLED`
- an event `BOOT -> ESTOP: E-stop pressed (or sense wire open) at boot`. **This is correct**:
  GPIO21 is not wired yet, and an open sense wire counts as pressed (fail-safe).
- a warning that the rear sonar is not fitted, then Wi-Fi falling back to the bench AP
  `URRT-ROVER`

Type `status` and press Enter. If the native-USB port is the only one that shows a log, rebuild
with `-e s3_main_nativeusb` and tell us.

**Send back:** the full boot log and the output of `status`.

## Stage 2: hardwired E-stop (motors still disconnected)

Build the circuit in [03_electrical.md](03_electrical.md) §3.4 on veroboard: E-stop NC #1 in
the contactor coil path, ARM button, hold path, flyback diode, NC #2 to S3 GPIO21. Leave the
driver branch fuses **F5/F6 out** and the drivers disconnected.

1. Battery in, E-stop released. Measure the motor bus: **0 V** (contactor open at power-on).
2. Press ARM: the contactor clicks and the motor bus reads the battery voltage.
3. Press the E-stop: the motor bus drops to **0 V**.
4. Release the E-stop: the motor bus **stays at 0 V**.
5. On the S3 console, with the E-stop released, type `reset`. Expected: `OK (200): E-stop reset; mode idle`.
   Press the E-stop: the event `-> ESTOP: hardware E-stop pressed (or sense wire open)` appears.
   Type `reset` while it is held: `REJECTED (409): hardware E-stop still pressed ...`.

**Send back:** the four multimeter readings and the console lines from step 5.

## Stage 3: Wi-Fi, dashboard and voltage sensing

1. Join the `URRT-ROVER` network from the laptop and open `http://192.168.4.1/`. The link pill
   should say `live` and Connection → Telemetry median should be close to 75 ms.
2. (Arena or lab network) Fill in `WIFI_STA_SSID` / `WIFI_STA_PASSWORD` in `src/s3/secrets.h` and
   re-upload. The boot log prints `Wi-Fi STA <ssid> ip <address>`. Use the IP or
   `http://urrt-rover.local/`. For competition builds set `WIFI_SOFTAP_FALLBACK = false` in
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

**Send back:** a table of ruler distance vs reported mm for every sensor, plus the step 3 and 4 observations.

## Stage 5: S3 detects the PCA9685 on I²C

Wire the PCA9685 per [03_electrical.md](03_electrical.md) §3.7: **VCC from the S3 3V3**, SDA
GPIO8, SCL GPIO9, GND. No servo yet. Console:

```
i2cscan
```

Expected: a device at `0x40` (and possibly `0x70`). If nothing answers, check the wiring and
whether your header really has GPIO8/9 there. If another address appears, set
`PCA9685_I2C_ADDR`.

**Send back:** the `i2cscan` output.

## Stage 6: SG90 travel through the PCA9685 (motors disconnected)

Servo on channel 0, V+ fed from the "servo 5 V" buck on the motor bus (press ARM to power it).
Drive fuses F5/F6 stay out. **Detach the camera from the bracket for the first test.**

1. In IDLE, type `pulse 1500`. The servo should go to about mid-travel.
2. Step outward slowly: `pulse 1400`, `pulse 1300` … and `pulse 1600`, `pulse 1700` … Stop
   **before** the horn hits a mechanical stop or the servo buzzes against it. Note the last good
   pulse at each end. The firmware never goes beyond 500–2500 µs.
3. Set `SERVO_US_AT_0_DEG` / `SERVO_US_AT_180_DEG` from your measurements
   (`src/s3/config_tuning.h`), rebuild and upload, then try `pan 45`, `pan 90`, `pan 135`.
4. Refit the camera. Move between the soft limits and check that the ribbon cable cannot snag or
   stretch. Widen `SERVO_SOFT_MIN_DEG` / `MAX` only if it is clear.

**Send back:** the end pulses, the angles you settled on, and whether the cable is clear.

## Stage 7: ESP32-CAM: stream, QR, pan-aware motion

1. Copy `src/cam/secrets.example.h` to `src/cam/secrets.h` (same networks as the S3).
2. Put the board in flash mode (GPIO0 to GND, reset), then upload and monitor (its port
   differs from the S3's):

   ```bash
   tools/pio.sh run -e cam -t upload --upload-port /dev/ttyUSB0
   ```

   ```bash
   tools/pio.sh device monitor -e cam --port /dev/ttyUSB0
   ```

   Remove the GPIO0 jumper and reset.
3. **Expected:** a banner with `PSRAM found`, then either `camera OK: sensor <name>, PID 0x....`
   or `CAMERA FAILED: <reason>`. **If it failed, or the PID is not one you expected, stop and send
   the log plus a close photo of the camera module and its ribbon markings.** Then
   `Wi-Fi: connected ...` with the stream and status URLs.
4. Open `http://<cam-ip>/gray.jpg`. If the image is scrambled noise but the stream looks right,
   flip `CAM_RGB565_HIGH_BYTE_FIRST`. If upside down or mirrored, set `CAM_VFLIP` / `CAM_HMIRROR`.
5. Wire the UART link (CAM 13/14 ↔ S3 41/42, common GND). The S3 log shows
   `camera link up (<ip>)`; the dashboard Camera card shows link `ok`, the fps and the stream.
6. **QR:** generate a QR code holding an IPv4 address (`qrencode -t ANSIUTF8 "192.168.4.2"` in a
   terminal, or any generator) and show it to the camera. TELEOP → SCAN QR + PING. The camera
   serial prints `QR "..." -> ip ...` and the dashboard shows the text and the IP.
7. **Pan-aware motion:** TELEOP → DETECTOR ON. Wave a hand: Target `yes`. Then move the pan
   with the slider while nothing moves in view. There should be **no** detection while the servo
   moves, and the camera takes a fresh baseline after it settles.
8. Check `MOTION_PAN_DIRECTION`: put the target on the **right** of the image during the AUTO
   motion mission (stage 11). The pan should turn toward it. If it turns away, set the constant to `-1`.

**Send back:** the camera boot log, `/status` JSON, a `/gray.jpg` screenshot and the results of steps 6 and 7.

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

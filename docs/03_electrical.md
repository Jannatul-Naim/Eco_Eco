# 3. Electrical handoff

This document is for the team member who builds the circuit. It covers every part the firmware
assumes, how each one is powered and wired, and which items still need confirming.

**Labels used below**

| Label | Meaning |
|---|---|
| **CONFIRMED** | the team has stated it owns this or has decided on it |
| **RECOMMENDED** | proposed here, with the reason given; replace it if you have a better verified part |
| **UNKNOWN** | needed but not identified yet; do not wire anything that depends on it until it is checked |

Rulebook constraints that shape this design (Mechanical & Electrical Rulebook V1.3):

- 1.2: a red **mushroom-head** E-stop that removes power from all actuators **in hardware**. Releasing it must not restart motion.
- 3.1: circuits on **soldered veroboard/perfboard or PCB**. No breadboards anywhere on the rover.
- 3.2: **XT30/XT60/XT90 or Deans** on every power connection between the battery, E-stop, distribution and drivers. Red wire for every positive line, black for ground, any other colour for signals.
- 3.3: nominal battery ≤ 24 V, fused, secured, and carried and charged in a fire-resistant bag.
- 3.4: a schematic showing every pin, committed to GitHub with the pin map. **The text diagrams here are a starting point; the team must still draw the schematic** (KiCad, EasyEDA, Fritzing or neat pen and paper).
- Class A (4WD, no flippers) is assumed, since no flipper hardware exists.

## 3.1 Bill of materials

### Drive

| # | Part | Qty | Label | Notes |
|---|---|---|---|---|
| D1 | 12 V brushed gearmotor, **Pololu #4744** (37D × 54L mm, 70:1, helical pinion, **no encoder**) | 4 | RECOMMENDED (baseline); "four drive motors" CONFIRMED | Pololu data at 12 V: 150 RPM, 0.2 A no-load, **5.5 A stall** (extrapolated), keep continuous load ≤ 10 kg·cm. The encoder version is **#4754**: buy that if you want the traverse task to be able to finish as COMPLETE. |
| D2 | Cytron **MD30C** single-channel driver (PWM + DIR) | 2 | RECOMMENDED; "two motor drivers" CONFIRMED, model UNKNOWN | 5–30 V, 30 A continuous, 80 A for ≤ 1 s, PWM ≤ 20 kHz, logic high 3–5.5 V (3.3 V is fine). One driver per side, with that side's two motors in parallel. |
| D2-alt | BTS7960 / IBT-2 dual half-bridge module | 2 | alternative (listed in the team's component notes) | Firmware supports it: set `MOTOR_DRIVER_TYPE` to `DRIVER_BTS7960` in `src/s3/config_hardware.h`. See §3.5 for the logic-voltage caveat. |

**Driver sizing with real data.** The per-side load is 2 × 5.5 A = **11 A at stall**, and both
sides together 22 A. MD30C: 30 A continuous is 2.7× the per-side stall, so a stalled side does
not exceed its rating; the branch fuses (§3.3) protect the wiring. A "10 A dual H-bridge" (the
other option in the team's notes) is **below** the 11 A per-side stall of these motors. Do not
use it with this motor choice.

**3S battery vs 12 V motors.** A full 3S pack is 12.6 V, 5 % above the motor rating. The
firmware caps PWM duty at 12.0 V / V<sub>battery</sub> once the battery divider is fitted and
calibrated (`BATTERY_SENSE_FITTED`), and at a fixed 0.95 (= 12.0 / 12.6) otherwise. Fit the
divider so the cap follows the real voltage.

### Power and safety

| # | Part | Qty | Label | Notes |
|---|---|---|---|---|
| P1 | 3S Li-ion pack (11.1 V nominal, 12.6 V full) **with BMS**, XT60 lead | 1 | RECOMMENDED | e.g. 3S2P of high-drain 18650 cells (≥ 15 A continuous each) ≈ 5 Ah. **BMS continuous rating ≥ 25 A.** Typical drive current is about 2–6 A, stall worst case 22 A. |
| P2 | Matching 3S Li-ion CC/CV charger, 12.6 V, 1–2 A | 1 | RECOMMENDED | never a generic 12 V adapter |
| P3 | Fire-resistant battery bag (LiPo-safe) | 1 | RECOMMENDED | rulebook 3.3.2: carry and charge in it |
| P4 | Red **mushroom-head latching E-stop** (twist to release), 22 mm, with **two NC contact blocks** | 1 | RECOMMENDED (required by rule 1.2.1) | NC #1 breaks the contactor coil, NC #2 tells the S3 (`PIN_ESTOP_SENSE`). The KCD11 rocker in the team's notes is **not** acceptable as the E-stop. |
| P5 | DC contactor / power relay, **12 V coil**, contacts rated **≥ 40 A at ≥ 14 V DC** | 1 | RECOMMENDED | check the DC (not AC) contact rating on the datasheet. A sealed automotive 40 A relay with a DC rating is acceptable. |
| P6 | Momentary NO push button, green, "ARM" | 1 | RECOMMENDED | the deliberate restart action (rule 1.2.4) |
| P7 | Flyback diode across the contactor coil (1N4007) | 1 | RECOMMENDED | cathode to coil +. Omit if the relay has a built-in diode (check polarity). |
| P8 | Inline blade-fuse holders + fuses: **25 A** main, **15 A** × 2 (driver branches), **3 A** logic, **3 A** servo supply, **1 A** × 2 (coil/ARM and hold path) | 1 set | RECOMMENDED | slow-blow automotive blade (ATO/ATC). Main fuse as close to the battery as possible. |
| P9 | XT60 pair (battery), XT30 pairs (driver branches, buck inputs) | 1 + 4 | RECOMMENDED | rulebook 3.2.1 |
| P10 | 5 V buck converter, ≥ 3 A continuous, 6–24 V in ("logic 5 V") | 1 | RECOMMENDED | S3, ESP32-CAM, sonars, optional audio board |
| P11 | 5 V buck converter, ≥ 3 A continuous ("servo 5 V") | 1 | RECOMMENDED | MG996R only (stall ≈ 2.5 A); fed from the **motor bus** so the E-stop also stops the servo |
| P12 | Wire: 14 AWG silicone red/black (battery to drivers), 18 AWG (drivers to motors), 22 AWG red/black (5 V rails), 24–26 AWG coloured (signals) | — | RECOMMENDED | rulebook 3.2.3 colours |
| P13 | 12 V DC 4-inch case fan | 1 | listed in the team's notes; current UNKNOWN | on the logic branch (after the 3 A fuse); check its current on the label |

### Control, sensing, camera

| # | Part | Qty | Label | Notes |
|---|---|---|---|---|
| C1 | ESP32-S3 dev board, marked "ESP32 23 2022 V1.3" | 1 | CONFIRMED (owned); ESP32-S3-N16R8, DevKitC-1 header (photo) | treated as DevKitC-1 compatible, see [05_pin_maps.md](05_pin_maps.md). Photograph both sides and the header silkscreen before wiring. |
| C2 | ESP32-CAM assembly with "RHYX M21-45" camera | 1 | CONFIRMED (owned); **sensor UNKNOWN** | AI-Thinker layout assumed; GC2145-family RGB565 is only a hypothesis. The firmware reads the sensor PID at boot. |
| C3 | ESP32-CAM-MB board (shows up as an FTDI FT232R, `0403:6001`), to flash the ESP32-CAM | 1 | CONFIRMED (owned) | the ESP32-CAM has no USB port. This MB has **no auto-reset**: hold IO0, press RST to flash; press RST alone to run |
| C4 | HC-SR04 ultrasonic sensor | 4 | **1 CONFIRMED**, 3 to buy | front-center, front-left, front-right, rear. Fit order: front-center, then **rear** (enables reverse), then left/right. |
| C5 | Echo divider per sonar: 1 kΩ series + 1.8 kΩ to GND (5 V → 3.2 V) | 4 sets | RECOMMENDED | **never** connect 5 V Echo straight to an ESP32 pin |
| C6 | ~~PCA9685 PWM board~~ | 0 | **not needed** | since 1 Nov 2026 the ESP32-CAM drives the servo signal itself (IO2) |
| C7 | MG996R metal-gear servo, **180° positional type** + sturdy camera pan bracket | 1 | PLANNED (team choice, 1 Nov 2026) | not the 360° continuous-rotation version (stage 6 checks this); 55 g, so the bracket must be rigid. 180° is nominal; the usable range comes from calibration and the bracket |
| C8 | 10 kΩ pull-down per motor-driver input | 4 (6 with BTS7960) | RECOMMENDED | keeps driver inputs LOW while the S3 boots or resets |
| C9 | Battery divider 100 kΩ / 22 kΩ (+ 100 nF at the ADC pin) | 1 | RECOMMENDED | 12.6 V → 2.27 V on GPIO1 |
| C10 | Motor-bus divider 100 kΩ / 22 kΩ (+ 100 nF) | 1 | optional | lets the firmware see that the contactor opened |
| C11 | 470 µF 16 V electrolytic + 100 nF at the ESP32-CAM 5 V pin; 470–1000 µF across the servo 5 V / GND at the servo connector | 2 | RECOMMENDED | ESP32-CAM brownouts and servo current spikes are common |
| C12 | Soldered veroboard/perfboard, female headers, JST-XH or Dupont crimp connectors, heat shrink, cable ties | — | RECOMMENDED | rulebook 3.1 |

### Optional audio (two-way audio task, teleop only, ×1)

| # | Part | Qty | Label | Notes |
|---|---|---|---|---|
| A1 | ESP32 DevKitC (WROOM-32) | 1 | RECOMMENDED, optional | separate so audio work can never delay the motor control loop |
| A2 | INMP441 I²S MEMS microphone | 1 | listed in the team's notes; provisional | L/R pin to GND |
| A3 | MAX98357A I²S class-D amplifier | 1 | listed in the team's notes; provisional | |
| A4 | 4 Ω 3 W speaker | 1 | listed in the team's notes | |

## 3.2 Reconciling the team's component notes

The team's list from 25 Oct 2026 differs from the baseline in places. Nothing has been bought on
the strength of this document; decide per line:

| Team note | This repository | What to do |
|---|---|---|
| "10 A dual H-bridge (1 pc)" | MD30C × 2 | Too small for 2 × Pololu #4744 per side (11 A stall). Use MD30C or BTS7960. |
| "BTS7960 (2 pcs)" | supported | Set `MOTOR_DRIVER_TYPE DRIVER_BTS7960` and wire per §3.5. |
| "25GA motor 300 rpm × 4" | Pololu #4744 | **UNKNOWN rating.** Before choosing the fuses and driver, read the motor label or datasheet for voltage, no-load current and stall current. Change D1 and this section together. |
| "HC-SR04 (2 pcs)" | 4 | Any subset works; unfitted ones report `not_fitted`. With two, fit front-center and rear. |
| "INMP441, MAX98357A, 4 Ω 3 W speaker" on the main ESP (flow diagram) | separate optional ESP32 board | Kept separate on purpose (no audio code on the motor controller). Moving audio onto the S3 would need a port; it is not done. |
| "Kill switch: KCD11 mini rocker (red)" | red mushroom E-stop + contactor | A KCD11 is not a mushroom-head E-stop (rule 1.2.1) and is rated for small AC loads. Keep it out of the motor path. It may switch the 3 A logic branch only if its DC rating covers that. |
| "12 V 4-inch fan" | P13 | Added, current unknown. |

## 3.3 Power plan

```
                 F1 25A                 K1 contactor (≥40 A DC)
 BAT+ ──XT60──●──[====]──●───────────────o/ o───────●── MOTOR BUS ──┬─F5 15A─XT30─► MD30C LEFT  B+ ─► 2 left motors
 (3S, BMS)    │          │                          │               ├─F6 15A─XT30─► MD30C RIGHT B+ ─► 2 right motors
              │          │                          │               └─F8 3A ─XT30─► BUCK "servo 5V" ─► MG996R red (signal from ESP32-CAM IO2)
              │          │                          │
              │          │   (E-stop / ARM latch, §3.4)
              │          │
              │          └─F3 3A──XT30──► BUCK "logic 5V" ──► S3 5V, ESP32-CAM 5V, 4× HC-SR04 VCC, audio board 5V
              │                                └──────────► 12 V fan (P13) is on the F3 branch, before the buck
              │
 BAT− ────────●──────────────────────────────────── common GND (star point near the drivers' B−)
```

- **One ground.** Battery −, both drivers' B−, both buck GNDs, S3, CAM, servo and sonars
  share one ground. Run the motor-current returns straight to the star point, not through the
  logic board.
- **Logic stays powered when the E-stop is pressed.** The S3 keeps reporting telemetry, and the
  dashboard shows *why* it stopped.
- **The servo supply is on the motor bus,** so the E-stop also stops the camera pan. Its signal
  comes from the ESP32-CAM, which stays powered, and the S3 also tells the camera to hold the
  pan while in E-stop.
- **Current budget (5 V logic):** S3 about 0.35 A Wi-Fi peak, ESP32-CAM about 0.5 A peak,
  4 × HC-SR04 about 0.06 A, audio board about 0.3 A plus the amplifier (up to about 0.7 A peak at
  full volume). About 2 A peak in total, so the 3 A buck has margin.
- **Servo 5 V:** an MG996R draws 0.5–0.9 A while moving and up to about 2.5 A stalled. Use a
  buck rated ≥ 3 A continuous with 1000 µF at the servo, on a 3 A slow-blow fuse. Never power the
  servo from the S3's or the ESP32-CAM's 5 V pin, nor from the logic buck.

## 3.4 E-stop circuit (hardware, independent of software)

Single-relay self-latching circuit. The contactor holds itself on through its own output, so
once it drops out it stays out until ARM is pressed:

```
 BAT+ (after F1) ──F4 1A──[ ARM, momentary NO ]──┐
                                                  ├──[ E-stop NC #1 ]──●── K1 coil (+) ──┐
 MOTOR BUS (K1 output) ──F7 1A────────────────────┘                    │                 │
                                                          1N4007 ──────┘ cathode at (+)   │
                                                          anode ─────────────────────────●─ GND
 S3 GPIO21 (INPUT_PULLUP) ──[ E-stop NC #2 ]── GND        (closed = released, open/broken = PRESSED)
```

How it behaves:

| Action | Result |
|---|---|
| Power on | K1 open; motors and servo unpowered. The S3 boots with outputs disabled. |
| Press **ARM** (E-stop released) | coil energised → K1 closes → motor bus live → coil now held from the motor bus |
| Press **E-stop** | NC #1 opens → coil drops → K1 opens → motor bus dead (motors **and** servo). NC #2 opens → S3 latches ESTOP. |
| Release E-stop | **nothing restarts.** K1 stays open (its hold path is dead) and the S3 stays in ESTOP. |
| Restart | twist-release the E-stop → press ARM (power) → RESET E-STOP in the dashboard (software) → choose a mode |
| S3 crashes, Wi-Fi drops, dashboard closes | K1 is unaffected; the E-stop still cuts power. The S3 watchdog reboots with outputs LOW. |

Check at inspection: with the motors running on the bench (wheels raised), press the E-stop and
the wheels must stop and the motor bus must read 0 V on a multimeter.

## 3.5 Motor drivers and motor pairing

**MD30C (baseline), one per side:**

| MD30C pin | Left driver | Right driver |
|---|---|---|
| PWM | S3 GPIO4 + 10 kΩ to GND | S3 GPIO6 + 10 kΩ to GND |
| DIR | S3 GPIO5 + 10 kΩ to GND | S3 GPIO7 + 10 kΩ to GND |
| GND (logic) | common GND | common GND |
| B+ / B− | motor bus via F5 / GND | motor bus via F6 / GND |
| MA / MB | both left motors **in parallel** | both right motors **in parallel** |

- Set the MD30C jumper/switch for **external PWM** (sign-magnitude PWM + DIR), per its manual.
- The firmware uses 20 kHz PWM at 10 bits.
- **Pairing:** wire both motors on a side so that they spin the same way for the same polarity
  (check each motor alone on a bench supply first). If a whole side runs backwards, swap that
  side's MA/MB or set `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT`. That check happens in bring-up
  stage 8, with the wheels raised.

**BTS7960 / IBT-2 alternative:** RPWM = A pin (GPIO4 left, GPIO6 right), LPWM = B pin (GPIO5,
GPIO7), R_EN + L_EN of **both** modules = GPIO15, each with a 10 kΩ pull-down. The IBT-2 inputs
go through a 74HC244 buffer powered from the module's VCC pin. At VCC = 5 V that buffer needs
≥ 3.5 V for a HIGH, which 3.3 V GPIO does not guarantee. Power the module's **VCC from 3.3 V**
(check on your module that VCC feeds only the buffer), or add a 74HCT245 level shifter.

## 3.6 Sonars (HC-SR04 ×4)

```
 5V logic ── VCC   HC-SR04   GND ── GND
 S3 TRIG pin ── Trig
                Echo ──[1 kΩ]──●── S3 ECHO pin
                               └──[1.8 kΩ]── GND        (5 V × 1.8/2.8 = 3.2 V)
```

| Sonar | Trig | Echo | Mount |
|---|---|---|---|
| front_center (index 0) | GPIO10 | GPIO11 | facing forward, centred, level |
| front_left (1) | GPIO12 | GPIO13 | forward-left, angled about 30° out |
| front_right (2) | GPIO14 | GPIO16 | forward-right, angled about 30° out |
| rear (3) | GPIO17 | GPIO18 | facing backward; enables reverse |

- Sensors fire one at a time, in 40 ms slots (no cross-talk). Front-center is sampled every other slot.
- Measure each sensor face's distance behind the bumper and enter it in `SONAR_FACE_TO_BUMPER_MM`,
  so all distances are bumper-referenced (stage 4).
- Most HC-SR04s trigger reliably from 3.3 V. If one does not, use the HC-SR04P (3.3–5 V) variant.

## 3.7 Camera pan servo (MG996R) on the ESP32-CAM

| From | To | Notes |
|---|---|---|
| ESP32-CAM **IO2** | MG996R signal (orange) | 50 Hz PWM from the camera's LEDC (`PAN_PIN`). Most MG996Rs accept a 3.3 V signal; if it jitters or ignores it, add a 74AHCT125 buffer powered from 5 V |
| "servo 5V" buck (on the motor bus) | MG996R V+ (red) | servo power only, ≥ 3 A, with 1000 µF across V+ / GND at the servo |
| GND | MG996R GND (brown) **and** ESP32-CAM GND | the signal needs this common ground |

- IO2 is the only free camera pin that neither stops the boot (IO12) nor pulses at power-up
  (IO15). It is a strapping pin, so nothing may pull it HIGH at reset; a servo signal input does not.
  **Provisional** until stage 6 shows a normal boot with the servo connected.
- The camera starts at home (90°), keeps a 45–135° range (`kPanMinDeg` / `kPanMaxDeg`, shared by
  both boards) and slews at 120°/s. Calibrate the end pulses in small steps (stage 6), and check
  that the camera ribbon cannot snag at either end before widening the range.
- The S3 picks the pan mode (hold, goto, scan, track, pulse) over the camera link; scanning and
  target following run on the camera.

## 3.8 ESP32-CAM

| ESP32-CAM pin | To | Notes |
|---|---|---|
| 5V | logic 5 V buck | short, thick wires, 470 µF + 100 nF at the pin |
| GND | common GND | |
| GPIO13 (RX) | S3 GPIO41 (TX) | 3.3 V both sides |
| GPIO14 (TX) | S3 GPIO42 (RX) | |
| GPIO2 | MG996R signal (orange) | camera pan servo, see §3.7 |
| GPIO4 | — | flash LED, kept OFF by the firmware |
| microSD slot | **leave empty** | it shares GPIO13/14 with the link |

Programming: connect a 3.3 V USB-UART adapter to U0TXD (GPIO1) / U0RXD (GPIO3) and tie **GPIO0 to
GND** while resetting to enter the bootloader, or use an ESP32-CAM-MB board.

## 3.9 Optional audio board

| ESP32 DevKit | Part pin |
|---|---|
| 3V3 | INMP441 VDD |
| GND | INMP441 GND, INMP441 **L/R** |
| GPIO26 | INMP441 SCK |
| GPIO25 | INMP441 WS |
| GPIO34 | INMP441 SD |
| 5V (from the logic buck) | MAX98357A VIN |
| GND | MAX98357A GND |
| GPIO27 | MAX98357A BCLK |
| GPIO32 | MAX98357A LRC |
| GPIO33 | MAX98357A DIN |
| GPIO23 | MAX98357A SD (HIGH = on) |
| — | MAX98357A GAIN: leave open (9 dB) |
| MAX98357A + / − | 4 Ω 3 W speaker |

No connection to the S3 other than common power and ground. It talks over Wi-Fi only.

## 3.10 Build rules checklist

- [ ] Every module sits on soldered female headers on veroboard/PCB. No breadboard anywhere.
- [ ] Every power joint uses XT30/XT60 or Deans. No bare wire, clips or tape.
- [ ] Red = positive (voltage labelled at each junction), black = GND, other colours for signals.
- [ ] Heat shrink on every joint, cable ties, strain relief at every connector.
- [ ] Motor wires routed away from sonar, servo and camera-link wiring.
- [ ] E-stop rigidly mounted, reachable from outside without crossing moving parts.
- [ ] Battery fused at the battery, secured against vibration, in a fire-resistant bag when charging.
- [ ] Schematic drawn with every pin labelled, committed to the repository with the pin map.

## 3.11 Shopping list (beyond what the team already owns)

3 × HC-SR04 · 1 × MG996R (180° type) + rigid pan bracket · 1 × 5 V ≥ 3 A servo buck · 2 × MD30C (or confirm the owned
drivers) · 4 × Pololu #4744 (or #4754 with encoders; or confirm the owned 25GA motors' rating) ·
1 × red mushroom E-stop with 2 NC blocks · 1 × 12 V-coil contactor ≥ 40 A DC · 1 × green NO
push button · fuse holders and 25/15/15/3/2/1/1 A blade fuses · XT60 × 1 pair, XT30 × 4 pairs ·
2 × 5 V buck converters (3 A and 2 A) · 3S Li-ion pack with ≥ 25 A BMS + 12.6 V charger +
fire-resistant bag · resistors (1 k × 4, 1.8 k × 4, 10 k × 6, 100 k × 2, 22 k × 2) · capacitors
(470 µF 16 V × 2, 1000 µF 16 V × 1, 100 nF × 4) · 1N4007 × 1 · wire per P12 · veroboard, headers,
crimp connectors · 3.3 V USB-UART adapter or ESP32-CAM-MB. Optional audio: ESP32 DevKitC,
INMP441, MAX98357A, 4 Ω 3 W speaker.

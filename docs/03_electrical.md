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
  **Team decisions (6 and 13 Nov 2026):** the E-stop is a plain 2-pin ON/OFF switch (SW1) in the motor
  supply line. It removes power from the motors and the servo in hardware. It is not a mushroom-head button,
  so **1.2.1 is NOT met**. "No restart on release" is enforced by the S3 firmware and the driver pull-downs,
  **not by hardware** (§3.4). Get the organisers' approval in writing before inspection. The inspector may
  refuse it, and a rover that fails the E-stop test may not run (1.2.3).
- 3.1: circuits on **soldered veroboard/perfboard or PCB**. No breadboards anywhere on the rover.
- 3.2: **XT30/XT60/XT90 or Deans** on every power connection between the battery, E-stop, distribution and drivers. Red wire for every positive line, black for ground, any other colour for signals.
- 3.3: nominal battery ≤ 24 V, fused, secured, and carried and charged in a fire-resistant bag.
- 3.4: a schematic showing every pin, committed to GitHub with the pin map. **The full schematic, wire schedule, pin maps and test sheet are in [Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf](Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf), for the electrical team.** The circuit must match it before the M4 freeze, so change it when the build changes (`tools/make_electrical_pdf.py`). It is drawn by a script; rule 3.4.1 names schematic software or a neat pen-and-paper diagram, so redraw it in KiCad or EasyEDA if a judge asks.
- Class A (4WD, no flippers) is assumed, since no flipper hardware exists.

## 3.1 Bill of materials

### The team's component list (13 Nov 2026)

Every row is **CONFIRMED** as the team's choice. The last column says what still has to be checked
on the part itself.

| # | Part (team list) | Qty | Ref | Use in this design | Check before wiring |
|---|---|---|---|---|---|
| 1 | BTS7960 motor driver (IBT-2 module) | 2 | U4, U5 | one per side, each side's two motors in parallel (§3.5) | VCC pin has no connection to B+ |
| 2 | 25GA 300 RPM DC gear motor, **12 V** | 4 | M1–M4 | M1, M2 left; M3, M4 right | **stall current UNKNOWN**: measure it (test T17); it sets F1 |
| 3 | 25GA motor mount | 4 | — | mechanical | |
| 4 | 25GA hex long shaft (coupling) | 4 | — | mechanical | |
| 5 | 85 mm wheel | 4 | — | mechanical | |
| 6 | "INA 441" (name to verify) | 1 | U8 | assumed to be the **INMP441** I²S microphone (optional audio, §3.9) | **UNKNOWN** until the marking is read. An INMP441 board has SCK, WS, SD, L/R, VDD, GND |
| 7 | MAX98357 I²S audio amplifier | 1 | U9 | optional audio | |
| 8 | 4 Ω 3 W speaker | 1 | LS1 | optional audio | |
| 9 | HC-SR04 ultrasonic sensor | 4 | US1–US4 | front-centre, front-left, front-right, rear (§3.6) | only US1 is wired and tested so far |
| 10 | 12 V 4-inch fan | 1 | FAN1 | on the logic pack after SW2 | current on its label (**UNKNOWN**) |
| 11 | Female header | 2 | — | sockets for U1 (and U2) on the logic board | strip length **UNKNOWN**: U1 alone needs 2 × 22 pins |
| 12 | ON/OFF switch, **2 pins** (single-pole) | 2 | SW1, SW2 | SW1 = E-STOP in the motor line, SW2 = POWER in the logic line | **rating UNKNOWN**: SW1 must be DC-rated at ≥ the F1 current (§3.4) |
| 13 | 4-pin JST connector, 90° | 15 | P1–P10 | board side of every signal cable (§3.10) | |
| 14 | 4-pin JST connector, straight | 15 | — | cable side, mating P1–P10 | |
| 15 | Double-sided vero board | 1 | — | the logic board (§3.10). No motor current on it | |
| 16 | XL4015 DC-DC buck converter, 5 A | 2 | U6, U7 | U6 = logic 5 V from the logic pack; U7 = servo 5 V from the motor bus | set each to 5.0 V with nothing connected (test T3, T12) |
| 17 | 850 mAh LiPo, **3S** (11.1 V, 12.6 V full) | 2 | BT1, BT2 | BT1 = motor pack, BT2 = logic pack (§3.3) | which plug each pack has; its C rating |
| 18 | ESP32 DevKit | 1 | U3 | optional audio board | |
| 19 | ESP32-S3 development board | 1 | U1 | main controller, the only motor authority | runs today on the bench |
| 20 | ESP32-CAM module | 1 | U2 | camera, QR, motion, pan servo signal | runs today on the bench (GC2145 sensor) |
| 21 | SG90 servo | 1 | SV1 | camera pan (§3.7) | that it holds a position (a "360°" SG90 cannot) |

Also owned, not on the rover: the ESP32-CAM-MB programming board (shows up as an FTDI FT232R,
`0403:6001`; **no auto-reset**: hold IO0 and press RST to flash, press RST alone to run).

### Must add: not in the list, but the rules or the circuit need it

| Part | Qty | Why |
|---|---|---|
| Inline blade-fuse holder + slow-blow fuses: **10 A** (F1) and **3 A** (F2), plus spares | 2 holders | rule 3.3.1: every battery fused, as close to the pack as possible |
| XT30 pairs (J1–J7) | 7 | rule 3.2.1: every power joint between batteries, E-stop, distribution and drivers. If a pack already has an XT30 or XT60 plug, only its mate is needed; a red JST-RCY plug does not meet the rule |
| 3S LiPo **balance** charger | 1 | none in the list. Never charge a LiPo from a plain power supply |
| Fire-resistant LiPo bag | 1 | rule 3.3.2: carry and charge the packs in it |
| Resistors, 1/4 W: 10 kΩ × 6, 1 kΩ × 4, 1.8 kΩ × 4, 100 kΩ × 2, 22 kΩ × 2; and **1 kΩ 0.5 W × 1** | 19 | driver pull-downs R1–R6, sonar Echo dividers R11–R18, voltage sense R21–R24, motor-bus bleed R25 |
| Capacitors: 470 µF 16 V × 2, 100 µF 16 V × 4, 100 nF × 7 | 13 | ESP32-CAM supply (C1, C2), servo supply (C3), sonar supplies (C11–C18), ADC filters (C21, C22) |
| Wire: 18 AWG silicone red + black, 22 AWG red + black, 24–26 AWG in other colours | — | rule 3.2.3 colours |
| Dupont female housings (or jumper leads), heat shrink, cable ties | — | the IBT-2 control header is male pins; rule 3.1 |
| Camera pan bracket for the SG90 | 1 | the camera has to turn |
| LiPo cell checker / low-voltage alarm | 1–2 | RECOMMENDED: bare LiPo packs have no BMS |

**Motor data.** 25GA 300 RPM 12 V is a seller code, not one part number, and the stall current
depends on the maker. Seller data for similar 25 mm, 370-size 12 V gearmotors runs from 1.2 A to
5 A stall. So F1 is **provisional at 10 A** (four motors at 2.5 A each). Measure one motor stalled on
a bench supply (T17) and set F1 just above 4 × the stall current or 3 × the four-motor running
current, whichever is lower, but never above SW1's rating. The BTS7960 current limit (43 A typical)
is far above any of these.

**3S packs vs 12 V motors.** A full 3S pack is 12.6 V, 5 % above the motor rating. The firmware
caps PWM duty at 12.0 V / V<sub>motor bus</sub> while the motor-bus voltage is measured, and at a
fixed 0.95 (= 12.0 / 12.6) otherwise.

**Battery care.** Bare LiPo packs have no BMS. Stop and charge at **10.5 V** (3.5 V per cell);
never run below **9.9 V** (3.3 V per cell). The dashboard shows the motor pack as the motor-bus
voltage, and the logic pack as the battery voltage once R21/R22 are fitted and calibrated. Charge
both packs before every run; 850 mAh is small, so measure the run time in test T17.

## 3.2 What changed on 13 Nov 2026

| Earlier design | Now | Why |
|---|---|---|
| Pololu #4744 motors | 25GA 300 RPM 12 V | team list |
| One 3S Li-ion pack with BMS | **two** 3S LiPo packs, 850 mAh: BT1 motors + servo, BT2 logic | team list. Split, so a motor stall can never brown out the ESP32 boards |
| Contactor K1, ARM button, hold path, flyback diode | **removed**. SW1 cuts the motor line directly | no contactor in the list; team chose the direct switch |
| 2-pole E-stop switch, second pole on GPIO21 | 1-pole switch; the S3 senses the **motor-bus voltage** on GPIO2 | the list's switches have 2 pins |
| MG996R servo, 3 A servo buck | SG90 on XL4015 U7 | team list |
| Fuses F1–F8 | F1 10 A (motor pack), F2 3 A (logic pack) | smaller motors, no contactor circuit |
| Two generic 5 V bucks | two XL4015 | team list |
| 1 HC-SR04 | 4 | team list |

## 3.3 Power plan

```
                F1 10A                  SW1 E-STOP (2-pin)
 BT1 + ──J1──[====]────────────────────o/ o──J2──●── MOTOR BUS: 0 V, or 9.9–12.6 V
 (3S LiPo, motor pack)                           ├──J3──► U4 BTS7960 left  B+ ─► M1, M2
                                                 ├──J4──► U5 BTS7960 right B+ ─► M3, M4
                                                 ├──J5──► U7 XL4015 "servo 5V" ─► SG90 red (signal from ESP32-CAM IO2)
                                                 ├── R25 1 kΩ 0.5 W ── GND        (bleed: bus empties in about 1 s)
                                                 └── R23 100k / R24 22k ──► S3 GPIO2 (E-stop sense)

                F2 3A                   SW2 POWER (2-pin)
 BT2 + ──J6──[====]────────────────────o/ o─────●──J7──► U6 XL4015 "logic 5V" ──► S3 5V, ESP32-CAM 5V, 4× HC-SR04,
 (3S LiPo, logic pack)                           │                                  DevKit 5V, MAX98357A VIN
                                                 ├──────► FAN1 12 V
                                                 └── R21 100k / R22 22k ──► S3 GPIO1 (logic pack, optional)

 BT1 −, BT2 − ──────────────────────────────────●── common GND (star point near the drivers' B−)
```

- **Two packs, one ground.** BT1 − and BT2 − join at the star point. Both drivers' B−, both
  XL4015 GNDs, the S3, the camera, the servo and the sonars share that ground. Run the motor
  returns straight to the star point, not through the logic board.
- **No motor current on the vero board.** The motor bus is a soldered, heat-shrunk splice of
  18 AWG wire from J2 to J3, J4 and J5. Only the thin sense lead (R23) and the bleed resistor reach
  the bus from the board side.
- **Logic stays powered when the E-stop is OFF.** The S3 keeps reporting, and the dashboard shows
  *why* it stopped. SW2 switches the whole logic side off.
- **The servo supply is on the motor bus,** so the E-stop also stops the camera pan. Its signal
  comes from the ESP32-CAM, which stays powered, and the S3 also tells the camera to hold the pan
  while in E-stop.
- **XL4015 set-up:** input 8–36 V by its datasheet, so a 3S pack (9.9–12.6 V) is inside the range.
  Set each output to 5.0 V with nothing connected (T3, T12). If your module has a second (current)
  trimmer, leave it at its maximum.
- **Current budget, logic pack:** S3 about 0.35 A Wi-Fi peak, ESP32-CAM about 0.5 A peak, 4 × HC-SR04
  about 0.06 A, audio board about 0.3 A plus the amplifier (up to about 0.7 A at full volume). About
  2 A peak at 5 V, about 1 A from the pack, plus the fan. F2 = 3 A.
- **Wire:** BT1 to the drivers 18 AWG; drivers to motors 18–20 AWG; BT2 side and 5 V rails 22 AWG;
  signals 24–26 AWG.

## 3.4 E-stop circuit (hardware cut, S3 latch)

SW1 is the only path from the motor pack to the motors and the servo. The S3 cannot see the switch
directly (it has one pole), so it watches the motor-bus voltage instead:

```
 BT1 + ── F1 ── SW1 E-STOP ── MOTOR BUS ── U4, U5, U7
                                 │
                                 ├── R25 1 kΩ 0.5 W ── GND
                                 └── R23 100 kΩ ──●── S3 GPIO2   (12.6 V → 2.27 V)
                                                  ├── R24 22 kΩ ── GND
                                                  └── C22 100 nF ── GND
```

How it behaves:

| Action | Result |
|---|---|
| Power on: SW2 ON, SW1 OFF | logic runs; the bus is 0 V, so the S3 boots into ESTOP ("E-stop switch OFF at boot") |
| Switch SW1 ON | the bus rises to the pack voltage. The S3 **stays in ESTOP** until RESET E-STOP in the dashboard, then a mode is chosen |
| Switch SW1 OFF, driving or not | motors and servo lose power **in hardware** at once. The S3 reads the bus every 10 ms; below 6 V it latches ESTOP and holds the drivers' EN LOW |
| Switch SW1 back ON | **nothing moves.** The S3 holds ESTOP and EN LOW until RESET E-STOP, which it refuses while the bus is still 0 V |
| S3 crashes or reboots | R5 and R6 pull EN LOW, so both drivers are off. After the reboot the S3 starts disabled, and in ESTOP if SW1 is OFF |
| Wi-Fi drops, dashboard closes | the teleop timeout stops the motors within 0.3 s; SW1 still cuts power |

What this design does **not** do:

- **"No restart" is firmware, not hardware.** It depends on the S3 seeing the bus drop and on the
  EN pull-downs. Rule 1.2 asks for the power cut in hardware, which SW1 does; an inspector may still
  want the restart blocked in hardware. If so, go back to the contactor and ARM button design (PDF
  Rev C, Sheet 3).
- **SW1 carries all the motor current.** Its DC rating must be at least F1 (10 A at 12.6 V). AC
  ratings do not carry over to DC. The KCD11 mini rocker in the team's earlier notes is commonly
  sold as 3 A 250 V AC / 6 A 125 V AC with no DC rating: too small for this line. It is fine for SW2.
- **The bench jumper.** Without R23/R24, GPIO2 floats. On the bench, tie GPIO2 to 3V3 (reads as
  "bus present") and connect nothing that moves. **Remove it before the bus divider goes in**: left
  in, it hides the E-stop from the S3.

Fail-safe checks: a broken R23 or GPIO2 wire reads 0 V, which the S3 treats as E-stop OFF. An open
R24 pulls GPIO2 to the bus through 100 kΩ, which still reads 0 V when the bus is off.

Check at inspection: wheels raised, motors running. Switch SW1 OFF: the wheels stop, the dashboard
shows ESTOP, the bus reads about 0 V on a meter within about 1 s. Switch it back ON: nothing moves.

## 3.5 Motor drivers and motor pairing

**BTS7960 / IBT-2 module, one per side** (team decision, 11 Nov 2026). Each module holds two
BTS7960B half-bridges, so one module drives one side's two motors in both directions.

| IBT-2 pin | Left module | Right module |
|---|---|---|
| RPWM (forward) | S3 GPIO4 + 10 kΩ (R1) to GND, via P5 | S3 GPIO6 + 10 kΩ (R3) to GND, via P6 |
| LPWM (reverse) | S3 GPIO5 + 10 kΩ (R2) to GND, via P5 | S3 GPIO7 + 10 kΩ (R4) to GND, via P6 |
| R_EN + L_EN, joined at the module | S3 GPIO15 + 10 kΩ (R5) to GND, via P5 | S3 GPIO15 + 10 kΩ (R6) to GND, via P6 |
| R_IS, L_IS | not connected | not connected |
| VCC (logic) | S3 **3V3** pin, via P7 | S3 **3V3** pin, via P7 |
| GND (logic) | logic GND, via P5 | logic GND, via P6 |
| B+ / B− | motor bus via J3 / star GND | motor bus via J4 / star GND |
| M+ / M− | both left motors **in parallel** | both right motors **in parallel** |

- **VCC from 3.3 V, not 5 V.** The control pins go through a 74HC244 buffer powered from VCC. At
  VCC = 5 V that buffer needs about 3.5 V for a HIGH, which 3.3 V GPIO does not guarantee. With
  VCC on the S3's 3V3 pin the buffer and the GPIOs use the same level. Sellers list 3.3–5 V logic
  for this module. Check with a meter that VCC on your module has no connection to B+.
  RECOMMENDED, not tested on this rover.
- **How the firmware drives it:** forward = PWM on RPWM with LPWM LOW; reverse = PWM on LPWM with
  RPWM LOW. 20 kHz at 10 bits (the BTS7960 is specified for PWM up to 25 kHz). EN goes HIGH only
  while a side is being driven. EN LOW turns all four half-bridges off, so the motors coast; with EN
  HIGH and both inputs LOW a side brakes. E-stop, disarm and every fault set EN LOW at once.
- The 10 kΩ pull-downs keep every input LOW while the S3 boots or resets: one on each RPWM and
  LPWM, and one on the EN pin at each module (6 in total). Solder them at the logic board.
- R_IS and L_IS are current-sense outputs. The firmware does not read them, so leave them
  unconnected. Wiring one to an S3 pin would first need the module's sense resistor measured and a
  divider designed (UNKNOWN).
- **Heatsink.** At least one seller notes that the heatsink is not insulated from the board. Treat
  it as live: mount it so it touches no metal and no other module, or fit an insulating thermal pad.
- **Pairing:** wire both motors on a side so that they spin the same way for the same polarity
  (check each motor alone on a bench supply first). If a whole side runs backwards, swap that
  side's M+/M− or set `MOTOR_LEFT_INVERT` / `MOTOR_RIGHT_INVERT`. That check happens in bring-up
  stage 8, with the wheels raised.

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

## 3.7 Camera pan servo (SG90) on the ESP32-CAM

| From | To | Notes |
|---|---|---|
| ESP32-CAM **IO2** | SG90 signal (orange), via P9 | 50 Hz PWM from the camera's LEDC (`PAN_PIN`). Hobby servos normally accept a 3.3 V signal; if it jitters, add a 74AHCT125 buffer powered from 5 V |
| U7 "servo 5V" (on the motor bus) | SG90 V+ (red), via P9 | 470 µF (C3) across V+ / GND at the servo |
| GND | SG90 GND (brown) **and** ESP32-CAM GND | the signal needs this common ground |

- The SG90 is light (about 1.8 kg·cm by seller data), so balance the camera on the horn and keep
  the bracket short. It draws far less than the MG996R did; seller figures for its stall current run
  from about 0.55 A to over 1 A, well inside U7's 5 A.
- Some SG90s are sold as "360°" continuous-rotation servos. Those cannot hold an angle: stage 6
  checks that yours does.
- IO2 is the only free camera pin that neither stops the boot (IO12) nor pulses at power-up
  (IO15). It is a strapping pin, so nothing may pull it HIGH at reset; a servo signal input does not.
  A normal boot with a servo connected was seen on 2 Nov 2026 with the earlier MG996R; check it again
  with the SG90 in stage 6.
- The camera starts at home (90°), keeps a 45–135° range (`kPanMinDeg` / `kPanMaxDeg`, shared by
  both boards) and slews at 120°/s. The end pulses are nominal (500 / 2400 µs); calibrate them in
  small steps (stage 6), and check that the camera ribbon cannot snag at either end.
- The S3 picks the pan mode (hold, goto, scan, track, pulse) over the camera link; scanning and
  target following run on the camera.

## 3.8 ESP32-CAM

| ESP32-CAM pin | To | Notes |
|---|---|---|
| 5V | U6 logic 5 V | short, thick wires, 470 µF (C1) + 100 nF (C2) at the pin |
| GND | common GND | |
| GPIO13 (RX) | S3 GPIO41 (TX) | 3.3 V both sides |
| GPIO14 (TX) | S3 GPIO42 (RX) | |
| GPIO2 | SG90 signal (orange) | camera pan servo, see §3.7 |
| GPIO4 | — | flash LED, kept OFF by the firmware |
| microSD slot | **leave empty** | it shares GPIO13/14 with the link |

Programming: connect a 3.3 V USB-UART adapter to U0TXD (GPIO1) / U0RXD (GPIO3) and tie **GPIO0 to
GND** while resetting to enter the bootloader, or use an ESP32-CAM-MB board.

## 3.9 Optional audio board

U8 is assumed to be an INMP441 (the list says "INA 441, name to verify"). Read the marking before
wiring it.

| ESP32 DevKit | Part pin |
|---|---|
| 3V3 | INMP441 VDD |
| GND | INMP441 GND, INMP441 **L/R** |
| GPIO26 | INMP441 SCK |
| GPIO25 | INMP441 WS |
| GPIO34 | INMP441 SD |
| 5V (from U6, via P13) | MAX98357A VIN |
| GND | MAX98357A GND |
| GPIO27 | MAX98357A BCLK |
| GPIO32 | MAX98357A LRC |
| GPIO33 | MAX98357A DIN |
| GPIO23 | MAX98357A SD (HIGH = on) |
| — | MAX98357A GAIN: leave open (9 dB) |
| MAX98357A + / − | 4 Ω 3 W speaker |

No connection to the S3 other than common power and ground. It talks over Wi-Fi only.

## 3.10 Logic board and connectors

One double-sided vero board holds U1 on the female headers, the pull-downs R1–R6, the Echo dividers
R11–R18, R22, R24, C21, C22 and the board side of every signal cable. Each cable uses one of the
list's 4-pin JST pairs: the 90° part on the board, the straight part on the cable. Mark pin 1 on the
board and on every plug.

| Conn | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Cable goes to |
|---|---|---|---|---|---|
| P1 | +5V LOGIC | GPIO10 (Trig) | Echo, 5 V: R11 to GPIO11, R12 to GND | GND | US1 front-centre |
| P2 | +5V LOGIC | GPIO12 | Echo: R13 to GPIO13, R14 to GND | GND | US2 front-left |
| P3 | +5V LOGIC | GPIO14 | Echo: R15 to GPIO16, R16 to GND | GND | US3 front-right |
| P4 | +5V LOGIC | GPIO17 | Echo: R17 to GPIO18, R18 to GND | GND | US4 rear |
| P5 | GPIO4 RPWM (R1) | GPIO5 LPWM (R2) | GPIO15 EN (R5) | GND | U4 control header (Dupont housing at the module) |
| P6 | GPIO6 RPWM (R3) | GPIO7 LPWM (R4) | GPIO15 EN (R6) | GND | U5 control header |
| P7 | 3V3 to U4 VCC | 3V3 to U5 VCC | n.c. | n.c. | U4, U5 VCC pins |
| P8 | +5V LOGIC | GND | GPIO41 to U2 IO13 | U2 IO14 to GPIO42 | U2, when it is not socketed on this board |
| P9 | +5V SERVO (U7) | GND | servo signal (U2 IO2) | n.c. | SV1 SG90 |
| P10 | U2 IO2 | GND | n.c. | n.c. | from U2 to P9 pin 3, when U2 is off this board |
| P11 | +5V from U6 | GND | +5V from U6 | GND | U6 output (two pins each share the current) |
| P12 | from R21 (BT2 side, after SW2) | n.c. | from R23 (motor bus) | n.c. | voltage sense leads; R21 and R23 sit at the supply end, so the leads carry only µA |
| P13 | +5V LOGIC | GND | n.c. | n.c. | U3 audio DevKit and U9 VIN |

P14 and P15 are spare. The fan and both packs never touch the board: they run on 18 to 22 AWG
wire through XT30 connectors and the switches.

## 3.11 Build rules checklist

- [ ] Every module sits on soldered female headers or is screwed down. No breadboard anywhere.
- [ ] Every power joint between batteries, E-stop, splice and drivers is XT30/XT60 or Deans (J1–J7). No bare wire, clips or tape.
- [ ] Red = positive (voltage labelled at each junction), black = GND, other colours for signals.
- [ ] Heat shrink on every joint, cable ties, strain relief at every connector.
- [ ] Motor wires routed away from sonar, servo and camera-link wiring.
- [ ] SW1 labelled E-STOP, rigidly mounted, reachable from outside without crossing moving parts; SW2 labelled POWER.
- [ ] Both packs fused (F1, F2) at the pack, strapped down against vibration, in the fire-resistant bag when charging.
- [ ] The GPIO2 bench jumper is removed once R23/R24 are fitted.
- [ ] Schematic drawn with every pin labelled, committed to the repository with the pin map.

## 3.12 What to buy

Everything in the "Must add" table in §3.1: fuse holders and the 10 A and 3 A fuses, 7 XT30 pairs, a
3S LiPo balance charger, a fire-resistant LiPo bag, the resistors and capacitors listed there,
wire, Dupont housings, heat shrink, a pan bracket for the SG90, and (recommended) a LiPo cell
checker. Before buying a replacement for SW1, read the rating printed on the switches you have.

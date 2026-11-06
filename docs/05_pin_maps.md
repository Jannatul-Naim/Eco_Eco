# 5. Pin maps

> **PROVISIONAL.** The S3 header was checked against the team's photo (1 Nov 2026). Tested with real
> wiring so far: S3 GPIO10, 11, 21, 41, 42 and camera IO2, IO13, IO14. The same pins are drawn on
> Sheet 1 of [Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf](Eco_Eco_Electrical_Schematic_and_Build_Guide.pdf). Before wiring a board, compare its header silkscreen with the table. If a pin label differs, or a pin listed here does not exist on your
> header, **stop**. Send a photo of both sides of the board and the header labels, and update
> the config file before you wire anything.

Source of truth: `src/s3/config_pins.h`, `src/cam/config_cam.h`, `src/audio/config_audio.h`.
Change the code and this page together.

## 5.1 ESP32-S3 main controller

Board reported as "ESP32 23 2022 V1.3". The team's photo (1 Nov 2026) shows an **ESP32-S3-N16R8**
module on a two-USB-C board with an RGB LED, whose header labels match the
**ESP32-S3-DevKitC-1** layout. Every GPIO in this table is on the header, including 41/42 for the
camera link.

| GPIO | Function | Direction | Connects to | Notes |
|---|---|---|---|---|
| 1 | `PIN_BATT_ADC` | in (ADC1) | battery divider 100 k / 22 k, before the contactor | optional; `BATTERY_SENSE_FITTED` |
| 2 | `PIN_MOTOR_BUS_ADC` | in (ADC1) | motor-bus divider 100 k / 22 k, after the contactor | optional; `MOTOR_BUS_SENSE_FITTED` |
| 4 | `PIN_LEFT_A` | out PWM | MD30C left **PWM** / BTS7960 left RPWM | 10 kΩ to GND |
| 5 | `PIN_LEFT_B` | out | MD30C left **DIR** / BTS7960 left LPWM | 10 kΩ to GND |
| 6 | `PIN_RIGHT_A` | out PWM | MD30C right **PWM** / BTS7960 right RPWM | 10 kΩ to GND |
| 7 | `PIN_RIGHT_B` | out | MD30C right **DIR** / BTS7960 right LPWM | 10 kΩ to GND |
| 10 | sonar 0 TRIG | out | front_center Trig | |
| 11 | sonar 0 ECHO | in | front_center Echo via 1 k / 1.8 k | |
| 12 | sonar 1 TRIG | out | front_left Trig | |
| 13 | sonar 1 ECHO | in | front_left Echo via divider | |
| 14 | sonar 2 TRIG | out | front_right Trig | |
| 15 | `PIN_DRIVER_EN` | out | BTS7960 R_EN + L_EN (both modules) | BTS7960 only; 10 kΩ to GND |
| 16 | sonar 2 ECHO | in | front_right Echo via divider | |
| 17 | sonar 3 TRIG | out | rear Trig | |
| 18 | sonar 3 ECHO | in | rear Echo via divider | |
| 21 | `PIN_ESTOP_SENSE` | in, pull-up | E-stop switch pole B → GND | LOW = switch ON (run), HIGH/open = OFF or wire broken |
| 41 | `PIN_CAM_UART_TX` | out | ESP32-CAM GPIO13 (RX) | 115200 8N1 |
| 42 | `PIN_CAM_UART_RX` | in | ESP32-CAM GPIO14 (TX) | |
| 8, 9, 39, 40 | spare | — | — | believed safe on DevKitC-1-style boards (8/9 were the PCA9685 I²C pins) |
| 5V / VIN | — | — | logic 5 V buck | |

Pins avoided on purpose: 0, 3, 45, 46 (strapping) · 19, 20 (USB) · 26–32 (flash) · 33–37
(octal PSRAM/flash on N8R8/N16R8 modules) · 38, 48 (RGB LED on common boards) · 43, 44 (UART0
serial monitor) · 47.

Which USB port: the `s3_main` environment logs on the USB-UART bridge port (often labelled
"COM" or "UART"). On the native USB port (`303A:1001`, "USB JTAG/serial debug unit"), build
`s3_main_nativeusb` instead. Since 1 Nov 2026 the team's board is connected through its CH343
bridge (`1a86:55d3`, appears as `/dev/ttyACM0`) and runs `s3_main`.

Chip as reported by esptool on 31 Oct 2026: ESP32-S3 (QFN56) rev v0.2 with **embedded 8 MB PSRAM**,
MAC fc:01:2c:d1:5c:6c. The boot banner reports 16 MB flash. In-package PSRAM on the S3 is octal and
occupies GPIO33–37, which this map already avoids. The build does not enable PSRAM.

## 5.2 Camera pan servo

The pan servo (MG996R) is wired to the ESP32-CAM (IO2), not to the S3, and the S3 has no I²C bus. See
§5.3 and [03_electrical.md](03_electrical.md) §3.7.

## 5.3 ESP32-CAM (AI-Thinker layout, **provisional**)

Team's board (esptool, 1 Nov 2026): ESP32-D0WD-V3 rev v3.1, 40 MHz crystal, 4 MB flash,
MAC 1c:c3:ab:e0:4b:14, programmed through an FTDI FT232R adapter with no reset line (press RST by hand).

| GPIO | Function |
|---|---|
| 32 | camera PWDN |
| 0 | camera XCLK (also the boot strap: GND at reset = flash mode) |
| 26 / 27 | camera SIOD / SIOC (SCCB) |
| 35, 34, 39, 36, 21, 19, 18, 5 | camera D7 … D0 |
| 25 / 23 / 22 | VSYNC / HREF / PCLK |
| 2 | **pan servo signal, MG996R** (`PAN_PIN`, LEDC 50 Hz; provisional, strapping pin) |
| 4 | flash LED (kept OFF) |
| 13 | **link RX** ← S3 GPIO41 |
| 14 | **link TX** → S3 GPIO42 |
| 1 / 3 | U0TXD / U0RXD (programming and serial monitor) |
| 16 | PSRAM CS (do not use) |
| 12 | avoided (flash-voltage strapping pin) |

Leave the microSD slot empty: it shares GPIO13/14 with the link. The firmware prints the camera
sensor PID at boot (expected `0x2145` if the GC2145 hypothesis is right; `0x0026` would be an
OV2640). A failed `esp_camera_init` means this layout or the sensor is wrong. Stop and identify
the module.

## 5.4 Optional audio board (ESP32 DevKitC, provisional)

Team's board (esptool, 1 Nov 2026): ESP32-D0WD-V3 rev v3.1, **26 MHz crystal** (unusual; the
Arduino core auto-detects it), 4 MB flash, CP2102 USB bridge, MAC 8c:94:df:69:24:ac.

| GPIO | Function | Part pin |
|---|---|---|
| 26 | I²S0 BCLK | INMP441 SCK |
| 25 | I²S0 WS | INMP441 WS |
| 34 | I²S0 data in | INMP441 SD |
| 27 | I²S1 BCLK | MAX98357A BCLK |
| 32 | I²S1 LRC | MAX98357A LRC |
| 33 | I²S1 data out | MAX98357A DIN |
| 23 | amp enable | MAX98357A SD |

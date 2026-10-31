# 5. Pin maps

> **PROVISIONAL.** No board identity is confirmed. Before wiring a board, compare its header
> silkscreen with the table. If a pin label differs, or a pin listed here does not exist on your
> header, **stop**. Send a photo of both sides of the board and the header labels, and update
> the config file before you wire anything.

Source of truth: `src/s3/config_pins.h`, `src/cam/config_cam.h`, `src/audio/config_audio.h`.
Change the code and this page together.

## 5.1 ESP32-S3 main controller

Board reported as "ESP32 23 2022 V1.3". The map assumes an **ESP32-S3-DevKitC-1-compatible**
header (possibly a VCC-GND "YD-ESP32-S3", silkscreen "YD-ESP32-23 2022-V1.3"; **unconfirmed**).

| GPIO | Function | Direction | Connects to | Notes |
|---|---|---|---|---|
| 1 | `PIN_BATT_ADC` | in (ADC1) | battery divider 100 k / 22 k, before the contactor | optional; `BATTERY_SENSE_FITTED` |
| 2 | `PIN_MOTOR_BUS_ADC` | in (ADC1) | motor-bus divider 100 k / 22 k, after the contactor | optional; `MOTOR_BUS_SENSE_FITTED` |
| 4 | `PIN_LEFT_A` | out PWM | MD30C left **PWM** / BTS7960 left RPWM | 10 kΩ to GND |
| 5 | `PIN_LEFT_B` | out | MD30C left **DIR** / BTS7960 left LPWM | 10 kΩ to GND |
| 6 | `PIN_RIGHT_A` | out PWM | MD30C right **PWM** / BTS7960 right RPWM | 10 kΩ to GND |
| 7 | `PIN_RIGHT_B` | out | MD30C right **DIR** / BTS7960 right LPWM | 10 kΩ to GND |
| 8 | `PIN_I2C_SDA` | I²C | PCA9685 SDA | pull-up on the PCA9685 board (to its 3.3 V VCC) |
| 9 | `PIN_I2C_SCL` | I²C | PCA9685 SCL | |
| 10 | sonar 0 TRIG | out | front_center Trig | |
| 11 | sonar 0 ECHO | in | front_center Echo via 1 k / 1.8 k | |
| 12 | sonar 1 TRIG | out | front_left Trig | |
| 13 | sonar 1 ECHO | in | front_left Echo via divider | |
| 14 | sonar 2 TRIG | out | front_right Trig | |
| 15 | `PIN_DRIVER_EN` | out | BTS7960 R_EN + L_EN (both modules) | BTS7960 only; 10 kΩ to GND |
| 16 | sonar 2 ECHO | in | front_right Echo via divider | |
| 17 | sonar 3 TRIG | out | rear Trig | |
| 18 | sonar 3 ECHO | in | rear Echo via divider | |
| 21 | `PIN_ESTOP_SENSE` | in, pull-up | E-stop NC contact #2 → GND | LOW = released, HIGH/open = pressed |
| 41 | `PIN_CAM_UART_TX` | out | ESP32-CAM GPIO13 (RX) | 115200 8N1 |
| 42 | `PIN_CAM_UART_RX` | in | ESP32-CAM GPIO14 (TX) | |
| 39, 40 | spare | — | — | believed safe on DevKitC-1-style boards |
| 3V3 | — | — | PCA9685 VCC | |
| 5V / VIN | — | — | logic 5 V buck | |

Pins avoided on purpose: 0, 3, 45, 46 (strapping) · 19, 20 (USB) · 26–32 (flash) · 33–37
(octal PSRAM/flash on N8R8/N16R8 modules) · 38, 48 (RGB LED on common boards) · 43, 44 (UART0
serial monitor) · 47.

Which USB port: the `s3_main` environment logs on the USB-UART bridge port (often labelled
"COM" or "UART"). If you only use the native USB port ("USB"), build `s3_main_nativeusb` instead.

## 5.2 I²C bus (S3)

| Address | Device | Expected in `i2cscan` |
|---|---|---|
| 0x40 | PCA9685 (A0–A5 open) | yes |
| 0x70 | PCA9685 all-call | may appear |

Bus speed 100 kHz, 20 ms timeout. If the board shows a different address (solder jumpers), set
`PCA9685_I2C_ADDR` in `src/s3/config_hardware.h`.

## 5.3 PCA9685 channels

| Channel | Device | Supply |
|---|---|---|
| 0 | SG90 camera pan (`PAN_SERVO_CHANNEL`) | V+ from the "servo 5 V" buck on the motor bus |
| 1–15 | free | — |

## 5.4 ESP32-CAM (AI-Thinker layout, **provisional**)

| GPIO | Function |
|---|---|
| 32 | camera PWDN |
| 0 | camera XCLK (also the boot strap: GND at reset = flash mode) |
| 26 / 27 | camera SIOD / SIOC (SCCB) |
| 35, 34, 39, 36, 21, 19, 18, 5 | camera D7 … D0 |
| 25 / 23 / 22 | VSYNC / HREF / PCLK |
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

## 5.5 Optional audio board (ESP32 DevKitC, provisional)

| GPIO | Function | Part pin |
|---|---|---|
| 26 | I²S0 BCLK | INMP441 SCK |
| 25 | I²S0 WS | INMP441 WS |
| 34 | I²S0 data in | INMP441 SD |
| 27 | I²S1 BCLK | MAX98357A BCLK |
| 32 | I²S1 LRC | MAX98357A LRC |
| 33 | I²S1 data out | MAX98357A DIN |
| 23 | amp enable | MAX98357A SD |

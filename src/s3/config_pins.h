// ESP32-S3 main controller — GPIO map.
//
// *** PROVISIONAL ***  The team's board is an ESP32-S3-N16R8 module whose header
// labels match the ESP32-S3-DevKitC-1 layout (checked from a photo, 1 Nov 2026).
// Only GPIO10, 11, 21, 41 and 42 have been tested with real wiring so far.
//
// Pins deliberately avoided on every S3 module variant:
//   0, 3, 45, 46  strapping pins
//   19, 20        native USB D-/D+
//   26..32        SPI flash
//   33..37        octal PSRAM/flash on N8R8/N16R8/N32R8 modules
//   38, 48        on-board RGB LED on common DevKitC-1 / YD boards
//   43, 44        UART0 TX/RX (USB-UART bridge, serial monitor)
//   47            VDD_SPI domain on some modules
// STOP and re-check docs/05_pin_maps.md if your header silkscreen differs.
#pragma once

#define ROVER_PINMAP_STATUS "PROVISIONAL"

// ---- Analog sensing (ADC1 only: ADC2 is unusable while Wi-Fi runs)
constexpr int PIN_BATT_ADC = 1;       // logic pack BT2 divider (100k / 22k)
constexpr int PIN_MOTOR_BUS_ADC = 2;  // motor bus after the E-stop switch (100k / 22k): E-stop sense

// ---- Motor drivers: one BTS7960 (IBT-2) module per side, two motors in parallel.
// RPWM = forward duty, LPWM = reverse duty. Every pin has a 10k pull-down.
constexpr int PIN_LEFT_RPWM = 4;
constexpr int PIN_LEFT_LPWM = 5;
constexpr int PIN_RIGHT_RPWM = 6;
constexpr int PIN_RIGHT_LPWM = 7;
constexpr int PIN_DRIVER_EN = 15;  // R_EN + L_EN of both modules

// ---- HC-SR04 sonars. Every ECHO goes through its own 1k / 1k8 divider.
// Index order is fixed: 0 front-center, 1 front-left, 2 front-right, 3 rear.
constexpr int PIN_SONAR_TRIG[4] = {10, 12, 14, 17};
constexpr int PIN_SONAR_ECHO[4] = {11, 13, 16, 18};

// ---- UART link to the ESP32-CAM (3.3 V both sides, common GND)
constexpr int PIN_CAM_UART_TX = 41;  // S3 TX -> CAM GPIO13 (CAM RX)
constexpr int PIN_CAM_UART_RX = 42;  // S3 RX <- CAM GPIO14 (CAM TX)

// ---- Hardware E-stop sense: second pole of the E-stop switch between
// this pin and GND, internal pull-up. Closed (LOW) = released.
// Open, unplugged or broken wire (HIGH) = treated as PRESSED (fail-safe).
constexpr int PIN_ESTOP_SENSE = 21;  // only with a 2-pole E-stop switch (ESTOP_SENSE_FITTED)

// Spare, believed safe on DevKitC-1-style boards: 8, 9 (the old PCA9685 I2C
// pins; the pan servo now runs on the ESP32-CAM), 39, 40.

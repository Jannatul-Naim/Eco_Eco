// URRT rover — ESP32-S3 main controller.
//
// The S3 is the only authority over the drive motors. Physical safety does
// NOT depend on this firmware: the hardware E-stop opens the motor-power
// contactor directly (docs/03_electrical.md).
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "config_hardware.h"
#include "config_pins.h"
#include "config_tuning.h"
#include "console.h"
#include "events.h"
#include "net.h"
#include "rover.h"
#include "web.h"

namespace {
void controlTask(void*) {
  esp_task_wdt_add(nullptr);
  TickType_t last = xTaskGetTickCount();
  uint32_t missed = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    esp_task_wdt_reset();
    RoverLock lock(CONTROL_PERIOD_MS);
    if (lock.ok()) {
      g_rover.tick(millis());
      missed = 0;
    } else if (++missed == 20) {
      // 200 ms without a control tick: something holds the lock. Fail safe.
      g_rover.requestEstop();
      g_events.add(EvLevel::Error, "control loop starved for 200 ms: E-stop requested");
    }
  }
}

void banner() {
  Serial.println();
  Serial.println("==================================================");
  Serial.printf(" URRT rover main controller  %s\n", FW_VERSION);
  const uint64_t mac = ESP.getEfuseMac();
  Serial.printf(" chip %s rev %d, %d cores, %lu MHz, eFuse MAC %012llX\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()), mac);
  Serial.printf(" pin map: %s (see docs/05_pin_maps.md)\n", ROVER_PINMAP_STATUS);
  Serial.printf(" motor driver: %s | E-stop sense: %s | battery sense: %s\n",
                MOTOR_DRIVER_TYPE == DRIVER_BTS7960 ? "BTS7960" : "MD30C", ESTOP_SENSE_FITTED ? "fitted" : "NOT fitted",
                BATTERY_SENSE_FITTED ? "fitted" : "not fitted");
  Serial.printf(" sonars fitted: FC=%d FL=%d FR=%d REAR=%d\n", SONAR_FITTED[0], SONAR_FITTED[1], SONAR_FITTED[2],
                SONAR_FITTED[3]);
  Serial.println(" motor outputs start DISABLED. Type 'help' for console commands.");
  Serial.println("==================================================");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  banner();

  RoverLock::init();
  {
    RoverLock lock(1000);
    g_rover.begin();
  }

  const bool fsOk = LittleFS.begin(false);
  if (!fsOk) g_events.add(EvLevel::Warn, "LittleFS not mounted: run 'pio run -e s3_main -t uploadfs'");

  netBegin();
  webBegin(fsOk);

  esp_task_wdt_init(CONTROL_WDT_TIMEOUT_S, true);
  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 5, nullptr, 1);
  g_events.add(EvLevel::Info, "boot complete; control loop %lu ms", static_cast<unsigned long>(CONTROL_PERIOD_MS));
}

void loop() {
  const uint32_t now = millis();
  webLoop(now);
  consoleLoop(now);
  delay(5);
}

// URRT rover — ESP32-S3 main controller.
//
// The S3 is the only authority over the drive motors. Physical safety does
// NOT depend on this firmware: the hardware E-stop opens the motor-power
// contactor directly (docs/03_electrical.md).
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>

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
}  // namespace

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // Native USB (USB-Serial/JTAG): by default a write waits up to 100 ms when
  // no monitor is reading the port, and events are logged from the control
  // loop. 1 ms makes HWCDC give up quickly and drop output until the host reads
  // again. Not 0: in Arduino-ESP32 2.0.17 its retry counter then underflows
  // and a write can hang for good.
  Serial.setTxTimeoutMs(1);
#endif
  delay(300);
  consolePrintBanner();

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

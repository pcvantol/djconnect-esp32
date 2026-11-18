#include "PowerController.h"

#include <esp_sleep.h>
#include <esp_task_wdt.h>

#include "AppLog.h"
#include "Config.h"

bool PowerController::chargerConnected(const BatteryState &battery) const {
  return battery.charging || battery.full || battery.currentMa > Config::BatteryChargeCurrentThresholdMa;
}

bool PowerController::shouldReturnToSleepAfterTimerWake(const BatteryState &battery) const {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    return false;
  }
  if (chargerConnected(battery)) {
    return false;
  }
  // Let low-battery timer wakes show the explicit charge screen instead of silently sleeping.
  return battery.percent > 20;
}

uint64_t PowerController::buttonWakeMask() const {
  // Wake only on the two buttons. Encoder phase lines can rest LOW, which would wake immediately.
  return (1ULL << Config::BoardUserKeyPin) | (1ULL << Config::EncoderButtonPin);
}

uint64_t PowerController::sleepTimerWakeUs(bool lowBatteryTimerWake) const {
  return lowBatteryTimerWake ? Config::LowBatteryWakeCheckUs : Config::UsbAttachWakePollUs;
}

void PowerController::configureWatchdog() const {
  esp_err_t result = esp_task_wdt_status(nullptr);
  if (result == ESP_ERR_INVALID_STATE) {
    const esp_task_wdt_config_t config = {
        .timeout_ms = Config::WatchdogTimeoutSeconds * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    result = esp_task_wdt_init(&config);
  } else if (result == ESP_OK || result == ESP_ERR_NOT_FOUND) {
    result = ESP_OK;
  }
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    AppLog.print("Watchdog init failed: ");
    AppLog.println(esp_err_to_name(result));
  }

  result = esp_task_wdt_add(nullptr);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    AppLog.print("Watchdog attach failed: ");
    AppLog.println(esp_err_to_name(result));
  } else {
    AppLog.print("Watchdog active: ");
    AppLog.print(Config::WatchdogTimeoutSeconds);
    AppLog.println("s");
  }
}

void PowerController::serviceWatchdog() const {
  esp_task_wdt_reset();
}

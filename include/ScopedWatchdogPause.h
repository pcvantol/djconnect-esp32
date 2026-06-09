#pragma once

#include <esp_err.h>
#include <esp_task_wdt.h>

class ScopedWatchdogPause {
public:
  ScopedWatchdogPause() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
      active_ = esp_task_wdt_delete(nullptr) == ESP_OK;
    }
  }

  ~ScopedWatchdogPause() {
    if (active_) {
      if (esp_task_wdt_add(nullptr) == ESP_OK) {
        resetIfAttached();
      }
    }
  }

  static void resetIfAttached() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
      esp_task_wdt_reset();
    }
  }

private:
  bool active_ = false;
};

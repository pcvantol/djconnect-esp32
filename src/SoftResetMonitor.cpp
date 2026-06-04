// Separate reset watcher task.
#include "SoftResetMonitor.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "AppLog.h"
#include "Config.h"

const BatteryState *SoftResetMonitor::battery_ = nullptr;

void SoftResetMonitor::begin(const BatteryState &battery) {
  battery_ = &battery;
  xTaskCreatePinnedToCore(
      resetMonitorTask,
      "reset-monitor",
      2048,
      nullptr,
      1,
      nullptr,
      0);
}

void SoftResetMonitor::resetMonitorTask(void *parameter) {
  (void)parameter;
  uint32_t topButtonPressedAt = 0;
  uint32_t comboPressedAt = 0;

  for (;;) {
    // This runs outside the main loop so a stuck Spotify request cannot block manual recovery.
    const uint32_t now = millis();
    const bool topButtonPressed = digitalRead(Config::BoardUserKeyPin) == LOW;
    const bool encoderButtonPressed = digitalRead(Config::EncoderButtonPin) == LOW;

    // Holding both physical buttons is the no-confirm recovery path for corrupted setup/token state.
    if (topButtonPressed && encoderButtonPressed) {
      if (comboPressedAt == 0) {
        comboPressedAt = now;
      } else if (now - comboPressedAt >= Config::HardResetComboHoldMs) {
        if (batteryAllowsHardReset()) {
          AppLog.println("Top + encoder held, hard reset");
          hardResetToSetupPortal();
        } else {
          AppLog.println("Hard reset blocked: battery <= 20%");
          comboPressedAt = now;
        }
      }
    } else {
      comboPressedAt = 0;
    }

    // Single top-button hold remains a softer reboot and leaves credentials intact.
    if (topButtonPressed) {
      if (topButtonPressedAt == 0) {
        topButtonPressedAt = now;
      } else if (now - topButtonPressedAt >= Config::SoftResetHoldMs) {
        if (batteryAllowsSoftReset()) {
          AppLog.println("Top button held, soft reset");
          delay(50);
          ESP.restart();
        } else {
          AppLog.println("Soft reset blocked: battery <= 10%");
          topButtonPressedAt = now;
        }
      }
    } else {
      topButtonPressedAt = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

bool SoftResetMonitor::batteryAllowsSoftReset() {
  return battery_ == nullptr || !battery_->available || battery_->percent < 0 || battery_->percent > 10;
}

bool SoftResetMonitor::batteryAllowsHardReset() {
  return battery_ == nullptr || !battery_->available || battery_->percent < 0 || battery_->percent > 20;
}

void SoftResetMonitor::hardResetToSetupPortal() {
  // Keep this self-contained so recovery still works when the app loop or Spotify client is wedged.
  Preferences spotify;
  spotify.begin("spotify", false);
  spotify.clear();
  spotify.end();

  Preferences spotifyDj;
  spotifyDj.begin("spotifydj", false);
  spotifyDj.clear();
  spotifyDj.end();

  Preferences provision;
  provision.begin("provision", false);
  provision.clear();
  provision.putBool("setup", true);
  provision.end();

  if (LittleFS.begin(true)) {
    fs::File root = LittleFS.open("/");
    fs::File file = root.openNextFile();
    while (file) {
      const String path = file.name();
      file.close();
      if (!path.isEmpty()) {
        LittleFS.remove(path);
      }
      file = root.openNextFile();
    }
    root.close();
  }

  delay(100);
  ESP.restart();
}

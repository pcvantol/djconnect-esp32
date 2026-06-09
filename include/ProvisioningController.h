#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "Config.h"
#include "I18n.h"

struct ProvisioningSettings {
  String wifiSsid;
  String wifiPassword;
  uint32_t screenOffTimeoutMs = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs = Config::DeviceSleepAfterMs;
  uint8_t screenBrightnessPercent = 100;
  uint8_t speakerVolumePercent = 100;
  Language language = Language::English;
  String themeCode = "dark";
  String logLevel = "info";
  bool volumeFeedbackEnabled = true;
  uint32_t pongHighScore = 0;
  uint32_t asteroidsHighScore = 0;
  uint32_t flyerHighScore = 0;
  bool setupModeRequested = false;
  bool helpShown = false;
};

// Owns NVS provisioning keys so the app does not need to know storage details.
class ProvisioningController {
public:
  ProvisioningSettings load() const;

  void saveDisplaySettings(
      uint32_t screenOffTimeoutMs,
      uint32_t deviceSleepTimeoutMs,
      uint8_t screenBrightnessPercent,
      const String &languageCode,
      const String &themeCode,
      const String &logLevel,
      uint8_t speakerVolumePercent,
      bool volumeFeedbackEnabled) const;

  void saveWifiCredentials(const String &ssid, const String &password) const;
  void saveSetupProvisioning(const String &ssid, const String &password) const;
  void saveGameHighScores(uint32_t pongHighScore, uint32_t asteroidsHighScore, uint32_t flyerHighScore) const;
  void requestSetupMode() const;
  void requestWifiChangeMode() const;
  void markHelpShown() const;
};

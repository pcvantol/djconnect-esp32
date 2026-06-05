#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "Config.h"
#include "I18n.h"

struct ProvisioningSettings {
  String wifiSsid;
  String wifiPassword;
  MqttSettings mqtt;
  uint32_t screenOffTimeoutMs = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs = Config::DeviceSleepAfterMs;
  uint8_t screenBrightnessPercent = 100;
  uint8_t speakerVolumePercent = 100;
  Language language = Language::English;
  String themeCode = "dark";
  bool volumeFeedbackEnabled = true;
  bool setupModeRequested = false;
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
      uint8_t speakerVolumePercent,
      bool volumeFeedbackEnabled) const;

  void saveWifiCredentials(const String &ssid, const String &password) const;
  void saveMqttSettings(const MqttSettings &settings) const;
  void saveSpotifyCredentials(const String &clientId, const String &refreshToken, const String &spotifyMarket) const;
  void saveSetupProvisioning(
      const String &ssid,
      const String &password,
      const String &clientId,
      const String &refreshToken,
      const String &spotifyMarket,
      const MqttSettings &mqttSettings) const;
  void requestSetupMode() const;
};

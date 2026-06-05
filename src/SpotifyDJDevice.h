// Home Assistant-facing SpotifyDJ device identity, pairing state, and NVS storage.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "AppState.h"
#include "Config.h"
#include "DisplayManager.h"

class SpotifyDJDevice {
public:
  void begin(const BatteryState *battery, DisplayManager *display);

  bool isPaired() const;
  bool isSpotifyConfigured() const;
  String getDeviceId() const;
  String getDeviceName() const;
  String getDeviceToken() const;
  String getHaUrl() const;
  String getFirmwareVersion() const;
  String getModel() const;
  String getPairCode() const;
  String getLocalUrl() const;
  String getSpotifyMarket() const;
  String getAssistPipelineId() const;
  MqttSettings getMqttSettings() const;

  static bool saveProvisionedLanguage(const String &languageCode);
  static String normalizedLanguageCode(const String &languageCode);

  void ensurePairingCode();
  void displayPairingCode();
  void displayPaired();

  void savePairing(const String &haUrl, const String &deviceToken);
  void saveSpotifyCredentials(const String &clientId, const String &refreshToken, const String &market);
  void saveAssistPipelineId(const String &pipelineId);
  void saveMqttSettings(const MqttSettings &settings);
  void clearHomeAssistantPairing();
  void clearPairing();
  void clearSpotifyCredentials();

  const BatteryState *battery() const;

private:
  String readString(const char *key, const String &fallback = "") const;
  void writeString(const char *key, const String &value);
  void removeKey(const char *key);
  static String macSuffix();

  const BatteryState *battery_ = nullptr;
  DisplayManager *display_ = nullptr;
  String deviceId_;
  String pairCode_;
};

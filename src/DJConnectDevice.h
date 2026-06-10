// Home Assistant-facing DJConnect device identity, pairing state, and NVS storage.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "AppState.h"
#include "Config.h"
#include "DisplayManager.h"

class DJConnectDevice {
public:
  void begin(const BatteryState *battery, DisplayManager *display);

  bool isPaired() const;
  bool isPlaybackConfigured() const;
  String getDeviceId() const;
  String getDeviceName() const;
  String getDeviceToken() const;
  String getHaLocalUrl() const;
  String getFirmwareVersion() const;
  String getModel() const;
  String getClientType() const;
  String getPairCode() const;
  String getLocalUrl() const;
  String getAssistPipelineId() const;

  static bool saveProvisionedLanguage(const String &languageCode);
  static String normalizedLanguageCode(const String &languageCode);

  void ensurePairingCode();
  void displayPairingCode();
  void displayPaired();

  void savePairing(
      const String &deviceToken,
      const String &haLocalUrl);
  void saveAssistPipelineId(const String &pipelineId);
  void clearHomeAssistantPairing();
  void clearPairing();
  void clearLegacyPlaybackCredentials();

  const BatteryState *battery() const;

private:
  String readString(const char *key, const String &fallback = "") const;
  bool writeString(const char *key, const String &value);
  void removeKey(const char *key);
  static String macSuffix();

  const BatteryState *battery_ = nullptr;
  DisplayManager *display_ = nullptr;
  String deviceId_;
  String pairCode_;
};

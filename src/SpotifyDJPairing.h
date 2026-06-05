// Home Assistant pairing and periodic status posting for SpotifyDJ.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "AppState.h"
#include "SpotifyDJDevice.h"
#include "SpotifyDJDiscovery.h"

class SpotifyDJPairing {
public:
  enum class StatusResult {
    Skipped,
    Ok,
    Failed,
    PairingInvalid,
  };

  void begin(SpotifyDJDevice &device, SpotifyDJDiscovery *discovery = nullptr);
  void setLanguageProvisionedCallback(void (*callback)(void *context, const String &languageCode), void *context);
  void setSpotifyProvisionedCallback(void (*callback)(void *context), void *context);
  bool pairWithHomeAssistant(const String &haUrl);
  StatusResult sendStatusToHA(const BatteryState &battery, bool spotifyConfigured);

private:
  void applyProvisionedLanguage(JsonVariantConst payload);
  void applyProvisionedSpotifyCredentials(JsonVariantConst payload);

  SpotifyDJDevice *device_ = nullptr;
  SpotifyDJDiscovery *discovery_ = nullptr;
  void (*languageProvisionedCallback_)(void *context, const String &languageCode) = nullptr;
  void *languageProvisionedContext_ = nullptr;
  void (*spotifyProvisionedCallback_)(void *context) = nullptr;
  void *spotifyProvisionedContext_ = nullptr;
};

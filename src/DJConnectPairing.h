// Home Assistant pairing and periodic status posting for DJConnect.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "AppState.h"
#include "DJConnectDevice.h"
#include "DJConnectDiscovery.h"

class DJConnectPairing {
public:
  enum class StatusResult {
    Skipped,
    Ok,
    Failed,
    PairingInvalid,
    VersionMismatch,
  };

  void begin(DJConnectDevice &device, DJConnectDiscovery *discovery = nullptr);
  void setLanguageProvisionedCallback(void (*callback)(void *context, const String &languageCode), void *context);
  bool pairWithHomeAssistant(const String &haUrl);
  StatusResult sendStatusToHA(
      const BatteryState &battery,
      bool playbackConfigured,
      const DeviceSettingsStatus &settings,
      const VisualState &visualState,
      const String &soundOutput);

private:
  void applyProvisionedLanguage(JsonVariantConst payload);

  DJConnectDevice *device_ = nullptr;
  DJConnectDiscovery *discovery_ = nullptr;
  void (*languageProvisionedCallback_)(void *context, const String &languageCode) = nullptr;
  void *languageProvisionedContext_ = nullptr;
};

// Local ESP HTTP API used by the Home Assistant spotify_dj integration.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"
#include "SoundManager.h"
#include "SpotifyClient.h"
#include "SpotifyDJDevice.h"
#include "SpotifyDJDiscovery.h"
#include "SpotifyDJOTA.h"
#include "SpotifyDJPairing.h"

class SpotifyDJApiServer {
public:
  using DjResponseCallback = bool (*)(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType);
  using LanguageProvisionedCallback = void (*)(void *context, const String &languageCode);
  using DeviceCommandCallback = bool (*)(void *context, const DeviceCommand &command, String &message);
  using DirectPairCallback = void (*)(void *context);

  void begin(
      WebServer &server,
      SpotifyDJDevice &device,
      SpotifyDJPairing &pairing,
      SpotifyDJDiscovery &discovery,
      SpotifyDJOTA &ota,
      SpotifyClient &spotify,
      DisplayManager &display,
      LedRing &ledRing,
      SoundManager &sound,
      const BatteryState &battery,
      const RuntimeDiagnostics &diagnostics,
      void *callbackContext = nullptr,
      DjResponseCallback djResponseCallback = nullptr,
      LanguageProvisionedCallback languageProvisionedCallback = nullptr,
      DeviceCommandCallback deviceCommandCallback = nullptr,
      DirectPairCallback directPairCallback = nullptr);
  void loop();
  bool isRunning() const;

private:
  bool validateBearerToken(bool sendError = true);
  void sendJson(int code, const String &payload);
  void handleInfo();
  void handlePairingInfo();
  void handlePair();
  void handleOta();
  void handleDjResponse();
  void handleCommand();
  void handleReboot();
  void handleForget();

  WebServer *server_ = nullptr;
  SpotifyDJDevice *device_ = nullptr;
  SpotifyDJPairing *pairing_ = nullptr;
  SpotifyDJDiscovery *discovery_ = nullptr;
  SpotifyDJOTA *ota_ = nullptr;
  SpotifyClient *spotify_ = nullptr;
  DisplayManager *display_ = nullptr;
  LedRing *ledRing_ = nullptr;
  SoundManager *sound_ = nullptr;
  const BatteryState *battery_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  void *callbackContext_ = nullptr;
  DjResponseCallback djResponseCallback_ = nullptr;
  LanguageProvisionedCallback languageProvisionedCallback_ = nullptr;
  DeviceCommandCallback deviceCommandCallback_ = nullptr;
  DirectPairCallback directPairCallback_ = nullptr;
  bool running_ = false;
};

// Local ESP HTTP API used by the Home Assistant spotify_dj integration.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"
#include "SpotifyClient.h"
#include "SpotifyDJDevice.h"
#include "SpotifyDJDiscovery.h"
#include "SpotifyDJOTA.h"
#include "SpotifyDJPairing.h"

class SpotifyDJApiServer {
public:
  using DjResponseCallback = bool (*)(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType);
  using LanguageProvisionedCallback = void (*)(void *context, const String &languageCode);

  void begin(
      WebServer &server,
      SpotifyDJDevice &device,
      SpotifyDJPairing &pairing,
      SpotifyDJDiscovery &discovery,
      SpotifyDJOTA &ota,
      SpotifyClient &spotify,
      DisplayManager &display,
      LedRing &ledRing,
      const BatteryState &battery,
      const RuntimeDiagnostics &diagnostics,
      void *callbackContext = nullptr,
      DjResponseCallback djResponseCallback = nullptr,
      LanguageProvisionedCallback languageProvisionedCallback = nullptr);
  void loop();
  bool isRunning() const;

private:
  bool validateBearerToken(bool sendError = true);
  void sendJson(int code, const String &payload);
  void handleInfo();
  void handlePairingInfo();
  void handlePair();
  void handleProvisionSpotify();
  void handleOta();
  void handleDjResponse();
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
  const BatteryState *battery_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  void *callbackContext_ = nullptr;
  DjResponseCallback djResponseCallback_ = nullptr;
  LanguageProvisionedCallback languageProvisionedCallback_ = nullptr;
  bool running_ = false;
};

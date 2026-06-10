// Local ESP HTTP API used by the Home Assistant djconnect integration.
#pragma once

#include <Arduino.h>
#include <FS.h>
using fs::FS;
#include <WebServer.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"
#include "SoundManager.h"
#include "SpotifyClient.h"
#include "DJConnectDevice.h"
#include "DJConnectDiscovery.h"
#include "DJConnectOTA.h"
#include "DJConnectPairing.h"

class DJConnectApiServer {
public:
  using DjResponseCallback = bool (*)(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType);
  using LanguageProvisionedCallback = void (*)(void *context, const String &languageCode);
  using DeviceCommandCallback = bool (*)(void *context, const DeviceCommand &command, String &message);
  using DirectPairCallback = void (*)(void *context);
  using OtaPrepareCallback = void (*)(void *context);

  void begin(
      WebServer &server,
      DJConnectDevice &device,
      DJConnectPairing &pairing,
      DJConnectDiscovery &discovery,
      DJConnectOTA &ota,
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
      DirectPairCallback directPairCallback = nullptr,
      OtaPrepareCallback otaPrepareCallback = nullptr);
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
  DJConnectDevice *device_ = nullptr;
  DJConnectPairing *pairing_ = nullptr;
  DJConnectDiscovery *discovery_ = nullptr;
  DJConnectOTA *ota_ = nullptr;
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
  OtaPrepareCallback otaPrepareCallback_ = nullptr;
  bool running_ = false;
};

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
  void begin(
      WebServer &server,
      SpotifyDJDevice &device,
      SpotifyDJPairing &pairing,
      SpotifyDJDiscovery &discovery,
      SpotifyDJOTA &ota,
      SpotifyClient &spotify,
      DisplayManager &display,
      LedRing &ledRing,
      const BatteryState &battery);
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
  bool running_ = false;
};

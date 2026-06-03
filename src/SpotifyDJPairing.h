// Home Assistant pairing and periodic status posting for SpotifyDJ.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "SpotifyDJDevice.h"
#include "SpotifyDJDiscovery.h"

class SpotifyDJPairing {
public:
  void begin(SpotifyDJDevice &device, SpotifyDJDiscovery *discovery = nullptr);
  bool pairWithHomeAssistant(const String &haUrl);
  bool sendStatusToHA(const BatteryState &battery, bool spotifyConfigured);

private:
  SpotifyDJDevice *device_ = nullptr;
  SpotifyDJDiscovery *discovery_ = nullptr;
};

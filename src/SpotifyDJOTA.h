// Firmware OTA download/install helper for Home Assistant-triggered updates.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"

struct SpotifyDJOTARequest {
  String url;
  String sha256;
  String version;
  String device;
};

class SpotifyDJOTA {
public:
  bool canUpdate(const BatteryState *battery, String &message) const;
  bool performUpdate(
      const SpotifyDJOTARequest &request,
      const BatteryState *battery,
      DisplayManager *display,
      LedRing *ledRing,
      String &message);
};

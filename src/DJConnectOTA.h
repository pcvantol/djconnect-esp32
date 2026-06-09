// Firmware OTA download/install helper for Home Assistant-triggered updates.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"
#include "SoundManager.h"

struct DJConnectOTARequest {
  String url;
  String sha256;
  String version;
  String device;
};

class DJConnectOTA {
public:
  bool canUpdate(const BatteryState *battery, String &message) const;
  bool performUpdate(
      const DJConnectOTARequest &request,
      const BatteryState *battery,
      DisplayManager *display,
      LedRing *ledRing,
      SoundManager *sound,
      String &message);
};

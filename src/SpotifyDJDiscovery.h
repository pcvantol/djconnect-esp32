// mDNS advertising for Home Assistant SpotifyDJ discovery.
#pragma once

#include <Arduino.h>

#include "SpotifyDJDevice.h"

class SpotifyDJDiscovery {
public:
  bool begin(SpotifyDJDevice &device);
  void updateTxtRecords();
  bool isRunning() const;

private:
  SpotifyDJDevice *device_ = nullptr;
  bool running_ = false;
};

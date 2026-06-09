// mDNS advertising for Home Assistant DJConnect discovery.
#pragma once

#include <Arduino.h>

#include "DJConnectDevice.h"

class DJConnectDiscovery {
public:
  bool begin(DJConnectDevice &device);
  void updateTxtRecords();
  bool isRunning() const;

private:
  DJConnectDevice *device_ = nullptr;
  bool running_ = false;
};

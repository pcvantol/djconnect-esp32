#pragma once

#include <Arduino.h>

#include "AppState.h"

// Encapsulates power policy that is independent from rendering and Spotify state.
class PowerController {
public:
  bool chargerConnected(const BatteryState &battery) const;
  bool shouldReturnToSleepAfterTimerWake(const BatteryState &battery) const;
  uint64_t buttonWakeMask() const;
  uint64_t sleepTimerWakeUs(bool lowBatteryTimerWake) const;

  void configureWatchdog() const;
  void serviceWatchdog() const;
};

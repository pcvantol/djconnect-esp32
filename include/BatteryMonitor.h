// Reads battery percentage and charge state from the BQ27220 fuel gauge over I2C.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "AppState.h"

class BatteryMonitor {
public:
  explicit BatteryMonitor(BatteryState &state) : state_(state) {}

  // Initializes I2C and performs the first fuel-gauge read.
  void begin();

  // Refreshes percentage, voltage, current, and charging/discharging flags from the BQ27220.
  bool refresh();

private:
  // Reads one little-endian 16-bit standard BQ27220 command.
  bool readWord(uint8_t command, uint16_t &value);

  // Clears stale display state when the gauge is unavailable.
  void markUnavailable();

  BatteryState &state_;
};

// Keeps the top-button soft reset independent from the main UI loop.
#pragma once

#include "AppState.h"

class SoftResetMonitor {
public:
  // Starts a tiny watchdog task so holding the top button can reset even if loop work is busy.
  void begin(const BatteryState &battery);

private:
  // Clears saved credentials/tokens/settings and marks the next boot for setup portal mode.
  static void hardResetToSetupPortal();
  static bool batteryAllowsSoftReset();
  static bool batteryAllowsHardReset();

  static void resetMonitorTask(void *parameter);
  static const BatteryState *battery_;
};

// Builds Home Assistant-facing status snapshots from app-owned runtime state.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "SettingsController.h"

namespace DeviceStatusBuilder {

DeviceSettingsStatus settingsStatus(const DeviceSettingsSnapshot &settings);
String soundOutputName(const SpotifyState &playback);

}  // namespace DeviceStatusBuilder

#include "DeviceStatusBuilder.h"

namespace DeviceStatusBuilder {

DeviceSettingsStatus settingsStatus(const DeviceSettingsSnapshot &settings) {
  return SettingsController::statusFor(settings);
}

String soundOutputName(const SpotifyState &playback) {
  return playback.deviceName.isEmpty() ? "" : playback.deviceName;
}

}  // namespace DeviceStatusBuilder

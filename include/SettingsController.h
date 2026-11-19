// Normalizes and applies device settings shared by web, HA and device menu flows.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"

struct DeviceSettingsSnapshot {
  uint8_t screenBrightnessPercent = 100;
  uint32_t screenOffTimeoutMs = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs = Config::DeviceSleepAfterMs;
  uint8_t speakerVolumePercent = 100;
  Language language = Language::English;
  String languageCode = "en";
  String themeCode = "dark";
  String logLevel = "info";
  bool wakeWordEnabled = false;
};

struct DeviceSettingsSelections {
  size_t languageSelection = 0;
  size_t themeSelection = 0;
  size_t logLevelSelection = 1;
  size_t speakerVolumeSelection = 3;
  size_t sleepTimeoutSelection = 0;
};

namespace SettingsController {

DeviceSettingsSnapshot normalize(
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    bool wakeWordEnabled);

DeviceSettingsSelections selectionsFor(const DeviceSettingsSnapshot &settings);
DeviceSettingsStatus statusFor(const DeviceSettingsSnapshot &settings);

}  // namespace SettingsController

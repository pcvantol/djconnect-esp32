#include "SettingsController.h"

#include "DJConnectMenuModel.h"

namespace {
String normalizedTheme(String themeCode) {
  themeCode.toLowerCase();
  if (themeCode == "auto" || themeCode == "light") {
    return themeCode;
  }
  return "dark";
}

String normalizedLogLevel(String logLevel) {
  logLevel.toLowerCase();
  if (logLevel == "debug" || logLevel == "warning" || logLevel == "error") {
    return logLevel;
  }
  return "info";
}
}  // namespace

namespace SettingsController {

DeviceSettingsSnapshot normalize(
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    bool wakeWordEnabled) {
  DeviceSettingsSnapshot settings;
  settings.screenBrightnessPercent = constrain(brightnessPercent, 25, 100);
  settings.screenOffTimeoutMs = constrain(offTimeoutMs, 30000UL, 240000UL);
  settings.deviceSleepTimeoutMs = constrain(sleepTimeoutMs, 300000UL, 3600000UL);
  settings.speakerVolumePercent = constrain(speakerVolumePercent, 25, 100);
  settings.language = I18n::languageFromCode(languageCode);
  settings.languageCode = languageCode;
  settings.languageCode.toLowerCase();
  if (settings.languageCode != "nl") {
    settings.languageCode = "en";
  }
  settings.themeCode = normalizedTheme(themeCode);
  settings.logLevel = normalizedLogLevel(logLevel);
  settings.wakeWordEnabled = wakeWordEnabled;
  return settings;
}

DeviceSettingsSelections selectionsFor(const DeviceSettingsSnapshot &settings) {
  DeviceSettingsSelections selections;
  selections.languageSelection = settings.language == Language::Dutch ? 1 : 0;
  selections.sleepTimeoutSelection = Logic::deepSleepTimeoutIndexForMs(settings.deviceSleepTimeoutMs);
  for (size_t index = 0; index < DJConnectMenuModel::ThemeOptionCount; index++) {
    if (settings.themeCode == DJConnectMenuModel::themeValue(index)) {
      selections.themeSelection = index;
      break;
    }
  }
  for (size_t index = 0; index < DJConnectMenuModel::LogLevelOptionCount; index++) {
    if (settings.logLevel == DJConnectMenuModel::logLevelValue(index)) {
      selections.logLevelSelection = index;
      break;
    }
  }
  for (size_t index = 0; index < DJConnectMenuModel::SpeakerVolumeOptionCount; index++) {
    if (settings.speakerVolumePercent == DJConnectMenuModel::speakerVolumeValuePercent(index)) {
      selections.speakerVolumeSelection = index;
      break;
    }
  }
  return selections;
}

DeviceSettingsStatus statusFor(const DeviceSettingsSnapshot &settings) {
  DeviceSettingsStatus status;
  status.screenBrightnessPercent = settings.screenBrightnessPercent;
  status.screenOffTimeoutMs = settings.screenOffTimeoutMs;
  status.turnOffAfterMs = settings.deviceSleepTimeoutMs;
  status.speakerVolumePercent = settings.speakerVolumePercent;
  status.language = settings.languageCode;
  status.theme = settings.themeCode;
  status.logLevel = settings.logLevel;
  status.wakeWordEnabled = settings.wakeWordEnabled;
  return status;
}

}  // namespace SettingsController

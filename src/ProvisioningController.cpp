#include "ProvisioningController.h"

#include <Preferences.h>

namespace {
String normalizedTheme(String themeCode) {
  themeCode.toLowerCase();
  if (themeCode != "auto" && themeCode != "light") {
    return "dark";
  }
  return themeCode;
}

String normalizedLogLevel(String logLevel) {
  logLevel.toLowerCase();
  if (logLevel != "debug" && logLevel != "warning" && logLevel != "error") {
    return "info";
  }
  return logLevel;
}
}  // namespace

ProvisioningSettings ProvisioningController::load() const {
  ProvisioningSettings settings;
  Preferences preferences;
  preferences.begin("provision", false);
  settings.wifiSsid = preferences.getString("ssid", "");
  settings.wifiPassword = preferences.getString("pass", "");
  settings.screenOffTimeoutMs = constrain(preferences.getUInt("screen_off_ms", Config::DisplayOffAfterMs), 30000UL, 240000UL);
  settings.deviceSleepTimeoutMs = constrain(preferences.getUInt("sleep_ms", Config::DeviceSleepAfterMs), 300000UL, 3600000UL);
  settings.screenBrightnessPercent = constrain(preferences.getUInt("screen_bright", 100), 25UL, 100UL);
  settings.language = I18n::languageFromCode(preferences.getString("language", "en"));
  settings.themeCode = normalizedTheme(preferences.getString("theme", "dark"));
  settings.logLevel = normalizedLogLevel(preferences.getString("log_level", "info"));
  settings.speakerVolumePercent = constrain(preferences.getUInt("speaker_volume", 100), 25UL, 100UL);
  settings.volumeFeedbackEnabled = preferences.getBool("volume_feedback", true);
  settings.pongHighScore = preferences.getUInt("pong_hi", 0);
  settings.asteroidsHighScore = preferences.getUInt("ast_hi", 0);
  settings.flyerHighScore = preferences.getUInt("fly_hi", 0);
  settings.setupModeRequested = preferences.getBool("setup", false);
  settings.helpShown = preferences.getBool("help_shown", false);
  preferences.end();
  return settings;
}

void ProvisioningController::saveDisplaySettings(
    uint32_t screenOffTimeoutMs,
    uint32_t deviceSleepTimeoutMs,
    uint8_t screenBrightnessPercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    uint8_t speakerVolumePercent,
    bool volumeFeedbackEnabled) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putUInt("screen_off_ms", screenOffTimeoutMs);
  provision.putUInt("sleep_ms", deviceSleepTimeoutMs);
  provision.putUInt("screen_bright", screenBrightnessPercent);
  provision.putString("language", languageCode);
  provision.putString("theme", normalizedTheme(themeCode));
  provision.putString("log_level", normalizedLogLevel(logLevel));
  provision.putUInt("speaker_volume", speakerVolumePercent);
  provision.putBool("volume_feedback", volumeFeedbackEnabled);
  provision.end();
}

void ProvisioningController::saveWifiCredentials(const String &ssid, const String &password) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putString("ssid", ssid);
  provision.putString("pass", password);
  provision.end();
}

void ProvisioningController::saveSetupProvisioning(const String &ssid, const String &password) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putString("ssid", ssid);
  provision.putString("pass", password);
  provision.putBool("setup", false);
  provision.end();
}

void ProvisioningController::saveGameHighScores(uint32_t pongHighScore, uint32_t asteroidsHighScore, uint32_t flyerHighScore) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putUInt("pong_hi", pongHighScore);
  provision.putUInt("ast_hi", asteroidsHighScore);
  provision.putUInt("fly_hi", flyerHighScore);
  provision.end();
}

void ProvisioningController::requestSetupMode() const {
  Preferences provision;
  provision.begin("provision", false);
  provision.clear();
  provision.putUInt("speaker_volume", 100);
  provision.putBool("setup", true);
  provision.end();
}

void ProvisioningController::requestWifiChangeMode() const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putBool("setup", true);
  provision.end();
}

void ProvisioningController::markHelpShown() const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putBool("help_shown", true);
  provision.end();
}

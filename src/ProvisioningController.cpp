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
}  // namespace

ProvisioningSettings ProvisioningController::load() const {
  ProvisioningSettings settings;
  Preferences preferences;
  preferences.begin("provision", false);
  settings.wifiSsid = preferences.getString("ssid", "");
  settings.wifiPassword = preferences.getString("pass", "");
  settings.mqtt.host = preferences.getString("mqtt_host", "");
  settings.mqtt.port = preferences.getUInt("mqtt_port", 1883);
  settings.mqtt.username = preferences.getString("mqtt_user", "");
  settings.mqtt.password = preferences.getString("mqtt_pass", "");
  settings.mqtt.enabled = !settings.mqtt.host.isEmpty();
  settings.screenOffTimeoutMs = constrain(preferences.getUInt("screen_off_ms", Config::DisplayOffAfterMs), 30000UL, 240000UL);
  settings.deviceSleepTimeoutMs = constrain(preferences.getUInt("sleep_ms", Config::DeviceSleepAfterMs), 300000UL, 3600000UL);
  settings.screenBrightnessPercent = constrain(preferences.getUInt("screen_bright", 100), 25UL, 100UL);
  settings.language = I18n::languageFromCode(preferences.getString("language", "en"));
  settings.themeCode = normalizedTheme(preferences.getString("theme", "dark"));
  settings.speakerVolumePercent = constrain(preferences.getUInt("speaker_volume", 100), 25UL, 100UL);
  settings.volumeFeedbackEnabled = preferences.getBool("volume_feedback", true);
  settings.setupModeRequested = preferences.getBool("setup", false);
  preferences.end();
  return settings;
}

void ProvisioningController::saveDisplaySettings(
    uint32_t screenOffTimeoutMs,
    uint32_t deviceSleepTimeoutMs,
    uint8_t screenBrightnessPercent,
    const String &languageCode,
    const String &themeCode,
    uint8_t speakerVolumePercent,
    bool volumeFeedbackEnabled) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putUInt("screen_off_ms", screenOffTimeoutMs);
  provision.putUInt("sleep_ms", deviceSleepTimeoutMs);
  provision.putUInt("screen_bright", screenBrightnessPercent);
  provision.putString("language", languageCode);
  provision.putString("theme", normalizedTheme(themeCode));
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

void ProvisioningController::saveMqttSettings(const MqttSettings &settings) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putString("mqtt_host", settings.host);
  provision.putUInt("mqtt_port", settings.port == 0 ? 1883 : settings.port);
  provision.putString("mqtt_user", settings.username);
  provision.putString("mqtt_pass", settings.password);
  provision.end();
}

void ProvisioningController::saveSpotifyCredentials(const String &clientId, const String &refreshToken, const String &spotifyMarket) const {
  Preferences provision;
  provision.begin("provision", false);
  provision.putString("sp_client", clientId);
  provision.putString("sp_refresh", refreshToken);
  provision.putString("spotify_market", spotifyMarket.isEmpty() ? "NL" : spotifyMarket);
  provision.end();

  Preferences spotifydj;
  spotifydj.begin("spotifydj", false);
  spotifydj.putString("sp_client", clientId);
  spotifydj.putString("sp_refresh", refreshToken);
  spotifydj.putString("sp_market", spotifyMarket.isEmpty() ? "NL" : spotifyMarket);
  spotifydj.end();
}

void ProvisioningController::saveSetupProvisioning(
    const String &ssid,
    const String &password,
    const String &clientId,
    const String &refreshToken,
    const String &spotifyMarket,
    const MqttSettings &mqttSettings) const {
  const bool hasSpotifyCredentials = !clientId.isEmpty() && !refreshToken.isEmpty();

  Preferences provision;
  provision.begin("provision", false);
  provision.putString("ssid", ssid);
  provision.putString("pass", password);
  if (hasSpotifyCredentials) {
    provision.putString("sp_client", clientId);
    provision.putString("sp_refresh", refreshToken);
  } else {
    provision.remove("sp_client");
    provision.remove("sp_refresh");
  }
  provision.putString("spotify_market", spotifyMarket.isEmpty() ? "NL" : spotifyMarket);
  if (!mqttSettings.host.isEmpty()) {
    provision.putString("mqtt_host", mqttSettings.host);
    provision.putUInt("mqtt_port", mqttSettings.port == 0 ? 1883 : mqttSettings.port);
    provision.putString("mqtt_user", mqttSettings.username);
    provision.putString("mqtt_pass", mqttSettings.password);
  }
  provision.putBool("setup", false);
  provision.end();

  Preferences spotifydj;
  spotifydj.begin("spotifydj", false);
  if (hasSpotifyCredentials) {
    spotifydj.putString("sp_client", clientId);
    spotifydj.putString("sp_refresh", refreshToken);
  } else {
    spotifydj.remove("sp_client");
    spotifydj.remove("sp_refresh");
  }
  spotifydj.putString("sp_market", spotifyMarket.isEmpty() ? "NL" : spotifyMarket);
  spotifydj.end();
}

void ProvisioningController::requestSetupMode() const {
  Preferences provision;
  provision.begin("provision", false);
  provision.clear();
  provision.putBool("setup", true);
  provision.end();
}

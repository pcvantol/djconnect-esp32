// Home Assistant-facing SpotifyDJ device identity, pairing state, and NVS storage.
#include "SpotifyDJDevice.h"

#include <Arduino.h>
#include <esp_system.h>

#include "AppLog.h"

namespace {
const char *Namespace = "spotifydj";
const char *DefaultDeviceName = "SpotifyDJ";
const char *Model = "lilygo-t-embed-s3";

String sixDigitCode(uint32_t value) {
  char buffer[7] = {};
  snprintf(buffer, sizeof(buffer), "%06lu", static_cast<unsigned long>(value % 1000000UL));
  return String(buffer);
}
}  // namespace

void SpotifyDJDevice::begin(const BatteryState *battery, DisplayManager *display) {
  battery_ = battery;
  display_ = display;
  deviceId_ = "spotifydj-" + macSuffix();
  if (readString("device_name").isEmpty()) {
    writeString("device_name", DefaultDeviceName);
  }

  AppLog.print("[SpotifyDJ] device_id: ");
  AppLog.println(deviceId_);
  AppLog.print("[SpotifyDJ] pairing: ");
  AppLog.println(isPaired() ? "paired" : "unpaired");
}

bool SpotifyDJDevice::isPaired() const {
  return !getDeviceToken().isEmpty();
}

bool SpotifyDJDevice::isSpotifyConfigured() const {
  return !readString("spotify_client_id").isEmpty() && !readString("spotify_refresh_token").isEmpty();
}

String SpotifyDJDevice::getDeviceId() const {
  return deviceId_;
}

String SpotifyDJDevice::getDeviceName() const {
  return readString("device_name", DefaultDeviceName);
}

String SpotifyDJDevice::getDeviceToken() const {
  return readString("device_token");
}

String SpotifyDJDevice::getHaUrl() const {
  return readString("ha_url");
}

String SpotifyDJDevice::getFirmwareVersion() const {
  return Config::AppVersionNumber;
}

String SpotifyDJDevice::getModel() const {
  return Model;
}

String SpotifyDJDevice::getPairCode() const {
  return pairCode_;
}

String SpotifyDJDevice::getLocalUrl() const {
  return "http://" + deviceId_ + ".local";
}

String SpotifyDJDevice::getSpotifyMarket() const {
  return readString("spotify_market", "NL");
}

String SpotifyDJDevice::getAssistPipelineId() const {
  return readString("assist_pipeline_id");
}

MqttSettings SpotifyDJDevice::getMqttSettings() const {
  MqttSettings settings;
  settings.host = readString("mqtt_host");
  settings.port = static_cast<uint16_t>(readString("mqtt_port", "1883").toInt());
  if (settings.port == 0) {
    settings.port = 1883;
  }
  settings.username = readString("mqtt_username");
  settings.password = readString("mqtt_password");
  settings.enabled = !settings.host.isEmpty();
  return settings;
}

void SpotifyDJDevice::ensurePairingCode() {
  if (!pairCode_.isEmpty()) {
    return;
  }
  pairCode_ = sixDigitCode(esp_random());
  AppLog.print("[SpotifyDJ] pairing code generated: ");
  AppLog.print(pairCode_);
  AppLog.print(" for ");
  AppLog.println(deviceId_);
}

void SpotifyDJDevice::displayPairingCode() {
  ensurePairingCode();
  if (display_ == nullptr) {
    AppLog.println("[SpotifyDJ] TODO displayPairingCode: display unavailable");
    return;
  }
  display_->forceBacklightPercent(100);
  if (battery_ != nullptr) {
    display_->showPairingCode(pairCode_, *battery_);
  } else {
    display_->showPairingCode(pairCode_);
  }
}

void SpotifyDJDevice::displayPaired() {
  if (display_ != nullptr) {
    if (battery_ != nullptr) {
      display_->showBootMessage("SpotifyDJ\nPaired", *battery_);
    } else {
      display_->showBootMessage("SpotifyDJ\nPaired");
    }
  }
}

void SpotifyDJDevice::savePairing(const String &haUrl, const String &deviceToken) {
  writeString("ha_url", haUrl);
  writeString("device_token", deviceToken);
  AppLog.print("[SpotifyDJ] paired with HA URL: ");
  AppLog.println(haUrl);
}

void SpotifyDJDevice::saveSpotifyCredentials(const String &clientId, const String &refreshToken, const String &market) {
  writeString("spotify_client_id", clientId);
  writeString("spotify_refresh_token", refreshToken);
  writeString("spotify_market", market.isEmpty() ? "NL" : market);
  AppLog.println("[SpotifyDJ] Spotify credentials saved to NVS");
}

void SpotifyDJDevice::saveAssistPipelineId(const String &pipelineId) {
  if (pipelineId.isEmpty()) {
    removeKey("assist_pipeline_id");
  } else {
    writeString("assist_pipeline_id", pipelineId);
  }
  AppLog.println("[SpotifyDJ] Assist pipeline setting saved");
}

void SpotifyDJDevice::saveMqttSettings(const MqttSettings &settings) {
  if (settings.host.isEmpty()) {
    return;
  }
  writeString("mqtt_host", settings.host);
  writeString("mqtt_port", String(settings.port == 0 ? 1883 : settings.port));
  writeString("mqtt_username", settings.username);
  writeString("mqtt_password", settings.password);
  AppLog.print("[SpotifyDJ] MQTT settings saved host=");
  AppLog.print(settings.host);
  AppLog.print(" port=");
  AppLog.println(settings.port == 0 ? 1883 : settings.port);
}

void SpotifyDJDevice::clearPairing() {
  clearHomeAssistantPairing();
  clearSpotifyCredentials();
}

void SpotifyDJDevice::clearHomeAssistantPairing() {
  removeKey("ha_url");
  removeKey("device_token");
  pairCode_ = "";
  AppLog.println("[SpotifyDJ] Home Assistant pairing cleared");
}

void SpotifyDJDevice::clearSpotifyCredentials() {
  removeKey("spotify_client_id");
  removeKey("spotify_refresh_token");
  removeKey("spotify_market");
  AppLog.println("[SpotifyDJ] Spotify credentials cleared");
}

const BatteryState *SpotifyDJDevice::battery() const {
  return battery_;
}

String SpotifyDJDevice::readString(const char *key, const String &fallback) const {
  Preferences preferences;
  preferences.begin(Namespace, true);
  const String value = preferences.getString(key, fallback);
  preferences.end();
  return value;
}

void SpotifyDJDevice::writeString(const char *key, const String &value) {
  Preferences preferences;
  preferences.begin(Namespace, false);
  preferences.putString(key, value);
  preferences.end();
}

void SpotifyDJDevice::removeKey(const char *key) {
  Preferences preferences;
  preferences.begin(Namespace, false);
  preferences.remove(key);
  preferences.end();
}

String SpotifyDJDevice::macSuffix() {
  char buffer[13] = {};
  snprintf(buffer, sizeof(buffer), "%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  return String(buffer);
}

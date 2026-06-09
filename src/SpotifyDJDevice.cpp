// Home Assistant-facing SpotifyDJ device identity, pairing state, and NVS storage.
#include "SpotifyDJDevice.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_system.h>

#include "AppLog.h"
#include "I18n.h"
#include "LogicHelpers.h"

namespace {
const char *Namespace = "spotifydj";
const char *DefaultDeviceName = "SpotifyDJ";
const char *Model = "lilygo-t-embed-s3";
const char *AssistPipelineKey = "assist_pipe";
const uint32_t ActiveHaUrlCacheMs = 30000;

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
  clearSpotifyCredentials();

  AppLog.print("Device ID: ");
  AppLog.println(deviceId_);
  AppLog.print("Home Assistant pairing: ");
  AppLog.println(isPaired() ? "paired" : "unpaired");
}

bool SpotifyDJDevice::isPaired() const {
  return !getDeviceToken().isEmpty();
}

bool SpotifyDJDevice::isSpotifyConfigured() const {
  return isPaired();
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

String SpotifyDJDevice::getHaLocalUrl() const {
  return readString("ha_local_url");
}

String SpotifyDJDevice::getHaRemoteUrl() const {
  return readString("ha_remote_url");
}

String SpotifyDJDevice::getActiveHaUrl() const {
  const String localUrl = getHaLocalUrl();
  const String remoteUrl = getHaRemoteUrl();
  const uint32_t now = millis();
  if (!activeHaUrl_.isEmpty() && now - activeHaUrlCheckedAt_ < ActiveHaUrlCacheMs) {
    return activeHaUrl_;
  }

  activeHaUrlCheckedAt_ = now;
  if (WiFi.status() != WL_CONNECTED) {
    activeHaUrl_ = "";
    activeHaRoute_ = "wifi_down";
    return activeHaUrl_;
  }

  if (!localUrl.isEmpty() && isUrlReachable(localUrl)) {
    activeHaUrl_ = localUrl;
    if (activeHaRoute_ != "local") {
      AppLog.println("Home Assistant route: local");
    }
    activeHaRoute_ = "local";
    return activeHaUrl_;
  }

  if (!remoteUrl.isEmpty()) {
    activeHaUrl_ = remoteUrl;
    if (activeHaRoute_ != "cloud") {
      AppLog.println("Home Assistant route: cloud");
    }
    activeHaRoute_ = "cloud";
    return activeHaUrl_;
  }

  activeHaUrl_ = "";
  activeHaRoute_ = "unavailable";
  return activeHaUrl_;
}

String SpotifyDJDevice::getFirmwareVersion() const {
  return Logic::otaComparableFirmwareVersion(Config::AppVersionNumber, Config::AppVersion);
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

String SpotifyDJDevice::getAssistPipelineId() const {
  return readString(AssistPipelineKey);
}

String SpotifyDJDevice::normalizedLanguageCode(const String &languageCode) {
  String normalized = languageCode;
  normalized.trim();
  normalized.toLowerCase();
  return normalized == "nl" || normalized == "en" ? normalized : "";
}

bool SpotifyDJDevice::saveProvisionedLanguage(const String &languageCode) {
  const String normalized = normalizedLanguageCode(languageCode);
  if (normalized.isEmpty()) {
    return false;
  }
  Preferences preferences;
  preferences.begin("provision", false);
  preferences.putString("language", normalized);
  preferences.end();
  AppLog.print("Provisioned UI language: ");
  AppLog.println(normalized);
  return true;
}

void SpotifyDJDevice::ensurePairingCode() {
  if (!pairCode_.isEmpty()) {
    return;
  }
  pairCode_ = sixDigitCode(esp_random());
  AppLog.print("Pairing code generated: ");
  AppLog.print(pairCode_);
  AppLog.print(" for ");
  AppLog.println(deviceId_);
}

void SpotifyDJDevice::displayPairingCode() {
  ensurePairingCode();
  if (display_ == nullptr) {
    AppLog.println("Pairing code display unavailable");
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
      display_->showBootMessage(I18n::text("boot_paired"), *battery_);
    } else {
      display_->showBootMessage(I18n::text("boot_paired"));
    }
  }
}

void SpotifyDJDevice::savePairing(
    const String &deviceToken,
    const String &haLocalUrl,
    const String &haRemoteUrl) {
  if (!haLocalUrl.isEmpty()) {
    writeString("ha_local_url", haLocalUrl);
  }
  if (!haRemoteUrl.isEmpty()) {
    writeString("ha_remote_url", haRemoteUrl);
  }
  writeString("device_token", deviceToken);
  activeHaUrl_ = "";
  activeHaRoute_ = "";
  activeHaUrlCheckedAt_ = 0;
  AppLog.println("Home Assistant pairing stored");
}

void SpotifyDJDevice::saveAssistPipelineId(const String &pipelineId) {
  if (pipelineId.isEmpty()) {
    removeKey(AssistPipelineKey);
  } else {
    writeString(AssistPipelineKey, pipelineId);
  }
  AppLog.println("Assist pipeline setting saved");
}

void SpotifyDJDevice::clearPairing() {
  clearHomeAssistantPairing();
  clearSpotifyCredentials();
}

void SpotifyDJDevice::clearHomeAssistantPairing() {
  removeKey("ha_local_url");
  removeKey("ha_remote_url");
  removeKey("device_token");
  activeHaUrl_ = "";
  activeHaRoute_ = "";
  activeHaUrlCheckedAt_ = 0;
  pairCode_ = "";
  AppLog.println("Home Assistant pairing cleared");
}

void SpotifyDJDevice::clearSpotifyCredentials() {
  AppLog.println("Playback credentials are managed by Home Assistant");
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

bool SpotifyDJDevice::writeString(const char *key, const String &value) {
  Preferences preferences;
  if (!preferences.begin(Namespace, false)) {
    return false;
  }
  const bool saved = preferences.putString(key, value) > 0 || value.isEmpty();
  preferences.end();
  return saved;
}

void SpotifyDJDevice::removeKey(const char *key) {
  Preferences preferences;
  preferences.begin(Namespace, false);
  preferences.remove(key);
  preferences.end();
}

bool SpotifyDJDevice::isUrlReachable(const String &url) const {
  if (url.isEmpty()) {
    return false;
  }
  HTTPClient http;
  http.setConnectTimeout(Config::HttpConnectTimeoutMs);
  http.setTimeout(1500);
  if (!http.begin(url)) {
    return false;
  }
  const int code = http.GET();
  http.end();
  return code > 0;
}

String SpotifyDJDevice::macSuffix() {
  char buffer[13] = {};
  snprintf(buffer, sizeof(buffer), "%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  return String(buffer);
}

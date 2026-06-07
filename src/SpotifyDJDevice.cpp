// Home Assistant-facing SpotifyDJ device identity, pairing state, and NVS storage.
#include "SpotifyDJDevice.h"

#include <Arduino.h>
#include <esp_system.h>

#include "AppLog.h"
#include "I18n.h"
#include "LogicHelpers.h"

namespace {
const char *Namespace = "spotifydj";
const char *DefaultDeviceName = "SpotifyDJ";
const char *Model = "lilygo-t-embed-s3";
const char *SpotifyClientIdKey = "sp_client";
const char *SpotifyRefreshKey = "sp_refresh";
const char *SpotifyMarketKey = "sp_market";
const char *AssistPipelineKey = "assist_pipe";

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

String SpotifyDJDevice::getHaUrl() const {
  return readString("ha_url");
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

String SpotifyDJDevice::getSpotifyClientId() const {
  return "";
}

String SpotifyDJDevice::getSpotifyMarket() const {
  return "";
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

void SpotifyDJDevice::savePairing(const String &haUrl, const String &deviceToken) {
  writeString("ha_url", haUrl);
  writeString("device_token", deviceToken);
  AppLog.println("Home Assistant pairing stored");
}

bool SpotifyDJDevice::saveSpotifyCredentials(const String &clientId, const String &refreshToken, const String &market) {
  (void)clientId;
  (void)refreshToken;
  (void)market;
  clearSpotifyCredentials();
  AppLog.println("Legacy playback credential save ignored");
  return false;
}

bool SpotifyDJDevice::saveSpotifyRefreshToken(const String &refreshToken) {
  (void)refreshToken;
  clearSpotifyCredentials();
  AppLog.println("Legacy playback token save ignored");
  return false;
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
  removeKey("ha_url");
  removeKey("device_token");
  pairCode_ = "";
  AppLog.println("Home Assistant pairing cleared");
}

void SpotifyDJDevice::clearSpotifyCredentials() {
  removeKey(SpotifyClientIdKey);
  removeKey(SpotifyRefreshKey);
  removeKey(SpotifyMarketKey);
  Preferences provision;
  provision.begin("provision", false);
  provision.remove("sp_client");
  provision.remove("sp_refresh");
  provision.remove("spotify_market");
  provision.end();
  AppLog.println("Legacy playback credentials cleared");
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

String SpotifyDJDevice::macSuffix() {
  char buffer[13] = {};
  snprintf(buffer, sizeof(buffer), "%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  return String(buffer);
}

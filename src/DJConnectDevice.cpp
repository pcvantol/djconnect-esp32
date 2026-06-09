// Home Assistant-facing DJConnect device identity, pairing state, and NVS storage.
#include "DJConnectDevice.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_system.h>

#include "AppLog.h"
#include "I18n.h"
#include "LogicHelpers.h"

namespace {
const char *Namespace = "djconnect";
const char *DefaultDeviceName = "DJConnect";
const char *Model = "lilygo-t-embed-s3";
const char *ClientType = "esp32";
const char *AssistPipelineKey = "assist_pipe";
const uint32_t ActiveHaUrlCacheMs = 30000;

String sixDigitCode(uint32_t value) {
  char buffer[7] = {};
  snprintf(buffer, sizeof(buffer), "%06lu", static_cast<unsigned long>(value % 1000000UL));
  return String(buffer);
}
}  // namespace

void DJConnectDevice::begin(const BatteryState *battery, DisplayManager *display) {
  battery_ = battery;
  display_ = display;
  deviceId_ = "djconnect-lilygo-" + macSuffix();
  if (readString("device_name").isEmpty()) {
    writeString("device_name", DefaultDeviceName);
  }
  clearLegacyPlaybackCredentials();

  AppLog.print("Device ID: ");
  AppLog.println(deviceId_);
  AppLog.print("Home Assistant pairing: ");
  AppLog.println(isPaired() ? "paired" : "unpaired");
}

bool DJConnectDevice::isPaired() const {
  return !getDeviceToken().isEmpty();
}

bool DJConnectDevice::isPlaybackConfigured() const {
  return isPaired();
}

String DJConnectDevice::getDeviceId() const {
  return deviceId_;
}

String DJConnectDevice::getDeviceName() const {
  return readString("device_name", DefaultDeviceName);
}

String DJConnectDevice::getDeviceToken() const {
  return readString("device_token");
}

String DJConnectDevice::getHaLocalUrl() const {
  return readString("ha_local_url");
}

String DJConnectDevice::getHaRemoteUrl() const {
  return readString("ha_remote_url");
}

String DJConnectDevice::getActiveHaUrl() const {
  const String localUrl = getHaLocalUrl();
  const String remoteUrl = getHaRemoteUrl();
  const uint32_t now = millis();
  if (!activeHaUrl_.isEmpty() && activeHaRoute_ == "local" && now - activeHaUrlCheckedAt_ < ActiveHaUrlCacheMs) {
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

void DJConnectDevice::invalidateActiveHaUrl() const {
  activeHaUrl_ = "";
  activeHaRoute_ = "";
  activeHaUrlCheckedAt_ = 0;
}

String DJConnectDevice::getFirmwareVersion() const {
  return Logic::otaComparableFirmwareVersion(Config::AppVersionNumber, Config::AppVersion);
}

String DJConnectDevice::getModel() const {
  return Model;
}

String DJConnectDevice::getClientType() const {
  return ClientType;
}

String DJConnectDevice::getPairCode() const {
  return pairCode_;
}

String DJConnectDevice::getLocalUrl() const {
  return "http://" + deviceId_ + ".local";
}

String DJConnectDevice::getAssistPipelineId() const {
  return readString(AssistPipelineKey);
}

String DJConnectDevice::normalizedLanguageCode(const String &languageCode) {
  String normalized = languageCode;
  normalized.trim();
  normalized.toLowerCase();
  return normalized == "nl" || normalized == "en" ? normalized : "";
}

bool DJConnectDevice::saveProvisionedLanguage(const String &languageCode) {
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

void DJConnectDevice::ensurePairingCode() {
  if (!pairCode_.isEmpty()) {
    return;
  }
  pairCode_ = sixDigitCode(esp_random());
  AppLog.print("Pairing code generated: ");
  AppLog.print(pairCode_);
  AppLog.print(" for ");
  AppLog.println(deviceId_);
}

void DJConnectDevice::displayPairingCode() {
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

void DJConnectDevice::displayPaired() {
  if (display_ != nullptr) {
    if (battery_ != nullptr) {
      display_->showBootMessage(I18n::text("boot_paired"), *battery_);
    } else {
      display_->showBootMessage(I18n::text("boot_paired"));
    }
  }
}

void DJConnectDevice::savePairing(
    const String &deviceToken,
    const String &haLocalUrl,
    const String &haRemoteUrl) {
  String normalizedLocalUrl = haLocalUrl;
  String normalizedRemoteUrl = haRemoteUrl;
  const bool localLooksRemote = normalizedLocalUrl.indexOf(".ui.nabu.casa") >= 0;
  if (localLooksRemote) {
    AppLog.println("Home Assistant pairing: cloud URL received as local, storing as remote");
    if (normalizedRemoteUrl.isEmpty()) {
      normalizedRemoteUrl = normalizedLocalUrl;
    }
    normalizedLocalUrl = "";
  }

  if (!normalizedLocalUrl.isEmpty()) {
    writeString("ha_local_url", normalizedLocalUrl);
  } else {
    removeKey("ha_local_url");
  }
  if (!normalizedRemoteUrl.isEmpty()) {
    writeString("ha_remote_url", normalizedRemoteUrl);
  } else {
    removeKey("ha_remote_url");
  }
  writeString("device_token", deviceToken);
  activeHaUrl_ = "";
  activeHaRoute_ = "";
  activeHaUrlCheckedAt_ = 0;
  AppLog.println("Home Assistant pairing stored");
}

void DJConnectDevice::saveAssistPipelineId(const String &pipelineId) {
  if (pipelineId.isEmpty()) {
    removeKey(AssistPipelineKey);
  } else {
    writeString(AssistPipelineKey, pipelineId);
  }
  AppLog.println("Assist pipeline setting saved");
}

void DJConnectDevice::clearPairing() {
  clearHomeAssistantPairing();
  clearLegacyPlaybackCredentials();
}

void DJConnectDevice::clearHomeAssistantPairing() {
  removeKey("ha_local_url");
  removeKey("ha_remote_url");
  removeKey("device_token");
  activeHaUrl_ = "";
  activeHaRoute_ = "";
  activeHaUrlCheckedAt_ = 0;
  pairCode_ = "";
  AppLog.println("Home Assistant pairing cleared");
}

void DJConnectDevice::clearLegacyPlaybackCredentials() {
  AppLog.println("Playback credentials are managed by Home Assistant");
}

const BatteryState *DJConnectDevice::battery() const {
  return battery_;
}

String DJConnectDevice::readString(const char *key, const String &fallback) const {
  Preferences preferences;
  preferences.begin(Namespace, true);
  const String value = preferences.getString(key, fallback);
  preferences.end();
  return value;
}

bool DJConnectDevice::writeString(const char *key, const String &value) {
  Preferences preferences;
  if (!preferences.begin(Namespace, false)) {
    return false;
  }
  const bool saved = preferences.putString(key, value) > 0 || value.isEmpty();
  preferences.end();
  return saved;
}

void DJConnectDevice::removeKey(const char *key) {
  Preferences preferences;
  preferences.begin(Namespace, false);
  preferences.remove(key);
  preferences.end();
}

bool DJConnectDevice::isUrlReachable(const String &url) const {
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

String DJConnectDevice::macSuffix() {
  char buffer[13] = {};
  snprintf(buffer, sizeof(buffer), "%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  return String(buffer);
}

// Home Assistant pairing and periodic status posting for DJConnect.
#include "DJConnectPairing.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "AppLog.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
namespace {
String joinUrl(const String &base, const char *path) {
  if (base.endsWith("/")) {
    return base.substring(0, base.length() - 1) + path;
  }
  return base + path;
}

String jsonString(JsonVariantConst payload, const char *primary, const char *fallback = nullptr) {
  String value = payload[primary] | "";
  if (value.isEmpty() && fallback != nullptr) {
    value = payload[fallback] | "";
  }
  value.trim();
  return value;
}
}  // namespace

void DJConnectPairing::begin(DJConnectDevice &device, DJConnectDiscovery *discovery) {
  device_ = &device;
  discovery_ = discovery;
}

void DJConnectPairing::setLanguageProvisionedCallback(
    void (*callback)(void *context, const String &languageCode),
    void *context) {
  languageProvisionedCallback_ = callback;
  languageProvisionedContext_ = context;
}

void DJConnectPairing::applyProvisionedLanguage(JsonVariantConst payload) {
  if (!payload.is<JsonObjectConst>()) {
    return;
  }
  String language = payload["device_language"] | "";
  if (language.isEmpty()) {
    language = payload["language"] | "";
  }
  const String normalized = DJConnectDevice::normalizedLanguageCode(language);
  if (normalized.isEmpty()) {
    return;
  }
  if (normalized == I18n::languageCode()) {
    return;
  }
  if (DJConnectDevice::saveProvisionedLanguage(normalized) &&
      languageProvisionedCallback_ != nullptr) {
    languageProvisionedCallback_(languageProvisionedContext_, normalized);
  }
}

bool DJConnectPairing::pairWithHomeAssistant(const String &haUrl) {
  if (device_ == nullptr || haUrl.isEmpty()) {
    return false;
  }
  device_->ensurePairingCode();

  JsonDocument request;
  request["device_id"] = device_->getDeviceId();
  request["device_name"] = device_->getDeviceName();
  request["pair_code"] = device_->getPairCode();
  request["firmware"] = device_->getFirmwareVersion();
  request["model"] = device_->getModel();
  request["local_url"] = device_->getLocalUrl();
  request["language"] = I18n::languageCode();

  String body;
  serializeJson(request, body);

  HTTPClient http;
  NetworkActivity activity("ha_pair", Config::HttpLongIoTimeoutMs);
  NetworkActivity::configureLongHttp(http);
  const String url = joinUrl(haUrl, "/api/djconnect/pair");
  if (!http.begin(url)) {
    AppLog.println("HA pair HTTP begin failed");
    activity.finishError("begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  const String payload = http.getString();
  http.end();
  activity.finish(code);

  AppLog.print("HA pair response: ");
  AppLog.println(code);
  if (code != 200 && code != 201) {
    return false;
  }

  JsonDocument response;
  if (deserializeJson(response, payload)) {
    AppLog.println("HA pair JSON failed");
    return false;
  }
  const bool success = response["success"] | false;
  const char *deviceToken = response["device_token"] | "";
  const char *assistPipelineId = response["assist_pipeline_id"] | "";
  if (!success || strlen(deviceToken) == 0) {
    AppLog.println("HA pair missing token");
    return false;
  }

  const String localUrl = jsonString(response.as<JsonVariantConst>(), "ha_local_url");
  const String remoteUrl = jsonString(response.as<JsonVariantConst>(), "ha_remote_url");
  device_->savePairing(deviceToken, localUrl.isEmpty() ? haUrl : localUrl, remoteUrl);
  if (strlen(assistPipelineId) > 0) {
    device_->saveAssistPipelineId(assistPipelineId);
  }
  applyProvisionedLanguage(response.as<JsonVariantConst>());
  if (discovery_ != nullptr) {
    discovery_->updateTxtRecords();
  }
  device_->displayPaired();
  return true;
}

DJConnectPairing::StatusResult DJConnectPairing::sendStatusToHA(
    const BatteryState &battery,
    bool playbackConfigured,
    const DeviceSettingsStatus &settings,
    const VisualState &visualState) {
  if (device_ == nullptr || !device_->isPaired()) {
    return StatusResult::Skipped;
  }
  const String haUrl = device_->getActiveHaUrl();
  const String token = device_->getDeviceToken();
  if (haUrl.isEmpty() || token.isEmpty()) {
    return StatusResult::Skipped;
  }

  JsonDocument request;
  request["device_id"] = device_->getDeviceId();
  request["ha_pairing_status"] = "paired";
  request["local_url"] = device_->getLocalUrl();
  request["ha_local_url"] = device_->getHaLocalUrl();
  request["ha_remote_url"] = device_->getHaRemoteUrl();
  request["ha_active_url"] = haUrl;
  request["state"] = "online";
  request["status"] = "online";
  request["ota_state"] = "idle";
  request["update_state"] = "idle";
  request["battery_percent"] = battery.percent;
  request["battery_mv"] = battery.voltageMv;
  request["charging"] = battery.charging || battery.full;
  request["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  request["firmware"] = device_->getFirmwareVersion();
  request["language"] = I18n::languageCode();
  request["device_language"] = I18n::languageCode();
  request["theme"] = settings.theme;
  request["log_level"] = settings.logLevel;
  request["brightness"] = settings.screenBrightnessPercent;
  request["screen_brightness"] = settings.screenBrightnessPercent;
  request["screen_brightness_percent"] = settings.screenBrightnessPercent;
  request["screen_dim_timeout"] = settings.screenOffTimeoutMs;
  request["screen_dim_timeout_ms"] = settings.screenOffTimeoutMs;
  request["screen_off_timeout_ms"] = settings.screenOffTimeoutMs;
  request["turn_off_after"] = settings.turnOffAfterMs;
  request["turn_off_after_ms"] = settings.turnOffAfterMs;
  request["cue_volume"] = settings.speakerVolumePercent;
  request["speaker_volume"] = settings.speakerVolumePercent;
  request["speaker_volume_percent"] = settings.speakerVolumePercent;
  request["screen_state"] = visualState.screenOn ? "on" : "off";
  request["screen_brightness_level"] = visualState.screenBrightnessLevel;
  request["led_state"] = visualState.ledOn ? "on" : "off";
  JsonObject settingsObject = request["settings"].to<JsonObject>();
  settingsObject["screen_brightness_percent"] = settings.screenBrightnessPercent;
  settingsObject["brightness"] = settings.screenBrightnessPercent;
  settingsObject["screen_dim_timeout_ms"] = settings.screenOffTimeoutMs;
  settingsObject["screen_off_timeout_ms"] = settings.screenOffTimeoutMs;
  settingsObject["turn_off_after_ms"] = settings.turnOffAfterMs;
  settingsObject["cue_volume"] = settings.speakerVolumePercent;
  settingsObject["speaker_volume_percent"] = settings.speakerVolumePercent;
  settingsObject["language"] = settings.language;
  settingsObject["theme"] = settings.theme;
  settingsObject["log_level"] = settings.logLevel;
  JsonObject screenObject = request["screen"].to<JsonObject>();
  screenObject["state"] = visualState.screenOn ? "on" : "off";
  screenObject["brightness_level"] = visualState.screenBrightnessLevel;
  JsonObject ledObject = request["led"].to<JsonObject>();
  ledObject["state"] = visualState.ledOn ? "on" : "off";
  request["playback_configured"] = playbackConfigured;
  request["free_heap"] = ESP.getFreeHeap();
  request["uptime"] = millis();
  request["uptime_ms"] = millis();

  String body;
  serializeJson(request, body);

  HTTPClient http;
  NetworkActivity activity("ha_status", Config::HttpLongIoTimeoutMs);
  NetworkActivity::configureLongHttp(http);
  const String url = joinUrl(haUrl, "/api/djconnect/status");
  if (!http.begin(url)) {
    AppLog.println("HA status HTTP begin failed");
    activity.finishError("begin failed");
    return StatusResult::Failed;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-DJConnect-Device-ID", device_->getDeviceId());
  const int code = http.POST(body);
  const String payload = http.getString();
  http.end();
  activity.finish(code);

  AppLog.print("HA status response: ");
  AppLog.println(code);
  if (Logic::isHomeAssistantPairingInvalidStatus(code)) {
    AppLog.println("HA pairing appears invalid");
    return StatusResult::PairingInvalid;
  }
  if (code >= 200 && code < 300 && !payload.isEmpty()) {
    JsonDocument response;
    if (!deserializeJson(response, payload)) {
      applyProvisionedLanguage(response.as<JsonVariantConst>());
    }
  }
  return code >= 200 && code < 300 ? StatusResult::Ok : StatusResult::Failed;
}

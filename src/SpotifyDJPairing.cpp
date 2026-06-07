// Home Assistant pairing and periodic status posting for SpotifyDJ.
#include "SpotifyDJPairing.h"

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
}  // namespace

void SpotifyDJPairing::begin(SpotifyDJDevice &device, SpotifyDJDiscovery *discovery) {
  device_ = &device;
  discovery_ = discovery;
}

void SpotifyDJPairing::setLanguageProvisionedCallback(
    void (*callback)(void *context, const String &languageCode),
    void *context) {
  languageProvisionedCallback_ = callback;
  languageProvisionedContext_ = context;
}

void SpotifyDJPairing::setSpotifyProvisionedCallback(
    void (*callback)(void *context),
    void *context) {
  spotifyProvisionedCallback_ = callback;
  spotifyProvisionedContext_ = context;
}

void SpotifyDJPairing::applyProvisionedLanguage(JsonVariantConst payload) {
  if (!payload.is<JsonObjectConst>()) {
    return;
  }
  String language = payload["device_language"] | "";
  if (language.isEmpty()) {
    language = payload["language"] | "";
  }
  const String normalized = SpotifyDJDevice::normalizedLanguageCode(language);
  if (normalized.isEmpty()) {
    return;
  }
  if (normalized == I18n::languageCode()) {
    return;
  }
  if (SpotifyDJDevice::saveProvisionedLanguage(normalized) &&
      languageProvisionedCallback_ != nullptr) {
    languageProvisionedCallback_(languageProvisionedContext_, normalized);
  }
}

void SpotifyDJPairing::applyProvisionedSpotifyCredentials(JsonVariantConst payload) {
  (void)payload;
  if (spotifyProvisionedCallback_ != nullptr) {
    spotifyProvisionedCallback_(spotifyProvisionedContext_);
  }
}

bool SpotifyDJPairing::pairWithHomeAssistant(const String &haUrl) {
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
  const String url = joinUrl(haUrl, "/api/spotify_dj/pair");
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

  device_->savePairing(haUrl, deviceToken);
  if (strlen(assistPipelineId) > 0) {
    device_->saveAssistPipelineId(assistPipelineId);
  }
  applyProvisionedLanguage(response.as<JsonVariantConst>());
  applyProvisionedSpotifyCredentials(response.as<JsonVariantConst>());
  if (discovery_ != nullptr) {
    discovery_->updateTxtRecords();
  }
  device_->displayPaired();
  return true;
}

SpotifyDJPairing::StatusResult SpotifyDJPairing::sendStatusToHA(const BatteryState &battery, bool spotifyConfigured) {
  if (device_ == nullptr || !device_->isPaired()) {
    return StatusResult::Skipped;
  }
  const String haUrl = device_->getHaUrl();
  const String token = device_->getDeviceToken();
  if (haUrl.isEmpty() || token.isEmpty()) {
    return StatusResult::Skipped;
  }

  JsonDocument request;
  request["device_id"] = device_->getDeviceId();
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
  request["spotify_configured"] = spotifyConfigured;
  request["free_heap"] = ESP.getFreeHeap();
  request["uptime_ms"] = millis();

  String body;
  serializeJson(request, body);

  HTTPClient http;
  NetworkActivity activity("ha_status", Config::HttpLongIoTimeoutMs);
  NetworkActivity::configureLongHttp(http);
  const String url = joinUrl(haUrl, "/api/spotify_dj/status");
  if (!http.begin(url)) {
    AppLog.println("HA status HTTP begin failed");
    activity.finishError("begin failed");
    return StatusResult::Failed;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());
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
      if (!spotifyConfigured) {
        applyProvisionedSpotifyCredentials(response.as<JsonVariantConst>());
      }
    }
  }
  return code >= 200 && code < 300 ? StatusResult::Ok : StatusResult::Failed;
}

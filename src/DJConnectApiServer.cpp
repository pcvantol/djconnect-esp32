// Local ESP HTTP API used by the Home Assistant djconnect integration.
#include "DJConnectApiServer.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <WiFi.h>

#include "AppLog.h"
#include "DeviceCommandParser.h"
#include "I18n.h"
#include "LogicHelpers.h"
void DJConnectApiServer::begin(
    WebServer &server,
    DJConnectDevice &device,
    DJConnectPairing &pairing,
    DJConnectDiscovery &discovery,
    DJConnectOTA &ota,
    SpotifyClient &spotify,
    DisplayManager &display,
    LedRing &ledRing,
    SoundManager &sound,
    const BatteryState &battery,
    const RuntimeDiagnostics &diagnostics,
    void *callbackContext,
    DjResponseCallback djResponseCallback,
    LanguageProvisionedCallback languageProvisionedCallback,
    DeviceCommandCallback deviceCommandCallback,
    DirectPairCallback directPairCallback) {
  if (running_) {
    return;
  }
  server_ = &server;
  device_ = &device;
  pairing_ = &pairing;
  discovery_ = &discovery;
  ota_ = &ota;
  spotify_ = &spotify;
  display_ = &display;
  ledRing_ = &ledRing;
  sound_ = &sound;
  battery_ = &battery;
  diagnostics_ = &diagnostics;
  callbackContext_ = callbackContext;
  djResponseCallback_ = djResponseCallback;
  languageProvisionedCallback_ = languageProvisionedCallback;
  deviceCommandCallback_ = deviceCommandCallback;
  directPairCallback_ = directPairCallback;

  static const char *headers[] = {"Authorization"};
  server_->collectHeaders(headers, 1);

  server_->on("/api/device/info", HTTP_GET, [this]() { handleInfo(); });
  server_->on("/api/device/pairing-info", HTTP_GET, [this]() { handlePairingInfo(); });
  server_->on("/api/device/pair", HTTP_POST, [this]() { handlePair(); });
  server_->on("/api/device/ota", HTTP_POST, [this]() { handleOta(); });
  server_->on("/api/device/dj_response", HTTP_POST, [this]() { handleDjResponse(); });
  server_->on("/api/device/command", HTTP_POST, [this]() { handleCommand(); });
  server_->on("/api/device/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_->on("/api/device/forget", HTTP_POST, [this]() { handleForget(); });
  running_ = true;
  AppLog.println("Device API routes registered");
}

void DJConnectApiServer::loop() {
  // Routes are served by the shared WebPortal WebServer instance.
}

bool DJConnectApiServer::isRunning() const {
  return running_;
}

bool DJConnectApiServer::validateBearerToken(bool sendError) {
  if (server_ == nullptr || device_ == nullptr || !device_->isPaired()) {
    if (sendError && server_ != nullptr) {
      server_->send(401, "application/json", "{\"error\":\"not paired\"}");
    }
    return false;
  }
  const String authorization = server_->header("Authorization");
  const String expected = "Bearer " + device_->getDeviceToken();
  const bool ok = authorization == expected;
  if (!ok && sendError) {
    server_->send(401, "application/json", "{\"error\":\"unauthorized\"}");
  }
  return ok;
}

void DJConnectApiServer::sendJson(int code, const String &payload) {
  server_->send(code, "application/json", payload);
}

void DJConnectApiServer::handleInfo() {
  JsonDocument doc;
  doc["device_id"] = device_->getDeviceId();
  doc["device_name"] = device_->getDeviceName();
  doc["firmware"] = device_->getFirmwareVersion();
  doc["model"] = device_->getModel();
  doc["paired"] = device_->isPaired();
  doc["playback_configured"] = device_->isPlaybackConfigured();
  doc["assist_pipeline_id"] = device_->getAssistPipelineId();
  doc["battery_percent"] = battery_ == nullptr ? -1 : battery_->percent;
  doc["battery_mv"] = battery_ == nullptr ? 0 : battery_->voltageMv;
  doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["ha_local_url"] = device_->getHaLocalUrl();
  doc["ha_remote_url"] = device_->getHaRemoteUrl();
  doc["ha_active_url"] = device_->getActiveHaUrl();
  doc["last_dj_text"] = diagnostics_ == nullptr ? "" : diagnostics_->lastDjText;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void DJConnectApiServer::handlePairingInfo() {
  device_->ensurePairingCode();
  JsonDocument doc;
  doc["device_id"] = device_->getDeviceId();
  doc["device_name"] = device_->getDeviceName();
  doc["pair_code"] = device_->getPairCode();
  doc["firmware"] = device_->getFirmwareVersion();
  doc["local_url"] = device_->getLocalUrl();
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void DJConnectApiServer::handlePair() {
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }
  const String legacyHaUrlKey = String("ha") + "_" + "url";
  if (!doc[legacyHaUrlKey].isNull()) {
    sendJson(400, "{\"error\":\"legacy_url_unsupported\",\"message\":\"use ha_local_url and/or ha_remote_url\"}");
    return;
  }
  const String deviceId = doc["device_id"] | "";
  if (deviceId.isEmpty()) {
    sendJson(400, "{\"error\":\"device id missing\",\"message\":\"device_id required\"}");
    return;
  }
  if (!Logic::isDjConnectLilygoDeviceId(deviceId.c_str()) || deviceId != device_->getDeviceId()) {
    sendJson(400, "{\"error\":\"device id invalid\",\"message\":\"device_id must match this DJConnect LilyGO device\"}");
    return;
  }
  const String haLocalUrl = doc["ha_local_url"] | "";
  const String haRemoteUrl = doc["ha_remote_url"] | "";
  const String deviceToken = doc["device_token"] | "";
  const String assistPipelineId = doc["assist_pipeline_id"] | "";
  String provisionedLanguage = doc["device_language"] | "";
  if (provisionedLanguage.isEmpty()) {
    provisionedLanguage = doc["language"] | "";
  }
  if (haLocalUrl.isEmpty() && haRemoteUrl.isEmpty()) {
    sendJson(400, "{\"error\":\"ha url missing\",\"message\":\"ha_local_url or ha_remote_url required\"}");
    return;
  }

  if (deviceToken.isEmpty()) {
    sendJson(400, "{\"error\":\"device token missing\",\"message\":\"device_token required\"}");
    return;
  }

  const bool samePairing =
      device_->isPaired() &&
      device_->getDeviceToken() == deviceToken &&
      (haLocalUrl.isEmpty() || device_->getHaLocalUrl() == haLocalUrl) &&
      (haRemoteUrl.isEmpty() || device_->getHaRemoteUrl() == haRemoteUrl);
  if (!samePairing) {
    device_->savePairing(deviceToken, haLocalUrl, haRemoteUrl);
  }
  if (!assistPipelineId.isEmpty()) {
    if (device_->getAssistPipelineId() != assistPipelineId) {
      device_->saveAssistPipelineId(assistPipelineId);
    }
  }
  const String normalizedLanguage = DJConnectDevice::normalizedLanguageCode(provisionedLanguage);
  if (!normalizedLanguage.isEmpty() && normalizedLanguage != I18n::languageCode()) {
    DJConnectDevice::saveProvisionedLanguage(normalizedLanguage);
    if (languageProvisionedCallback_ != nullptr) {
      languageProvisionedCallback_(callbackContext_, normalizedLanguage);
    }
  }
  if (samePairing) {
    AppLog.println("Home Assistant direct pairing unchanged");
  } else {
    AppLog.println("Home Assistant direct pairing stored: device_token=present");
  }
  if (!samePairing && directPairCallback_ != nullptr) {
    directPairCallback_(callbackContext_);
  }
  sendJson(200, "{\"success\":true,\"paired\":true}");
}

void DJConnectApiServer::handleOta() {
  if (!validateBearerToken()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  DJConnectOTARequest request;
  request.url = doc["url"] | "";
  request.sha256 = doc["sha256"] | "";
  request.version = doc["version"] | "";
  request.device = doc["device"] | "";

  String message;
  if (!ota_->canUpdate(battery_, message)) {
    JsonDocument response;
    response["success"] = false;
    response["message"] = message;
    String payload;
    serializeJson(response, payload);
    sendJson(409, payload);
    return;
  }

  JsonDocument response;
  response["success"] = true;
  response["message"] = "OTA started";
  response["target_version"] = request.version;
  String payload;
  serializeJson(response, payload);
  sendJson(200, payload);

  delay(100);
  if (ota_->performUpdate(request, battery_, display_, ledRing_, sound_, message)) {
    delay(500);
    ESP.restart();
  }
  AppLog.print("OTA failed after response: ");
  AppLog.println(message);
}

void DJConnectApiServer::handleDjResponse() {
  if (!validateBearerToken()) {
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  String text = doc["text"] | "";
  const String audioUrl = doc["audio_url"] | "";
  text.trim();
  if (text.isEmpty()) {
    sendJson(400, "{\"error\":\"text missing\"}");
    return;
  }

  AppLog.print("DJ response received chars=");
  AppLog.println(text.length());
  bool displayed = false;
  bool spoken = false;
  String audioType = audioUrl.isEmpty() ? "none" : "unknown";
  if (djResponseCallback_ != nullptr) {
    displayed = djResponseCallback_(callbackContext_, text, audioUrl, spoken, audioType);
  }
  if (!displayed) {
    sendJson(500, "{\"success\":false,\"error\":\"dj response display failed\"}");
    return;
  }
  JsonDocument response;
  response["success"] = true;
  response["spoken"] = spoken;
  response["displayed"] = true;
  response["audio_type"] = audioType;
  if (!spoken) {
    response["message"] = audioUrl.isEmpty()
                              ? "DJ response displayed; no TTS audio supplied"
                              : "DJ response displayed; audio could not be played";
  }
  String payload;
  serializeJson(response, payload);
  sendJson(spoken ? 200 : 202, payload);
}

void DJConnectApiServer::handleCommand() {
  if (!validateBearerToken()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }
  const DeviceCommand command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  if (command.type == DeviceCommandType::None) {
    sendJson(400, "{\"success\":false,\"error\":\"unknown command\"}");
    return;
  }
  if (deviceCommandCallback_ == nullptr) {
    sendJson(501, "{\"success\":false,\"error\":\"command handler unavailable\"}");
    return;
  }
  String message;
  const bool ok = deviceCommandCallback_(callbackContext_, command, message);
  JsonDocument response;
  response["success"] = ok;
  response["message"] = message;
  String payload;
  serializeJson(response, payload);
  sendJson(ok ? 200 : 502, payload);
}

void DJConnectApiServer::handleReboot() {
  if (!validateBearerToken()) {
    return;
  }
  sendJson(200, "{\"success\":true}");
  delay(500);
  ESP.restart();
}

void DJConnectApiServer::handleForget() {
  if (!validateBearerToken()) {
    return;
  }
  device_->clearPairing();
  discovery_->updateTxtRecords();
  sendJson(200, "{\"success\":true}");
  delay(500);
  ESP.restart();
}

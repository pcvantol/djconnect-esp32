// Local ESP HTTP API used by the Home Assistant spotify_dj integration.
#include "SpotifyDJApiServer.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <WiFi.h>

#include "AppLog.h"
#include "DeviceCommandParser.h"
void SpotifyDJApiServer::begin(
    WebServer &server,
    SpotifyDJDevice &device,
    SpotifyDJPairing &pairing,
    SpotifyDJDiscovery &discovery,
    SpotifyDJOTA &ota,
    SpotifyClient &spotify,
    DisplayManager &display,
    LedRing &ledRing,
    SoundManager &sound,
    const BatteryState &battery,
    const RuntimeDiagnostics &diagnostics,
    void *callbackContext,
    DjResponseCallback djResponseCallback,
    LanguageProvisionedCallback languageProvisionedCallback,
    DeviceCommandCallback deviceCommandCallback) {
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

void SpotifyDJApiServer::loop() {
  // Routes are served by the shared WebPortal WebServer instance.
}

bool SpotifyDJApiServer::isRunning() const {
  return running_;
}

bool SpotifyDJApiServer::validateBearerToken(bool sendError) {
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

void SpotifyDJApiServer::sendJson(int code, const String &payload) {
  server_->send(code, "application/json", payload);
}

void SpotifyDJApiServer::handleInfo() {
  JsonDocument doc;
  doc["device_id"] = device_->getDeviceId();
  doc["device_name"] = device_->getDeviceName();
  doc["firmware"] = device_->getFirmwareVersion();
  doc["model"] = device_->getModel();
  doc["paired"] = device_->isPaired();
  doc["spotify_configured"] = device_->isSpotifyConfigured();
  doc["assist_pipeline_id"] = device_->getAssistPipelineId();
  doc["battery_percent"] = battery_ == nullptr ? -1 : battery_->percent;
  doc["battery_mv"] = battery_ == nullptr ? 0 : battery_->voltageMv;
  doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["ha_url"] = device_->getHaUrl();
  doc["last_dj_text"] = diagnostics_ == nullptr ? "" : diagnostics_->lastDjText;
  String payload;
  serializeJson(doc, payload);
  sendJson(200, payload);
}

void SpotifyDJApiServer::handlePairingInfo() {
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

void SpotifyDJApiServer::handlePair() {
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }
  const String haUrl = doc["ha_url"] | "";
  const String deviceToken = doc["device_token"] | "";
  const String assistPipelineId = doc["assist_pipeline_id"] | "";
  const String deviceLanguage = doc["device_language"] | "";
  const String language = doc["language"] | "";
  if (haUrl.isEmpty()) {
    sendJson(400, "{\"error\":\"ha_url missing\"}");
    return;
  }

  // Newer HA config flows can complete pairing server-side and callback the ESP with
  // the issued device token. Accept that direct flow so the device leaves pairing mode
  // immediately after HA reports the entity as paired.
  if (!deviceToken.isEmpty()) {
    device_->savePairing(haUrl, deviceToken);
    if (!assistPipelineId.isEmpty()) {
      device_->saveAssistPipelineId(assistPipelineId);
    }
    const String provisionedLanguage = !deviceLanguage.isEmpty() ? deviceLanguage : language;
    if (languageProvisionedCallback_ != nullptr && !provisionedLanguage.isEmpty()) {
      languageProvisionedCallback_(callbackContext_, provisionedLanguage);
    }
    discovery_->updateTxtRecords();
    device_->displayPaired();
    AppLog.println("Home Assistant direct pairing stored: device_token=present");
    sendJson(200, "{\"success\":true,\"paired\":true}");
    return;
  }

  const bool ok = pairing_->pairWithHomeAssistant(haUrl);
  if (ok) {
    discovery_->updateTxtRecords();
    sendJson(200, "{\"success\":true}");
  } else {
    sendJson(502, "{\"success\":false,\"error\":\"pairing failed\"}");
  }
}

void SpotifyDJApiServer::handleOta() {
  if (!validateBearerToken()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }

  SpotifyDJOTARequest request;
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

void SpotifyDJApiServer::handleDjResponse() {
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

void SpotifyDJApiServer::handleCommand() {
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

void SpotifyDJApiServer::handleReboot() {
  if (!validateBearerToken()) {
    return;
  }
  sendJson(200, "{\"success\":true}");
  delay(500);
  ESP.restart();
}

void SpotifyDJApiServer::handleForget() {
  if (!validateBearerToken()) {
    return;
  }
  device_->clearPairing();
  discovery_->updateTxtRecords();
  sendJson(200, "{\"success\":true}");
  delay(500);
  ESP.restart();
}

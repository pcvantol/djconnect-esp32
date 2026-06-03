// Local ESP HTTP API used by the Home Assistant spotify_dj integration.
#include "SpotifyDJApiServer.h"

#include <ArduinoJson.h>
#include <Arduino.h>
#include <WiFi.h>

#include "AppLog.h"

void SpotifyDJApiServer::begin(
    WebServer &server,
    SpotifyDJDevice &device,
    SpotifyDJPairing &pairing,
    SpotifyDJDiscovery &discovery,
    SpotifyDJOTA &ota,
    SpotifyClient &spotify,
    const BatteryState &battery) {
  if (running_) {
    return;
  }
  server_ = &server;
  device_ = &device;
  pairing_ = &pairing;
  discovery_ = &discovery;
  ota_ = &ota;
  spotify_ = &spotify;
  battery_ = &battery;

  static const char *headers[] = {"Authorization"};
  server_->collectHeaders(headers, 1);

  server_->on("/api/device/info", HTTP_GET, [this]() { handleInfo(); });
  server_->on("/api/device/pairing-info", HTTP_GET, [this]() { handlePairingInfo(); });
  server_->on("/api/device/pair", HTTP_POST, [this]() { handlePair(); });
  server_->on("/api/device/provision_spotify", HTTP_POST, [this]() { handleProvisionSpotify(); });
  server_->on("/api/device/ota", HTTP_POST, [this]() { handleOta(); });
  server_->on("/api/device/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_->on("/api/device/forget", HTTP_POST, [this]() { handleForget(); });
  running_ = true;
  AppLog.println("[SpotifyDJ] device API routes registered");
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
  doc["battery_percent"] = battery_ == nullptr ? -1 : battery_->percent;
  doc["battery_mv"] = battery_ == nullptr ? 0 : battery_->voltageMv;
  doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["ha_url"] = device_->getHaUrl();
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
  if (haUrl.isEmpty()) {
    sendJson(400, "{\"error\":\"ha_url missing\"}");
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

void SpotifyDJApiServer::handleProvisionSpotify() {
  if (!validateBearerToken()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server_->arg("plain"))) {
    sendJson(400, "{\"error\":\"invalid json\"}");
    return;
  }
  const String clientId = doc["spotify_client_id"] | "";
  const String refreshToken = doc["spotify_refresh_token"] | "";
  const String market = doc["spotify_market"] | "NL";
  if (clientId.isEmpty() || refreshToken.isEmpty()) {
    sendJson(400, "{\"error\":\"spotify credentials missing\"}");
    return;
  }

  device_->saveSpotifyCredentials(clientId, refreshToken, market);
  spotify_->reloadCredentials();
  AppLog.println("[SpotifyDJ] Spotify provisioning success");
  sendJson(200, "{\"success\":true,\"spotify_configured\":true}");
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
  if (ota_->performUpdate(request, battery_, message)) {
    delay(500);
    ESP.restart();
  }
  AppLog.print("[SpotifyDJ] OTA failed after response: ");
  AppLog.println(message);
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

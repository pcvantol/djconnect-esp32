// Playback integration.
// Runtime playback is proxied through the paired Home Assistant integration so
// backend OAuth tokens do not need to live on the ESP device.
#include "SpotifyClient.h"

#include "AppLog.h"

#include <WiFi.h>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "SpotifyDJDevice.h"
#include "TextHelpers.h"

#ifndef SPOTIFY_MARKET
#define SPOTIFY_MARKET ""
#endif

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

SpotifyClient::RequestGuard::RequestGuard(SemaphoreHandle_t mutex, uint32_t waitMs)
    : mutex_(mutex) {
  // If the mutex could not be created, allow the call to proceed rather than deadlocking the app.
  if (mutex_ == nullptr) {
    locked_ = true;
    return;
  }
  locked_ = xSemaphoreTake(mutex_, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

SpotifyClient::RequestGuard::~RequestGuard() {
  if (locked_ && mutex_ != nullptr) {
    xSemaphoreGive(mutex_);
  }
}

bool SpotifyClient::RequestGuard::isLocked() const {
  return locked_;
}

void SpotifyClient::setHomeAssistantDevice(SpotifyDJDevice &device) {
  device_ = &device;
}

void SpotifyClient::begin() {
  requestMutex_ = xSemaphoreCreateMutex();
  volumeCommandQueue_ = xQueueCreate(1, sizeof(VolumeCommand));
  volumeResultQueue_ = xQueueCreate(1, sizeof(VolumeResult));

  // Volume requests run off the UI loop because HTTPS PUT latency can otherwise freeze input/display updates.
  if (volumeCommandQueue_ == nullptr || volumeResultQueue_ == nullptr) {
    AppLog.println("Volume queues failed");
  } else {
    xTaskCreatePinnedToCore(
        volumeWorkerEntry,
        "volume-worker",
        8192,
        this,
        1,
        nullptr,
        0);
  }

  refreshTokenSource_ = "Home Assistant";
}

bool SpotifyClient::authorize() {
  JsonDocument response;
  return proxyCommand("status", &response);
}

void SpotifyClient::reloadCredentials() {
  accessToken_ = "";
  accessTokenExpiresAt_ = 0;
  refreshTokenSource_ = "Home Assistant";
  tokenInvalidGrant_ = false;
}

void SpotifyClient::useCredentialsForProvisioning(const String &clientId, const String &refreshToken) {
  (void)clientId;
  (void)refreshToken;
  reloadCredentials();
  AppLog.println("Playback credentials ignored; Home Assistant owns backend credentials");
}

void SpotifyClient::clearStoredTokens() {
  reloadCredentials();
}

bool SpotifyClient::isAuthorized() const {
  return device_ != nullptr && device_->isPaired() && WiFi.status() == WL_CONNECTED && !tokenInvalidGrant_;
}

bool SpotifyClient::needsCredentialRefresh() const {
  return tokenInvalidGrant_;
}

uint32_t SpotifyClient::accessTokenExpiresInSeconds() const {
  return 0;
}

String SpotifyClient::refreshTokenSource() const {
  return refreshTokenSource_;
}

String SpotifyClient::proxyEndpoint() const {
  if (device_ == nullptr) {
    return "";
  }
  const String haUrl = device_->getHaUrl();
  if (haUrl.isEmpty()) {
    return "";
  }
  return haUrl + "/api/spotify_dj/command";
}

bool SpotifyClient::proxyCommand(const String &command, JsonDocument *response) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["command"] = command;
  return proxyRequest(request, response);
}

bool SpotifyClient::proxyCommand(const String &command, const String &value, JsonDocument *response) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["command"] = command;
  request["value"] = value;
  return proxyRequest(request, response);
}

bool SpotifyClient::proxyCommand(const String &command, int value, JsonDocument *response) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["command"] = command;
  request["value"] = value;
  return proxyRequest(request, response);
}

bool SpotifyClient::proxyRequest(JsonDocument &doc, JsonDocument *response) {
  if (device_ == nullptr || !device_->isPaired()) {
    setProxyError("Home Assistant not paired");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setProxyError("WiFi disconnected");
    return false;
  }
  const String token = device_->getDeviceToken();
  const String url = proxyEndpoint();
  if (token.isEmpty() || url.isEmpty()) {
    setProxyError("Home Assistant command unavailable");
    return false;
  }
  if (proxyCooldownActive()) {
    setProxyError("HA playback cooling down");
    return false;
  }

  String body;
  serializeJson(doc, body);

  RequestGuard guard(requestMutex_, 1000);
  if (!guard.isLocked()) {
    setProxyError("Playback proxy busy");
    return false;
  }

  HTTPClient http;
  NetworkActivity activity("ha_playback_command", Config::HttpIoTimeoutMs);
  NetworkActivity::configureDefaultHttp(http);
  if (!http.begin(url)) {
    activity.finishError("begin failed");
    setProxyError("HA playback begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());

  const String command = doc["command"] | "";
  AppLog.print("HA playback command: ");
  AppLog.println(command);
  const int code = http.POST(body);
  const String payload = http.getString();
  http.end();
  activity.finish(code);
  AppLog.print("HA playback command response: ");
  AppLog.println(code);

  if (code == 401 || code == 403 || code == 404) {
    tokenInvalidGrant_ = true;
  }
  if (code < 200 || code >= 300) {
    if (code < 0 || code >= 500) {
      lastProxyFailureAt_ = millis();
    }
    setProxyError("HA playback HTTP " + String(code));
    return false;
  }

  tokenInvalidGrant_ = false;
  lastProxyFailureAt_ = 0;
  if (response != nullptr && !payload.isEmpty()) {
    const DeserializationError error = deserializeJson(*response, payload);
    if (error) {
      setProxyError("HA playback JSON failed");
      return false;
    }
    const bool success = (*response)["success"] | true;
    if (!success) {
      setProxyError((*response)["message"] | (*response)["error"] | "HA playback failed");
      return false;
    }
  }
  state_.error = "";
  return true;
}

bool SpotifyClient::proxyCooldownActive() const {
  return lastProxyFailureAt_ != 0 && millis() - lastProxyFailureAt_ < 3500;
}

void SpotifyClient::setProxyError(const String &message) {
  state_.error = message;
  AppLog.println(message);
}

void SpotifyClient::applyPlayback(JsonVariantConst playback) {
  JsonVariantConst source = playback["playback"].isNull() ? playback : playback["playback"];
  if (source.isNull()) {
    return;
  }

  JsonVariantConst device = source["device"];
  if (!device.isNull()) {
    state_.deviceId = device["id"] | state_.deviceId;
    state_.deviceName = device["name"] | state_.deviceName;
    state_.deviceType = device["type"] | "";
    state_.supportsVolume = device["supports_volume"] | state_.supportsVolume;
    if (!device["volume_percent"].isNull()) {
      state_.volume = constrain(device["volume_percent"] | state_.volume, 0, Config::MaxSpotifyVolumePercent);
    }
  } else {
    state_.deviceId = source["device_id"] | state_.deviceId;
    state_.deviceName = source["device_name"] | state_.deviceName;
    state_.deviceType = source["device_type"] | "";
    state_.supportsVolume = source["supports_volume"] | state_.supportsVolume;
    if (!source["volume"].isNull()) {
      state_.volume = constrain(source["volume"] | state_.volume, 0, Config::MaxSpotifyVolumePercent);
    }
    if (!source["volume_percent"].isNull()) {
      state_.volume = constrain(source["volume_percent"] | state_.volume, 0, Config::MaxSpotifyVolumePercent);
    }
  }

  state_.hasPlayback = source["has_playback"] | source["hasPlayback"] | source["is_playing"].is<bool>();
  state_.isPlaying = source["is_playing"] | source["isPlaying"] | false;
  state_.shuffle = source["shuffle"] | source["shuffle_state"] | false;
  state_.repeatState = source["repeat_state"] | source["repeatState"] | "off";
  state_.progressMs = source["progress_ms"] | source["progressMs"] | 0;
  state_.durationMs = source["duration_ms"] | source["durationMs"] | 0;
  state_.progressSyncedAt = millis();
  state_.currentType = source["current_type"] | source["currently_playing_type"] | source["type"] | "";
  state_.currentUri = source["uri"] | source["current_uri"] | source["currentUri"] | "";
  state_.contextUri = source["context_uri"] | source["contextUri"] | "";
  state_.trackName = source["track_name"] | source["trackName"] | source["title"] | source["name"] | "";
  state_.artistName = source["artist_name"] | source["artistName"] | source["artist"] | "";
  state_.albumImageUrl = source["album_image_url"] | source["albumImageUrl"] | source["image_url"] | "";

  if (!state_.hasPlayback) {
    clearPlayback();
  }
}

void SpotifyClient::applyDeviceList(JsonVariantConst source, DeviceListState &devices) {
  devices.available = false;
  devices.error = "";
  devices.count = 0;
  JsonArrayConst items = source["devices"].is<JsonArrayConst>()
                             ? source["devices"].as<JsonArrayConst>()
                             : source["outputs"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (devices.count >= 8) {
      break;
    }
    SpotifyDeviceState &target = devices.devices[devices.count];
    target.id = item["id"] | item["device_id"] | "";
    target.name = item["name"] | "";
    target.type = item["type"] | "";
    target.active = item["active"] | item["is_active"] | false;
    target.supportsVolume = item["supports_volume"] | true;
    if (target.id.isEmpty() && target.name.isEmpty()) {
      continue;
    }
    devices.count++;
  }
  devices.available = true;
}

void SpotifyClient::applyQueue(JsonVariantConst source, QueueState &queue) {
  queue.available = false;
  queue.error = "";
  queue.count = 0;
  JsonArrayConst items = source["queue"].is<JsonArrayConst>()
                             ? source["queue"].as<JsonArrayConst>()
                             : source["items"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (queue.count >= 5) {
      break;
    }
    QueueItemState &target = queue.items[queue.count];
    target.title = item["title"] | item["name"] | "";
    target.subtitle = item["subtitle"] | item["artist"] | item["artists"] | "";
    if (target.title.isEmpty()) {
      continue;
    }
    queue.count++;
  }
  queue.available = true;
}

void SpotifyClient::applyPlaylists(JsonVariantConst source, PlaylistListState &playlists) {
  playlists.available = false;
  playlists.error = "";
  playlists.count = 0;
  JsonArrayConst items = source["playlists"].is<JsonArrayConst>()
                             ? source["playlists"].as<JsonArrayConst>()
                             : source["items"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (playlists.count >= 8) {
      break;
    }
    PlaylistItemState &target = playlists.items[playlists.count];
    target.name = item["name"] | "";
    target.owner = item["owner"] | "";
    target.uri = item["uri"] | item["id"] | "";
    if (target.name.isEmpty() || target.uri.isEmpty()) {
      continue;
    }
    playlists.count++;
  }
  playlists.available = true;
}

bool SpotifyClient::refreshPlayback() {
  JsonDocument response;
  if (!proxyCommand("status", &response)) {
    return false;
  }
  applyPlayback(response.as<JsonVariantConst>());
  return true;
}

bool SpotifyClient::refreshDevicesOnly() {
  DeviceListState proxiedDevices;
  if (!refreshDevices(proxiedDevices)) {
    return false;
  }
  for (size_t i = 0; i < proxiedDevices.count; ++i) {
    if (proxiedDevices.devices[i].active) {
      state_.deviceId = proxiedDevices.devices[i].id;
      state_.deviceName = proxiedDevices.devices[i].name;
      state_.deviceType = proxiedDevices.devices[i].type;
      state_.supportsVolume = proxiedDevices.devices[i].supportsVolume;
      state_.error = "";
      return true;
    }
  }
  if (proxiedDevices.count > 0) {
    state_.deviceId = proxiedDevices.devices[0].id;
    state_.deviceName = proxiedDevices.devices[0].name;
    state_.deviceType = proxiedDevices.devices[0].type;
    state_.supportsVolume = proxiedDevices.devices[0].supportsVolume;
    state_.error = "";
    return true;
  }
  state_.deviceId = "";
  state_.deviceName = "";
  state_.deviceType = "";
  state_.supportsVolume = false;
  state_.volume = -1;
  state_.error = "No playback outputs";
  return false;
}

bool SpotifyClient::refreshDevices(DeviceListState &devices) {
  JsonDocument response;
  if (!proxyCommand("devices", &response)) {
    devices.available = false;
    devices.error = state_.error;
    devices.count = 0;
    return false;
  }
  applyDeviceList(response.as<JsonVariantConst>(), devices);
  return true;
}

bool SpotifyClient::refreshQueue(QueueState &queue) {
  JsonDocument response;
  if (!proxyCommand("queue", &response)) {
    queue.available = false;
    queue.error = state_.error;
    queue.count = 0;
    return false;
  }
  applyQueue(response.as<JsonVariantConst>(), queue);
  return true;
}

bool SpotifyClient::refreshPlaylistContextQueue(QueueState &queue) {
  return refreshQueue(queue);
}

bool SpotifyClient::fillQueueFromPlaylistContext(QueueState &queue) {
  (void)queue;
  return false;
}

bool SpotifyClient::refreshPlaylists(PlaylistListState &playlists) {
  JsonDocument response;
  if (!proxyCommand("playlists", &response)) {
    playlists.available = false;
    playlists.error = state_.error;
    playlists.count = 0;
    return false;
  }
  applyPlaylists(response.as<JsonVariantConst>(), playlists);
  return true;
}

bool SpotifyClient::pausePlayback() {
  return proxyCommand("pause");
}

bool SpotifyClient::resumePlayback() {
  return proxyCommand("play");
}

bool SpotifyClient::nextTrack() {
  return proxyCommand("next");
}

bool SpotifyClient::previousTrack() {
  return proxyCommand("previous");
}

bool SpotifyClient::startLikedProxyPlaylist() {
  return proxyCommand("start_liked_proxy");
}

bool SpotifyClient::startPlaylist(const String &playlistUri) {
  return proxyCommand("start_playlist", playlistUri);
}

bool SpotifyClient::setPlayMode(const String &mode) {
  if (!proxyCommand("set_play_mode", mode)) {
    return false;
  }
  state_.shuffle = mode == "shuffle";
  state_.repeatState = mode == "repeat_once" ? "track" : (mode == "repeat_infinite" ? "context" : "off");
  return true;
}

bool SpotifyClient::transferPlayback(const String &deviceId, bool play) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["command"] = "set_output";
  request["value"] = deviceId;
  request["play"] = play;
  return proxyRequest(request);
}

bool SpotifyClient::findPlaylistUriByName(const String &playlistName, String &playlistUri) {
  playlistUri = "";
  (void)playlistName;
  state_.error = "Playlist lookup handled by Home Assistant";
  return false;
}

bool SpotifyClient::playContextUri(const String &contextUri) {
  return proxyCommand("start_playlist", contextUri);
}

bool SpotifyClient::queueVolume(int volume) {
  volume = constrain(volume, 0, Config::MaxSpotifyVolumePercent);
  if (!state_.supportsVolume) {
    state_.error = "Volume not supported";
    return false;
  }
  if (volumeCommandQueue_ == nullptr) {
    state_.error = "Volume worker missing";
    return false;
  }
  VolumeCommand queuedVolume;
  queuedVolume.volume = volume;
  xQueueOverwrite(volumeCommandQueue_, &queuedVolume);
  AppLog.print("Queued volume ");
  AppLog.println(volume);
  return true;
}

bool SpotifyClient::pollVolumeResult(VolumeResult &result) {
  if (volumeResultQueue_ == nullptr) {
    return false;
  }
  return xQueueReceive(volumeResultQueue_, &result, 0) == pdTRUE;
}

void SpotifyClient::configureTls(WiFiClientSecure &client) {
  // Direct backend HTTP is disabled; this legacy helper intentionally does not
  // install a backend CA or weaken TLS. Playback traffic goes through HA proxy.
  client.setHandshakeTimeout(Config::TlsHandshakeTimeoutMs);
  client.setTimeout(Config::HttpIoTimeoutMs);
}

void SpotifyClient::loadSpotifyCredentials() {
  reloadCredentials();
}

void SpotifyClient::saveRefreshToken(const String &newRefreshToken) {
  (void)newRefreshToken;
}

bool SpotifyClient::refreshAccessToken() {
  return authorize();
}

bool SpotifyClient::ensureAccessToken() {
  return isAuthorized();
}

int SpotifyClient::apiRequest(
    const char *method,
    const String &path,
    String *responsePayload,
    bool updatePlaybackError) {
  (void)method;
  (void)path;
  (void)responsePayload;
  if (updatePlaybackError) {
    state_.error = "Direct playback API disabled";
  }
  return -1;
}

bool SpotifyClient::sendPlayerCommand(const char *method, const String &path) {
  String payload;
  const int code = apiRequest(method, path, &payload);
  if (code == 204 || code == 200) {
    state_.error = "";
    return true;
  }

  if (state_.error.isEmpty()) {
    state_.error = "Command failed " + String(code);
  }
  return false;
}

VolumeResult SpotifyClient::sendVolumeToSpotify(const VolumeCommand &command) {
  VolumeResult result;
  result.volume = command.volume;

  AppLog.print("Volume worker: sending ");
  AppLog.println(command.volume);

  if (proxyCommand("set_volume", command.volume)) {
    result.ok = true;
    copyToBuffer(result.message, sizeof(result.message), "Volume " + String(command.volume) + "%");
    AppLog.println("Volume worker: OK");
    return result;
  }

  const String message = state_.error.isEmpty() ? "Volume failed" : state_.error;
  result.ok = false;
  copyToBuffer(result.message, sizeof(result.message), message);
  AppLog.print("Volume worker: failed ");
  AppLog.println(message);
  return result;
}

void SpotifyClient::volumeWorkerEntry(void *parameter) {
  auto *client = static_cast<SpotifyClient *>(parameter);
  VolumeCommand command;

  for (;;) {
    if (xQueueReceive(client->volumeCommandQueue_, &command, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    // Drain any stale commands so only the last knob position is sent to Spotify.
    VolumeCommand newerCommand;
    while (xQueueReceive(client->volumeCommandQueue_, &newerCommand, 0) == pdTRUE) {
      command = newerCommand;
    }

    const VolumeResult result = client->sendVolumeToSpotify(command);
    xQueueOverwrite(client->volumeResultQueue_, &result);
  }
}

void SpotifyClient::applyDevice(JsonVariantConst device) {
  state_.deviceId = device["id"] | "";
  state_.deviceName = device["name"] | "";
  state_.deviceType = device["type"] | "";
  state_.supportsVolume = device["supports_volume"] | false;

  if (!device["volume_percent"].isNull()) {
    state_.volume = constrain(device["volume_percent"] | -1, 0, Config::MaxSpotifyVolumePercent);
  } else {
    state_.volume = -1;
  }
}

void SpotifyClient::clearPlayback() {
  // A 204 response means Spotify has no active playback context, not that auth failed.
  state_.hasPlayback = false;
  state_.isPlaying = false;
  state_.trackName = "";
  state_.artistName = "";
  state_.currentType = "No active playback";
  state_.progressMs = 0;
  state_.durationMs = 0;
  state_.progressSyncedAt = millis();
}

String SpotifyClient::activeDeviceQuery() const {
  if (state_.deviceId.isEmpty()) {
    return "";
  }
  return String("?device_id=") + urlEncode(state_.deviceId);
}

String SpotifyClient::artistList(JsonArrayConst artists) const {
  String result;
  for (JsonVariantConst artist : artists) {
    const char *name = artist["name"] | "";
    if (strlen(name) == 0) {
      continue;
    }
    if (!result.isEmpty()) {
      result += ", ";
    }
    result += name;
  }
  return result;
}

String SpotifyClient::tokenErrorFromPayload(int code, const String &payload) const {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return "Token HTTP " + String(code);
  }

  const char *error = doc["error"] | "";
  const char *description = doc["error_description"] | "";

  if (strlen(error) > 0 && strlen(description) > 0) {
    return String("Token ") + error;
  }
  if (strlen(error) > 0) {
    return String("Token ") + error;
  }
  if (strlen(description) > 0) {
    return String("Token ") + description;
  }
  return "Token HTTP " + String(code);
}

String SpotifyClient::tokenErrorNameFromPayload(const String &payload) const {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return "";
  }
  return doc["error"] | "";
}

String SpotifyClient::spotifyErrorFromPayload(int code, const String &payload) const {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return "Spotify HTTP " + String(code);
  }

  const char *reason = doc["error"]["reason"] | "";
  const char *message = doc["error"]["message"] | "";

  if (strlen(reason) > 0) {
    if (strcmp(reason, "Device not found") == 0) {
      return I18n::text("device_not_found");
    }
    return String(reason);
  }
  if (strlen(message) > 0) {
    if (strcmp(message, "Device not found") == 0) {
      return I18n::text("device_not_found");
    }
    return String(message);
  }
  return "Spotify HTTP " + String(code);
}

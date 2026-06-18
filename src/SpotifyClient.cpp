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
#include "ScopedWatchdogPause.h"
#include "DJConnectDevice.h"
#include "TextHelpers.h"

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

namespace {
constexpr size_t MaxLargePlaybackPayloadBytes = 65536;

bool isLargePlaybackCommand(const String &command) {
  return command == "queue" || command == "playlists";
}

bool playbackContextSupportsOffset(const String &contextUri) {
  return contextUri.startsWith("spotify:playlist:") ||
         contextUri.startsWith("spotify:album:") ||
         contextUri.startsWith("spotify:show:");
}

JsonArrayConst firstPlaylistArray(JsonVariantConst source) {
  if (source["playlists"].is<JsonArrayConst>()) {
    return source["playlists"].as<JsonArrayConst>();
  }
  if (source["items"].is<JsonArrayConst>()) {
    return source["items"].as<JsonArrayConst>();
  }
  if (source["playlist"].is<JsonArrayConst>()) {
    return source["playlist"].as<JsonArrayConst>();
  }
  if (source["media"].is<JsonArrayConst>()) {
    return source["media"].as<JsonArrayConst>();
  }
  if (source["playlists"]["items"].is<JsonArrayConst>()) {
    return source["playlists"]["items"].as<JsonArrayConst>();
  }
  if (source["playlist"]["items"].is<JsonArrayConst>()) {
    return source["playlist"]["items"].as<JsonArrayConst>();
  }
  return JsonArrayConst();
}

JsonArrayConst playlistArrayFromResponse(JsonVariantConst source) {
  JsonArrayConst items = firstPlaylistArray(source);
  if (!items.isNull()) {
    return items;
  }

  const char *wrappers[] = {"data", "result", "response", "payload", "body", "message"};
  for (const char *wrapper : wrappers) {
    JsonVariantConst nested = source[wrapper];
    if (nested.isNull()) {
      continue;
    }
    items = firstPlaylistArray(nested);
    if (!items.isNull()) {
      return items;
    }
    JsonVariantConst nestedResult = nested["result"];
    if (!nestedResult.isNull()) {
      items = firstPlaylistArray(nestedResult);
      if (!items.isNull()) {
        return items;
      }
    }
  }

  return JsonArrayConst();
}

String playlistImageUrl(JsonVariantConst item) {
  String url =
      item["image_url"] | item["imageUrl"] | item["album_image_url"] | item["albumImageUrl"] |
      item["album_art_url"] | item["media_image_url"] | item["mediaImageUrl"] |
      item["entity_picture"] | item["thumbnail_url"] | item["thumbnailUrl"] |
      item["artwork_url"] | item["artworkUrl"] | "";
  if (!url.isEmpty()) {
    return url;
  }

  JsonArrayConst images = item["images"];
  if (!images.isNull()) {
    for (JsonVariantConst image : images) {
      url = image["url"] | "";
      if (!url.isEmpty()) {
        return url;
      }
    }
  }
  return "";
}
}  // namespace

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

void SpotifyClient::setHomeAssistantDevice(DJConnectDevice &device) {
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
}

bool SpotifyClient::authorize() {
  JsonDocument response;
  return proxyCommand("status", &response);
}

void SpotifyClient::reloadCredentials() {
  tokenInvalidGrant_ = false;
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

String SpotifyClient::proxyEndpoint() const {
  if (device_ == nullptr) {
    return "";
  }
  const String haUrl = device_->getHaLocalUrl();
  if (haUrl.isEmpty()) {
    AppLog.line("HA playback command unavailable: local HA URL missing");
    return "";
  }
  return haUrl + "/api/djconnect/command";
}

bool SpotifyClient::proxyCommand(const String &command, JsonDocument *response) {
  JsonDocument request;
  request["command"] = command;
  return proxyRequest(request, response);
}

bool SpotifyClient::proxyCommand(const String &command, const String &value, JsonDocument *response) {
  JsonDocument request;
  request["command"] = command;
  request["value"] = value;
  return proxyRequest(request, response);
}

bool SpotifyClient::proxyCommand(const String &command, int value, JsonDocument *response) {
  JsonDocument request;
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
  const String command = doc["command"] | "";
  const bool isStatusCommand = command == "status";
  if (!isStatusCommand && proxyCooldownActive()) {
    setProxyError("HA playback cooling down");
    return false;
  }

  String body;
  addCommandIdentityFields(doc);
  if (isLargePlaybackCommand(command)) {
    AppLog.line("HA playback request command=" + command + " limit=" + String(doc["limit"] | 0));
  }
  serializeJson(doc, body);

  RequestGuard guard(requestMutex_, 1000);
  if (!guard.isLocked()) {
    setProxyError("Playback proxy busy");
    return false;
  }

  String payload;
  int code = postProxyRequest(url, token, body, command, payload);
  if (code < 0 && isStatusCommand) {
    AppLog.println("HA playback status transport failed, keeping last playback state");
    return false;
  }
  if (code < 0) {
    AppLog.println("HA playback command transport failed on local route");
  }
  AppLog.line("HA playback command response: " + String(code));

  if (code < 200 || code >= 300) {
    if (!payload.isEmpty()) {
      JsonDocument errorDoc;
      const DeserializationError error = deserializeJson(errorDoc, payload);
      const char *errorKey = error ? "" : (errorDoc["error"] | "");
      if (Logic::isDjConnectInvalidClientType(errorKey)) {
        setProxyError("HA rejected payload: missing client_type=esp32");
        AppLog.println("HA rejected payload: missing client_type=esp32");
        return false;
      }
      if (Logic::isDjConnectVersionMismatch(code, errorKey)) {
        tokenInvalidGrant_ = false;
        lastProxyFailureAt_ = millis();
        setProxyError(errorDoc["message"] | "Update DJConnect firmware/integration");
        return false;
      }
    }
    if (code == 401 || code == 403 || code == 404) {
      tokenInvalidGrant_ = true;
    }
    if (!isStatusCommand && (code < 0 || code >= 500)) {
      lastProxyFailureAt_ = millis();
    }
    setProxyError("HA playback HTTP " + String(code));
    return false;
  }

  tokenInvalidGrant_ = false;
  lastProxyFailureAt_ = 0;
  if (response != nullptr) {
    if (payload.isEmpty()) {
      AppLog.line("HA playback empty response command=" + command + " http=" + String(code));
      setProxyError("HA playback empty response");
      return false;
    }
    if (isLargePlaybackCommand(command)) {
      AppLog.line("HA playback response command=" + command + " bytes=" + String(payload.length()));
    }
    if (isLargePlaybackCommand(command) && payload.length() > MaxLargePlaybackPayloadBytes) {
      AppLog.line("HA playback response too large for " + command + " len=" + String(payload.length()));
      setProxyError("HA playback response too large");
      return false;
    }
    DeserializationError error;
    {
      ScopedWatchdogPause watchdogPause;
      error = deserializeJson(*response, payload);
    }
    if (error) {
      AppLog.line("HA playback JSON failed len=" + String(payload.length()) + " body=" + payload.substring(0, 96));
      setProxyError("HA playback JSON failed");
      return false;
    }
    const bool success = (*response)["success"] | true;
    if (!success) {
      const char *errorKey = (*response)["error"] | "";
      if (Logic::isDjConnectInvalidClientType(errorKey)) {
        setProxyError("HA rejected payload: missing client_type=esp32");
        AppLog.println("HA rejected payload: missing client_type=esp32");
        return false;
      }
      const bool backendAvailable = (*response)["backend_available"] | true;
      if (!backendAvailable) {
        if (!isStatusCommand) {
          lastProxyFailureAt_ = millis();
        }
        AppLog.println("HA playback backend unavailable");
        setProxyError(I18n::text("playback_backend_unavailable_hint"));
      } else {
        setProxyError((*response)["message"] | (*response)["error"] | "HA playback failed");
      }
      return false;
    }
  }
  state_.error = "";
  return true;
}

void SpotifyClient::addCommandIdentityFields(JsonDocument &request) const {
  if (device_ == nullptr) {
    return;
  }

  request["device_id"] = device_->getDeviceId();
  request["client_type"] = device_->getClientType();
  request["payload_type"] = "command";
  request["firmware"] = device_->getFirmwareVersion();
}

int SpotifyClient::postProxyRequest(
    const String &url,
    const String &token,
    const String &body,
    const String &command,
    String &payload) {
  HTTPClient http;
  NetworkActivity activity("ha_playback_command", Config::HttpIoTimeoutMs);
  NetworkActivity::configureDefaultHttp(http);
  if (!http.begin(url)) {
    activity.finishError("begin failed");
    return -1;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-DJConnect-Device-ID", device_ == nullptr ? "" : device_->getDeviceId());

  AppLog.print("HA playback command: ");
  AppLog.println(command);
  int code = 0;
  {
    ScopedWatchdogPause watchdogPause;
    code = http.POST(body);
    if (code > 0) {
      payload = http.getString();
    }
  }
  if (code < 0) {
    AppLog.line(String("HA playback command transport: ") + http.errorToString(code) + " url=" + url);
  }
  ScopedWatchdogPause::resetIfAttached();
  http.end();
  activity.finish(code);
  return code;
}

bool SpotifyClient::proxyCooldownActive() const {
  return lastProxyFailureAt_ != 0 && millis() - lastProxyFailureAt_ < 3500;
}

void SpotifyClient::setProxyError(const String &message) {
  state_.error = message;
  AppLog.line(message);
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
    state_.deviceType = "";
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
  JsonArrayConst items;
  if (source["devices"].is<JsonArrayConst>()) {
    items = source["devices"].as<JsonArrayConst>();
  } else if (source["items"].is<JsonArrayConst>()) {
    items = source["items"].as<JsonArrayConst>();
  } else if (source["outputs"].is<JsonArrayConst>()) {
    items = source["outputs"].as<JsonArrayConst>();
  } else if (source["data"]["devices"].is<JsonArrayConst>()) {
    items = source["data"]["devices"].as<JsonArrayConst>();
  } else if (source["data"]["items"].is<JsonArrayConst>()) {
    items = source["data"]["items"].as<JsonArrayConst>();
  } else if (source["data"]["outputs"].is<JsonArrayConst>()) {
    items = source["data"]["outputs"].as<JsonArrayConst>();
  } else if (source["result"]["devices"].is<JsonArrayConst>()) {
    items = source["result"]["devices"].as<JsonArrayConst>();
  } else if (source["result"]["items"].is<JsonArrayConst>()) {
    items = source["result"]["items"].as<JsonArrayConst>();
  } else if (source["result"]["outputs"].is<JsonArrayConst>()) {
    items = source["result"]["outputs"].as<JsonArrayConst>();
  }
  for (JsonVariantConst item : items) {
    if (devices.count >= 8) {
      break;
    }
    SpotifyDeviceState &target = devices.devices[devices.count];
    target.id = item["id"] | item["device_id"] | item["value"] | "";
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
  queue.contextUri = source["context_uri"] | source["contextUri"] | source["queue_context"] |
                     source["queueContext"] | "";
  if (queue.contextUri.isEmpty()) {
    JsonVariantConst playback = source["playback"];
    if (!playback.isNull()) {
      queue.contextUri = playback["context_uri"] | playback["contextUri"] |
                         playback["queue_context"] | playback["queueContext"] | "";
    }
  }
  if (queue.contextUri.isEmpty()) {
    JsonVariantConst queuePayload = source["queue"];
    if (!queuePayload.isNull()) {
      queue.contextUri = queuePayload["context_uri"] | queuePayload["contextUri"] |
                         queuePayload["queue_context"] | queuePayload["queueContext"] | "";
    }
  }
  queue.count = 0;
  JsonArrayConst items;
  if (source["queue"].is<JsonArrayConst>()) {
    items = source["queue"].as<JsonArrayConst>();
  } else if (source["items"].is<JsonArrayConst>()) {
    items = source["items"].as<JsonArrayConst>();
  } else if (source["queue"]["items"].is<JsonArrayConst>()) {
    items = source["queue"]["items"].as<JsonArrayConst>();
  } else if (source["data"]["queue"].is<JsonArrayConst>()) {
    items = source["data"]["queue"].as<JsonArrayConst>();
  } else if (source["data"]["items"].is<JsonArrayConst>()) {
    items = source["data"]["items"].as<JsonArrayConst>();
  } else if (source["data"]["queue"]["items"].is<JsonArrayConst>()) {
    items = source["data"]["queue"]["items"].as<JsonArrayConst>();
  } else if (source["result"]["queue"].is<JsonArrayConst>()) {
    items = source["result"]["queue"].as<JsonArrayConst>();
  } else if (source["result"]["items"].is<JsonArrayConst>()) {
    items = source["result"]["items"].as<JsonArrayConst>();
  } else if (source["result"]["queue"]["items"].is<JsonArrayConst>()) {
    items = source["result"]["queue"]["items"].as<JsonArrayConst>();
  }
  for (JsonVariantConst item : items) {
    if (queue.count >= QueueState::MaxItems) {
      break;
    }
    QueueItemState candidate;
    candidate.title =
        item["title"] | item["name"] | item["track_name"] | item["trackName"] |
        item["episode_name"] | item["episodeName"] | item["media_title"] |
        item["mediaTitle"] | "";
    candidate.subtitle =
        item["subtitle"] | item["artist"] | item["artist_name"] | item["artists"] |
        item["album"] | item["show"] | item["show_name"] | item["showName"] | "";
    candidate.uri =
        item["uri"] | item["track_uri"] | item["trackUri"] | item["episode_uri"] |
        item["episodeUri"] | item["media_content_id"] | item["mediaContentId"] |
        item["content_id"] | item["contentId"] | item["id"] | item["value"] | "";
    candidate.imageUrl =
        item["album_image_url"] | item["albumImageUrl"] | item["album_art_url"] |
        item["image_url"] | item["imageUrl"] | item["media_image_url"] |
        item["entity_picture"] | item["thumbnail_url"] | "";
    if (candidate.title.isEmpty() && candidate.uri.isEmpty()) {
      continue;
    }
    if (candidate.title.isEmpty()) {
      candidate.title = candidate.uri;
    }
    bool duplicate = false;
    for (size_t existing = 0; existing < queue.count; existing++) {
      const QueueItemState &seen = queue.items[existing];
      const bool sameUri = !candidate.uri.isEmpty() && candidate.uri == seen.uri;
      const bool sameFallback =
          candidate.uri.isEmpty() && seen.uri.isEmpty() && candidate.title == seen.title &&
          candidate.subtitle == seen.subtitle;
      if (sameUri || sameFallback) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    queue.items[queue.count] = candidate;
    queue.count++;
  }
  queue.available = true;
}

void SpotifyClient::applyPlaylists(JsonVariantConst source, PlaylistListState &playlists) {
  playlists.available = false;
  playlists.error = "";
  playlists.count = 0;
  JsonArrayConst items = playlistArrayFromResponse(source);
  for (JsonVariantConst item : items) {
    if (playlists.count >= PlaylistListState::MaxItems) {
      break;
    }
    PlaylistItemState &target = playlists.items[playlists.count];
    target.name =
        item["name"] | item["title"] | item["display_title"] | item["displayTitle"] |
        item["media_title"] | item["mediaTitle"] | "";
    target.owner =
        item["owner"] | item["owner_name"] | item["description"] | item["artist"] |
        item["artists"] | item["subtitle"] | item["album"] | item["creator"] | "";
    target.uri =
        item["uri"] | item["playlist_uri"] | item["playlistUri"] | item["media_content_id"] |
        item["mediaContentId"] | item["content_id"] | item["contentId"] | item["value"] |
        item["id"] | "";
    target.imageUrl = playlistImageUrl(item);
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
  JsonVariantConst playback = response["playback"].is<JsonVariantConst>()
                                  ? response["playback"].as<JsonVariantConst>()
                                  : response.as<JsonVariantConst>();
  applyPlayback(playback);
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
  AppLog.line("Playback: devices decoded count=" + String(devices.count));
  return true;
}

bool SpotifyClient::refreshQueue(QueueState &queue) {
  JsonDocument response;
  JsonDocument request;
  request["command"] = "queue";
  request["limit"] = static_cast<int>(QueueState::MaxItems);
  if (!proxyRequest(request, &response)) {
    queue.available = false;
    queue.error = state_.error;
    queue.contextUri = "";
    queue.count = 0;
    return false;
  }
  applyQueue(response.as<JsonVariantConst>(), queue);
  AppLog.line("Playback: queue decoded count=" + String(queue.count) + " limit=" + String(QueueState::MaxItems));
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
  JsonDocument request;
  request["command"] = "playlists";
  request["limit"] = static_cast<int>(PlaylistListState::MaxItems);
  if (!proxyRequest(request, &response)) {
    playlists.available = false;
    playlists.error = state_.error;
    playlists.count = 0;
    return false;
  }
  applyPlaylists(response.as<JsonVariantConst>(), playlists);
  AppLog.line("Playback: playlists decoded count=" + String(playlists.count) +
              " limit=" + String(PlaylistListState::MaxItems));
  if (playlists.count == 0) {
    String body;
    serializeJson(response, body);
    AppLog.line("Playback: playlists empty response " + body.substring(0, 96));
  }
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

bool SpotifyClient::playQueueItem(
    const String &itemUri,
    const String &contextUri,
    const String &title,
    const String &artist,
    int index) {
  if (itemUri.isEmpty()) {
    state_.error = "Queue item unavailable";
    return false;
  }

  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["client_type"] = device_ == nullptr ? "" : device_->getClientType();
  JsonDocument response;
  request["command"] = (!contextUri.isEmpty() && playbackContextSupportsOffset(contextUri))
                           ? "play_context_at"
                           : "play_queue_item";
  JsonObject value = request["value"].to<JsonObject>();
  value["uri"] = itemUri;
  if (!title.isEmpty()) {
    value["title"] = title;
  }
  if (!artist.isEmpty()) {
    value["artist"] = artist;
  }
  if (index >= 0) {
    value["index"] = index;
  }
  if (!contextUri.isEmpty()) {
    value["context_uri"] = contextUri;
  }
  if (!contextUri.isEmpty() && playbackContextSupportsOffset(contextUri)) {
    value["offset_uri"] = itemUri;
  }
  if (!proxyRequest(request, &response)) {
    return false;
  }
  JsonVariantConst playback = response["playback"].is<JsonVariantConst>()
                                  ? response["playback"].as<JsonVariantConst>()
                                  : response.as<JsonVariantConst>();
  applyPlayback(playback);
  return true;
}

bool SpotifyClient::setShuffle(bool enabled) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["client_type"] = device_ == nullptr ? "" : device_->getClientType();
  request["command"] = "set_shuffle";
  request["value"] = enabled;
  if (!proxyRequest(request)) {
    return false;
  }
  state_.shuffle = enabled;
  return true;
}

bool SpotifyClient::setRepeatMode(const String &repeatState) {
  const String normalized = repeatState == "repeat_once" ? "track" : (repeatState == "repeat_infinite" ? "context" : repeatState);
  if (normalized != "off" && normalized != "track" && normalized != "context") {
    state_.error = "Unsupported repeat mode";
    return false;
  }
  if (!proxyCommand("set_repeat", normalized)) {
    return false;
  }
  state_.repeatState = normalized;
  return true;
}

bool SpotifyClient::transferPlayback(const String &deviceId, bool play) {
  JsonDocument request;
  request["device_id"] = device_ == nullptr ? "" : device_->getDeviceId();
  request["client_type"] = device_ == nullptr ? "" : device_->getClientType();
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
  AppLog.line("Playback: volume queued " + String(volume));
  return true;
}

bool SpotifyClient::pollVolumeResult(VolumeResult &result) {
  if (volumeResultQueue_ == nullptr) {
    return false;
  }
  return xQueueReceive(volumeResultQueue_, &result, 0) == pdTRUE;
}

VolumeResult SpotifyClient::sendVolumeToSpotify(const VolumeCommand &command) {
  VolumeResult result;
  result.volume = command.volume;

  AppLog.line("Playback: volume requested " + String(command.volume));

  if (proxyCommand("set_volume", command.volume)) {
    result.ok = true;
    copyToBuffer(result.message, sizeof(result.message), "Volume " + String(command.volume) + "%");
    AppLog.line("Playback: volume accepted " + String(command.volume));
    return result;
  }

  const String message = state_.error.isEmpty() ? "Volume failed" : state_.error;
  result.ok = false;
  copyToBuffer(result.message, sizeof(result.message), message);
  AppLog.line("Playback: volume failed: " + message);
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

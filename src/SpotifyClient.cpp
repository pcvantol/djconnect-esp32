// Spotify Web API integration.
// The firmware uses the Web API only; it controls an existing Spotify Connect player.
#include "SpotifyClient.h"

#include "AppLog.h"

#include <WiFi.h>

#include "Config.h"
#include "TextHelpers.h"

#ifndef SPOTIFY_MARKET
#define SPOTIFY_MARKET ""
#endif

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

// Spotify currently chains to DigiCert Global Root G2; keep TLS secure unless debugging demands otherwise.
static const char SpotifyRootCa[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

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

  loadSpotifyCredentials();
}

bool SpotifyClient::authorize() {
  return refreshAccessToken();
}

void SpotifyClient::reloadCredentials() {
  accessToken_ = "";
  accessTokenExpiresAt_ = 0;
  loadSpotifyCredentials();
}

void SpotifyClient::useCredentialsForProvisioning(const String &clientId, const String &refreshToken) {
  clientId_ = clientId;
  refreshToken_ = refreshToken;
  refreshTokenFromStorage_ = false;
  refreshTokenSource_ = "Portal";
  accessToken_ = "";
  accessTokenExpiresAt_ = 0;
}

void SpotifyClient::clearStoredTokens() {
  preferences_.remove("refresh");
  clientId_ = "";
  refreshToken_ = "";
  accessToken_ = "";
  accessTokenExpiresAt_ = 0;
  refreshTokenFromStorage_ = false;
  refreshTokenSource_ = "Cleared";
}

bool SpotifyClient::isAuthorized() const {
  return !accessToken_.isEmpty() && static_cast<int32_t>(millis() - accessTokenExpiresAt_) < 0;
}

uint32_t SpotifyClient::accessTokenExpiresInSeconds() const {
  if (!isAuthorized()) {
    return 0;
  }
  return (accessTokenExpiresAt_ - millis()) / 1000UL;
}

String SpotifyClient::refreshTokenSource() const {
  return refreshTokenSource_;
}

bool SpotifyClient::refreshPlayback() {
  String path = "/me/player";
  const String market = market_.isEmpty() ? String(SPOTIFY_MARKET) : market_;
  if (!market.isEmpty()) {
    path += "?market=";
    path += urlEncode(market);
  }

  String payload;
  const int code = apiRequest("GET", path, &payload);
  if (code == 204) {
    clearPlayback();
    refreshDevicesOnly();
    return true;
  }

  if (code != 200) {
    return false;
  }

  // ArduinoJson filters keep memory use low on the ESP32-S3 by parsing only fields shown in the UI.
  JsonDocument filter;
  filter["device"]["id"] = true;
  filter["device"]["name"] = true;
  filter["device"]["type"] = true;
  filter["device"]["supports_volume"] = true;
  filter["device"]["volume_percent"] = true;
  filter["is_playing"] = true;
  filter["shuffle_state"] = true;
  filter["repeat_state"] = true;
  filter["progress_ms"] = true;
  filter["currently_playing_type"] = true;
  filter["item"]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["type"] = true;
  filter["item"]["album"]["images"][0]["url"] = true;
  filter["item"]["album"]["images"][0]["width"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["show"]["name"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
    state_.error = "Playback JSON failed";
    return false;
  }

  applyDevice(doc["device"]);
  state_.hasPlayback = true;
  state_.isPlaying = doc["is_playing"] | false;
  state_.shuffle = doc["shuffle_state"] | false;
  state_.repeatState = doc["repeat_state"] | "off";
  state_.progressMs = doc["progress_ms"] | 0;
  state_.progressSyncedAt = millis();
  state_.currentType = doc["currently_playing_type"] | "";

  JsonVariantConst item = doc["item"];
  if (item.isNull()) {
    state_.trackName = state_.isPlaying ? "Playing" : "Paused";
    state_.artistName = "";
    state_.durationMs = 0;
    state_.albumImageUrl = "";
  } else {
    state_.trackName = item["name"] | "";
    state_.durationMs = item["duration_ms"] | 0;
    state_.albumImageUrl = "";

    const char *itemType = item["type"] | "";
    if (strcmp(itemType, "episode") == 0) {
      state_.artistName = item["show"]["name"] | "";
    } else {
      state_.artistName = artistList(item["artists"].as<JsonArrayConst>());

      JsonArrayConst images = item["album"]["images"].as<JsonArrayConst>();
      int bestWidth = 0;
      for (JsonVariantConst image : images) {
        const char *url = image["url"] | "";
        const int width = image["width"] | 0;
        if (strlen(url) == 0) {
          continue;
        }
        if ((width >= 160 && (bestWidth == 0 || width < bestWidth)) || bestWidth < 160) {
          state_.albumImageUrl = url;
          bestWidth = width;
        }
      }
    }
  }

  state_.error = "";
  return true;
}

bool SpotifyClient::refreshDevicesOnly() {
  String payload;
  const int code = apiRequest("GET", "/me/player/devices", &payload);
  if (code != 200) {
    return false;
  }

  JsonDocument filter;
  filter["devices"][0]["id"] = true;
  filter["devices"][0]["name"] = true;
  filter["devices"][0]["type"] = true;
  filter["devices"][0]["is_active"] = true;
  filter["devices"][0]["supports_volume"] = true;
  filter["devices"][0]["volume_percent"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
    state_.error = "Devices JSON failed";
    return false;
  }

  JsonArrayConst devices = doc["devices"].as<JsonArrayConst>();
  // Prefer the active device; fall back to the first known device so the screen still names an output.
  for (JsonVariantConst device : devices) {
    if (device["is_active"] | false) {
      applyDevice(device);
      state_.error = "";
      return true;
    }
  }

  for (JsonVariantConst device : devices) {
    applyDevice(device);
    state_.error = "";
    return true;
  }

  state_.deviceId = "";
  state_.deviceName = "";
  state_.deviceType = "";
  state_.supportsVolume = false;
  state_.volume = -1;
  state_.error = "No Spotify devices";
  return false;
}

bool SpotifyClient::refreshDevices(DeviceListState &devices) {
  devices.available = false;
  devices.error = "";
  devices.count = 0;

  String payload;
  const int code = apiRequest("GET", "/me/player/devices", &payload);
  if (code != 200) {
    devices.error = state_.error.isEmpty() ? "Devices failed " + String(code) : state_.error;
    return false;
  }

  JsonDocument filter;
  filter["devices"][0]["id"] = true;
  filter["devices"][0]["name"] = true;
  filter["devices"][0]["type"] = true;
  filter["devices"][0]["is_active"] = true;
  filter["devices"][0]["supports_volume"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
    devices.error = "Devices JSON failed";
    return false;
  }

  JsonArrayConst items = doc["devices"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (devices.count >= 8) {
      break;
    }
    SpotifyDeviceState &target = devices.devices[devices.count];
    target.id = item["id"] | "";
    target.name = item["name"] | "";
    target.type = item["type"] | "";
    target.active = item["is_active"] | false;
    target.supportsVolume = item["supports_volume"] | false;
    if (target.id.isEmpty() || target.name.isEmpty()) {
      continue;
    }
    devices.count++;
  }

  devices.available = true;
  return true;
}

bool SpotifyClient::refreshQueue(QueueState &queue) {
  queue.available = false;
  queue.error = "";
  queue.count = 0;

  String payload;
  const int code = apiRequest("GET", "/me/player/queue", &payload);
  if (code == 204 || (code == 200 && payload.isEmpty())) {
    // Spotify can return no useful queue body when there is no active queue context.
    // Treat that as an empty queue so the UI does not show a scary JSON parser error.
    queue.available = true;
    AppLog.println("Queue response empty");
    return true;
  }
  if (code != 200) {
    queue.error = state_.error.isEmpty() ? "Queue failed " + String(code) : state_.error;
    return false;
  }

  JsonDocument filter;
  filter["queue"][0]["name"] = true;
  filter["queue"][0]["type"] = true;
  filter["queue"][0]["artists"][0]["name"] = true;
  filter["queue"][0]["show"]["name"] = true;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(
      doc,
      payload,
      DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(32));
  if (error) {
    queue.error = String("Queue JSON ") + error.c_str();
    AppLog.print("Queue JSON parse failed: ");
    AppLog.print(error.c_str());
    AppLog.print(" bytes=");
    AppLog.println(payload.length());
    return false;
  }

  JsonArrayConst items = doc["queue"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (queue.count >= 5) {
      break;
    }

    QueueItemState &target = queue.items[queue.count];
    target.title = item["name"] | "";
    const char *type = item["type"] | "";
    if (strcmp(type, "episode") == 0) {
      target.subtitle = item["show"]["name"] | "";
    } else {
      target.subtitle = artistList(item["artists"].as<JsonArrayConst>());
    }
    if (target.title.isEmpty()) {
      continue;
    }
    queue.count++;
  }

  queue.available = true;
  return true;
}

bool SpotifyClient::refreshPlaylists(PlaylistListState &playlists) {
  playlists.available = false;
  playlists.error = "";
  playlists.count = 0;

  AppLog.println("Spotify: loading playlists");
  String payload;
  const int code = apiRequest("GET", "/me/playlists?limit=8", &payload);
  if (code != 200) {
    playlists.error = state_.error.isEmpty() ? "Playlists failed " + String(code) : state_.error;
    AppLog.print("Spotify: playlists failed HTTP ");
    AppLog.println(code);
    return false;
  }
  if (payload.isEmpty()) {
    playlists.available = true;
    AppLog.println("Spotify: playlists empty response");
    return true;
  }

  JsonDocument filter;
  filter["items"][0]["name"] = true;
  filter["items"][0]["uri"] = true;
  filter["items"][0]["owner"]["display_name"] = true;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    playlists.error = String("Playlists JSON ") + error.c_str();
    AppLog.print("Playlists JSON parse failed: ");
    AppLog.print(error.c_str());
    AppLog.print(" bytes=");
    AppLog.println(payload.length());
    return false;
  }

  JsonArrayConst items = doc["items"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    if (playlists.count >= 8) {
      break;
    }
    PlaylistItemState &target = playlists.items[playlists.count];
    target.name = item["name"] | "";
    target.owner = item["owner"]["display_name"] | "";
    target.uri = item["uri"] | "";
    if (target.name.isEmpty() || target.uri.isEmpty()) {
      continue;
    }
    playlists.count++;
  }

  playlists.available = true;
  AppLog.print("Spotify: playlists loaded count=");
  AppLog.println(playlists.count);
  return true;
}

bool SpotifyClient::pausePlayback() {
  return sendPlayerCommand("PUT", "/me/player/pause" + activeDeviceQuery());
}

bool SpotifyClient::resumePlayback() {
  return sendPlayerCommand("PUT", "/me/player/play" + activeDeviceQuery());
}

bool SpotifyClient::nextTrack() {
  return sendPlayerCommand("POST", "/me/player/next" + activeDeviceQuery());
}

bool SpotifyClient::previousTrack() {
  return sendPlayerCommand("POST", "/me/player/previous" + activeDeviceQuery());
}

bool SpotifyClient::startLikedProxyPlaylist() {
  if (state_.deviceId.isEmpty()) {
    refreshDevicesOnly();
  }
  if (state_.deviceId.isEmpty()) {
    state_.error = "Select a Spotify output first";
    AppLog.println("Spotify: cannot start Liked Proxy, no output selected");
    return false;
  }

  String playlistUri;
  if (!findPlaylistUriByName(Config::SpotifyLikedProxyPlaylistName, playlistUri)) {
    AppLog.println("Spotify: Liked Proxy playlist not found");
    return false;
  }
  return playContextUri(playlistUri);
}

bool SpotifyClient::startPlaylist(const String &playlistUri) {
  if (state_.deviceId.isEmpty()) {
    refreshDevicesOnly();
  }
  if (state_.deviceId.isEmpty()) {
    state_.error = "Select a Spotify output first";
    AppLog.println("Spotify: cannot start playlist, no output selected");
    return false;
  }
  return playContextUri(playlistUri);
}

bool SpotifyClient::setPlayMode(const String &mode) {
  bool targetShuffle = false;
  String targetRepeat = "off";
  if (mode == "shuffle") {
    targetShuffle = true;
  } else if (mode == "repeat_once") {
    targetRepeat = "track";
  } else if (mode == "repeat_infinite") {
    targetRepeat = "context";
  } else if (mode != "normal") {
    state_.error = "Unknown play mode";
    return false;
  }

  String deviceQuery = activeDeviceQuery();
  if (!deviceQuery.isEmpty()) {
    deviceQuery.replace("?", "&");
  }

  if (!sendPlayerCommand("PUT", String("/me/player/repeat?state=") + targetRepeat + deviceQuery)) {
    return false;
  }
  if (!sendPlayerCommand("PUT", String("/me/player/shuffle?state=") + (targetShuffle ? "true" : "false") + deviceQuery)) {
    return false;
  }

  state_.shuffle = targetShuffle;
  state_.repeatState = targetRepeat;
  state_.error = "";
  return true;
}

bool SpotifyClient::transferPlayback(const String &deviceId, bool play) {
  if (deviceId.isEmpty()) {
    state_.error = "Device missing";
    return false;
  }

  JsonDocument doc;
  JsonArray ids = doc["device_ids"].to<JsonArray>();
  ids.add(deviceId);
  doc["play"] = play;

  String body;
  serializeJson(doc, body);

  String payload;
  RequestGuard guard(requestMutex_, 5000);
  if (!guard.isLocked()) {
    state_.error = "Spotify busy";
    return false;
  }
  if (!ensureAccessToken()) {
    return false;
  }

  WiFiClientSecure client;
  configureTls(client);

  HTTPClient http;
  http.setConnectTimeout(Config::HttpConnectTimeoutMs);
  http.setTimeout(Config::HttpIoTimeoutMs);
  http.setReuse(false);
  if (!http.begin(client, String(Config::SpotifyApiBaseUrl) + "/me/player")) {
    state_.error = "Transfer HTTP begin failed";
    return false;
  }

  http.addHeader("Authorization", String("Bearer ") + accessToken_);
  http.addHeader("Content-Type", "application/json");
  const int code = http.PUT(body);
  payload = http.getString();
  http.end();

  if (code == 204 || code == 200) {
    state_.deviceId = deviceId;
    state_.error = "";
    return true;
  }

  state_.error = spotifyErrorFromPayload(code, payload);
  return false;
}

bool SpotifyClient::findPlaylistUriByName(const String &playlistName, String &playlistUri) {
  playlistUri = "";
  if (playlistName.isEmpty()) {
    state_.error = "Playlist name missing";
    return false;
  }

  String payload;
  const String path = String("/search?type=playlist&limit=1&q=") + urlEncode(playlistName);
  const int code = apiRequest("GET", path, &payload);
  if (code != 200) {
    state_.error = state_.error.isEmpty() ? "Playlist search failed " + String(code) : state_.error;
    return false;
  }
  if (payload.isEmpty()) {
    state_.error = "Playlist search empty";
    return false;
  }

  JsonDocument filter;
  filter["playlists"]["items"][0]["name"] = true;
  filter["playlists"]["items"][0]["uri"] = true;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    state_.error = "Playlist JSON failed";
    return false;
  }

  JsonArrayConst items = doc["playlists"]["items"].as<JsonArrayConst>();
  for (JsonVariantConst item : items) {
    const char *uri = item["uri"] | "";
    if (strlen(uri) == 0) {
      continue;
    }
    playlistUri = uri;
    state_.error = "";
    return true;
  }

  state_.error = "Liked Proxy playlist not found";
  return false;
}

bool SpotifyClient::playContextUri(const String &contextUri) {
  if (contextUri.isEmpty()) {
    state_.error = "Playlist URI missing";
    return false;
  }

  JsonDocument doc;
  doc["context_uri"] = contextUri;

  String body;
  serializeJson(doc, body);

  String payload;
  RequestGuard guard(requestMutex_, 5000);
  if (!guard.isLocked()) {
    state_.error = "Spotify busy";
    return false;
  }
  if (!ensureAccessToken()) {
    return false;
  }

  WiFiClientSecure client;
  configureTls(client);

  HTTPClient http;
  http.setConnectTimeout(Config::HttpConnectTimeoutMs);
  http.setTimeout(Config::HttpIoTimeoutMs);
  http.setReuse(false);
  const String path = String("/me/player/play") + activeDeviceQuery();
  if (!http.begin(client, String(Config::SpotifyApiBaseUrl) + path)) {
    state_.error = "Playlist HTTP begin failed";
    return false;
  }

  http.addHeader("Authorization", String("Bearer ") + accessToken_);
  http.addHeader("Content-Type", "application/json");
  AppLog.print("Spotify request: PUT ");
  AppLog.println(path);
  const int code = http.PUT(body);
  payload = http.getString();
  http.end();

  AppLog.print("Spotify response: ");
  AppLog.print(code);
  AppLog.print(" bytes=");
  AppLog.println(payload.length());

  if (code == 204 || code == 200) {
    state_.hasPlayback = true;
    state_.isPlaying = true;
    state_.progressSyncedAt = millis();
    state_.error = "";
    return true;
  }

  state_.error = spotifyErrorFromPayload(code, payload);
  return false;
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

  VolumeCommand command;
  command.volume = volume;
  copyToBuffer(command.deviceId, sizeof(command.deviceId), state_.deviceId);
  // The queue has depth 1 so fast knob turns collapse into the latest desired volume.
  xQueueOverwrite(volumeCommandQueue_, &command);

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
#if SPOTIFY_ALLOW_INSECURE_TLS
  client.setInsecure();
#else
  client.setCACert(SpotifyRootCa);
#endif
  client.setHandshakeTimeout(Config::TlsHandshakeTimeoutMs);
  client.setTimeout(Config::HttpIoTimeoutMs);
}

void SpotifyClient::loadSpotifyCredentials() {
  preferences_.begin("spotify", false);
  refreshToken_ = preferences_.getString("refresh", "");
  refreshTokenFromStorage_ = !refreshToken_.isEmpty();
  refreshTokenSource_ = refreshTokenFromStorage_ ? "NVS" : "";

  Preferences provision;
  provision.begin("provision", true);
  Preferences spotifydj;
  spotifydj.begin("spotifydj", true);
  clientId_ = spotifydj.getString("spotify_client_id", "");
  market_ = spotifydj.getString("spotify_market", "");
  spotifydj.end();

  if (clientId_.isEmpty()) {
    clientId_ = provision.getString("sp_client", "");
  }
  if (market_.isEmpty()) {
    market_ = provision.getString("spotify_market", "");
  }

  if (!refreshTokenFromStorage_) {
    Preferences spotifydjToken;
    spotifydjToken.begin("spotifydj", true);
    refreshToken_ = spotifydjToken.getString("spotify_refresh_token", "");
    spotifydjToken.end();
    if (refreshToken_.isEmpty()) {
      refreshToken_ = provision.getString("sp_refresh", "");
    }
    refreshTokenFromStorage_ = !refreshToken_.isEmpty();
    refreshTokenSource_ = refreshTokenFromStorage_ ? "Portal" : "";
  }
  provision.end();

  if (!refreshTokenFromStorage_) {
    refreshTokenSource_ = "Missing";
  }

  AppLog.print("Refresh token source: ");
  AppLog.println(refreshTokenSource_);
  AppLog.print("Spotify client id source: ");
  AppLog.println(clientId_.isEmpty() ? "missing" : "NVS");
}

void SpotifyClient::saveRefreshToken(const String &newRefreshToken) {
  if (newRefreshToken.isEmpty() || newRefreshToken == refreshToken_) {
    return;
  }

  if (preferences_.putString("refresh", newRefreshToken) == 0) {
    AppLog.println("Failed to save rotated refresh token");
    return;
  }

  refreshToken_ = newRefreshToken;
  refreshTokenFromStorage_ = true;
  refreshTokenSource_ = "NVS";
  AppLog.println("Saved rotated refresh token to NVS");
}

bool SpotifyClient::refreshAccessToken() {
  if (WiFi.status() != WL_CONNECTED) {
    state_.error = "WiFi disconnected";
    return false;
  }
  if (refreshToken_.isEmpty()) {
    state_.error = "Refresh token missing";
    return false;
  }
  if (clientId_.isEmpty()) {
    state_.error = "Spotify client id missing";
    return false;
  }

  for (int attempt = 0; attempt < 1; attempt++) {
    WiFiClientSecure client;
    configureTls(client);

    HTTPClient http;
    http.setConnectTimeout(Config::HttpConnectTimeoutMs);
    http.setTimeout(Config::HttpIoTimeoutMs);
    http.setReuse(false);
    if (!http.begin(client, Config::SpotifyAccountsUrl)) {
      state_.error = "Token HTTP begin failed";
      return false;
    }

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const String body = String("grant_type=refresh_token") +
                        "&refresh_token=" + urlEncode(refreshToken_) +
                        "&client_id=" + urlEncode(clientId_);

    const int code = http.POST(body);
    const String payload = http.getString();
    http.end();

    AppLog.print("Token response: ");
    AppLog.print(code);
    AppLog.print(" bytes=");
    AppLog.println(payload.length());

    if (code != 200) {
      state_.error = tokenErrorFromPayload(code, payload);
      AppLog.println(payload);
      return false;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      state_.error = "Token JSON failed";
      return false;
    }

    const char *token = doc["access_token"] | "";
    const int expiresIn = doc["expires_in"] | 3600;
    if (strlen(token) == 0) {
      state_.error = "Token missing";
      return false;
    }

    // Spotify may or may not return a new refresh token. Save it immediately when it appears.
    const char *rotatedRefreshToken = doc["refresh_token"] | "";
    if (strlen(rotatedRefreshToken) > 0) {
      saveRefreshToken(rotatedRefreshToken);
    } else if (!refreshTokenFromStorage_) {
      saveRefreshToken(refreshToken_);
    }

    accessToken_ = token;
    accessTokenExpiresAt_ = millis() + ((expiresIn - 60) * 1000UL);
    state_.error = "";
    return true;
  }

  return false;
}

bool SpotifyClient::ensureAccessToken() {
  if (!accessToken_.isEmpty() && static_cast<int32_t>(millis() - accessTokenExpiresAt_) < 0) {
    return true;
  }
  return refreshAccessToken();
}

int SpotifyClient::apiRequest(
    const char *method,
    const String &path,
    String *responsePayload,
    bool updatePlaybackError) {
  // Foreground playback polls fail fast when the volume worker is busy; the worker can wait longer.
  RequestGuard guard(requestMutex_, updatePlaybackError ? 50 : 5000);
  if (!guard.isLocked()) {
    if (updatePlaybackError) {
      state_.error = "Spotify busy";
    }
    return -2;
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    if (!ensureAccessToken()) {
      return -1;
    }

    WiFiClientSecure client;
    configureTls(client);

    HTTPClient http;
    http.setConnectTimeout(Config::HttpConnectTimeoutMs);
    http.setTimeout(Config::HttpIoTimeoutMs);
    http.setReuse(false);
    const String url = String(Config::SpotifyApiBaseUrl) + path;
    if (!http.begin(client, url)) {
      state_.error = "Spotify HTTP begin failed";
      return -1;
    }

    http.addHeader("Authorization", String("Bearer ") + accessToken_);
    AppLog.print("Spotify request: ");
    AppLog.print(method);
    AppLog.print(" ");
    AppLog.println(path);

    int code = -1;
    if (strcmp(method, "GET") == 0) {
      code = http.GET();
    } else {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Content-Length", "0");
      code = http.sendRequest(method, static_cast<uint8_t *>(nullptr), 0);
    }

    const String payload = http.getString();
    http.end();

    AppLog.print("Spotify response: ");
    AppLog.print(code);
    AppLog.print(" bytes=");
    AppLog.println(payload.length());

    if (code == 401 && attempt == 0) {
      // Access tokens expire; clear and retry once through refreshAccessToken().
      accessToken_ = "";
      continue;
    }

    if (responsePayload != nullptr) {
      *responsePayload = payload;
    }

    if (code >= 400 && updatePlaybackError) {
      state_.error = spotifyErrorFromPayload(code, payload);
    }
    return code;
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

  String path = "/me/player/volume?volume_percent=" + String(command.volume);
  if (strlen(command.deviceId) > 0) {
    path += "&device_id=" + urlEncode(command.deviceId);
  }

  AppLog.print("Volume worker: sending ");
  AppLog.println(command.volume);

  String payload;
  const int code = apiRequest("PUT", path, &payload, false);
  if (code == 204 || code == 200) {
    result.ok = true;
    copyToBuffer(result.message, sizeof(result.message), "Volume " + String(command.volume) + "%");
    AppLog.println("Volume worker: OK");
    return result;
  }

  String message;
  if (!payload.isEmpty()) {
    message = spotifyErrorFromPayload(code, payload);
  } else if (!state_.error.isEmpty()) {
    message = state_.error;
  } else {
    message = "Volume failed " + String(code);
  }

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
    return String(reason);
  }
  if (strlen(message) > 0) {
    return String(message);
  }
  return "Spotify HTTP " + String(code);
}

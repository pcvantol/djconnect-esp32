// Home Assistant proxy client for playback state, controls, and async volume updates.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "AppState.h"

class SpotifyDJDevice;

class SpotifyClient {
public:
  explicit SpotifyClient(SpotifyState &state) : state_(state) {}

  // Creates request locks and starts the volume worker.
  void begin();

  // Attaches the paired Home Assistant device identity used for proxy commands.
  void setHomeAssistantDevice(SpotifyDJDevice &device);

  // Checks whether Home Assistant can currently proxy playback commands.
  bool authorize();

  // Compatibility no-op: playback backend credentials live in Home Assistant.
  void useCredentialsForProvisioning(const String &clientId, const String &refreshToken);

  // Clears in-memory proxy/auth state.
  void clearStoredTokens();

  // Kept for compatibility with older provisioning flows; HA owns playback backend credentials now.
  void reloadCredentials();

  bool isAuthorized() const;
  bool needsCredentialRefresh() const;
  uint32_t accessTokenExpiresInSeconds() const;
  String refreshTokenSource() const;

  // Reads active playback, output, track, artist, progress, and volume.
  bool refreshPlayback();

  // Reads only available outputs, used when no active playback exists.
  bool refreshDevicesOnly();

  // Reads all available outputs for the Sound outputs screen.
  bool refreshDevices(DeviceListState &devices);

  // Reads the next songs from the backend play queue.
  bool refreshQueue(QueueState &queue);
  bool refreshPlaylistContextQueue(QueueState &queue);

  // Reads backend playlists for device/web selection.
  bool refreshPlaylists(PlaylistListState &playlists);

  // Playback control endpoints used by the current screen actions.
  bool pausePlayback();
  bool resumePlayback();
  bool nextTrack();
  bool previousTrack();
  bool startLikedProxyPlaylist();
  bool startPlaylist(const String &playlistUri);
  bool setPlayMode(const String &mode);
  bool transferPlayback(const String &deviceId, bool play);

  // Queues a non-blocking volume command; the worker sends only the latest queued value.
  bool queueVolume(int volume);

  // Retrieves the async volume result, if the worker has completed a request.
  bool pollVolumeResult(VolumeResult &result);

private:
  // RAII wrapper around the request mutex so all HTTPS calls remain serialized.
  class RequestGuard {
  public:
    RequestGuard(SemaphoreHandle_t mutex, uint32_t waitMs);
    ~RequestGuard();
    bool isLocked() const;

  private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
  };

  static void volumeWorkerEntry(void *parameter);

  // Home Assistant proxy helpers.
  bool proxyCommand(const String &command, JsonDocument *response = nullptr);
  bool proxyCommand(const String &command, const String &value, JsonDocument *response = nullptr);
  bool proxyCommand(const String &command, int value, JsonDocument *response = nullptr);
  bool proxyRequest(JsonDocument &request, JsonDocument *response = nullptr);
  String proxyEndpoint() const;
  bool proxyCooldownActive() const;
  void applyPlayback(JsonVariantConst playback);
  void applyDeviceList(JsonVariantConst source, DeviceListState &devices);
  void applyQueue(JsonVariantConst source, QueueState &queue);
  void applyPlaylists(JsonVariantConst source, PlaylistListState &playlists);
  void setProxyError(const String &message);

  // Legacy direct-backend helpers retained as guarded no-ops while callers move to proxy commands.
  void configureTls(WiFiClientSecure &client);
  void loadSpotifyCredentials();
  void saveRefreshToken(const String &newRefreshToken);
  bool refreshAccessToken();
  bool ensureAccessToken();

  // Common HTTP request path. updatePlaybackError controls whether background volume failures alter the UI error.
  int apiRequest(
      const char *method,
      const String &path,
      String *responsePayload = nullptr,
      bool updatePlaybackError = true);
  bool sendPlayerCommand(const char *method, const String &path);
  VolumeResult sendVolumeToSpotify(const VolumeCommand &command);
  bool findPlaylistUriByName(const String &playlistName, String &playlistUri);
  bool playContextUri(const String &contextUri);
  bool fillQueueFromPlaylistContext(QueueState &queue);

  // Spotify JSON parsing helpers.
  void applyDevice(JsonVariantConst device);
  void clearPlayback();
  String activeDeviceQuery() const;
  String artistList(JsonArrayConst artists) const;
  String tokenErrorFromPayload(int code, const String &payload) const;
  String tokenErrorNameFromPayload(const String &payload) const;
  String spotifyErrorFromPayload(int code, const String &payload) const;

  SpotifyState &state_;
  SpotifyDJDevice *device_ = nullptr;
  QueueHandle_t volumeCommandQueue_ = nullptr;
  QueueHandle_t volumeResultQueue_ = nullptr;
  SemaphoreHandle_t requestMutex_ = nullptr;
  String accessToken_;
  String clientId_;
  String refreshToken_;
  String market_;
  bool refreshTokenFromStorage_ = false;
  bool tokenInvalidGrant_ = false;
  String refreshTokenSource_ = "Unknown";
  uint32_t accessTokenExpiresAt_ = 0;
  uint32_t lastProxyFailureAt_ = 0;
};

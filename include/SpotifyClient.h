// Spotify Web API client for auth, playback state, playback controls, and async volume updates.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include "AppState.h"

class SpotifyClient {
public:
  explicit SpotifyClient(SpotifyState &state) : state_(state) {}

  // Creates request locks, starts the volume worker, and loads Spotify credentials from NVS.
  void begin();

  // Exchanges the refresh token for an access token.
  bool authorize();

  // Temporarily uses submitted Spotify credentials for captive-portal authorization testing.
  void useCredentialsForProvisioning(const String &clientId, const String &refreshToken);

  // Clears access-token state and the rotated refresh token stored by this client.
  void clearStoredTokens();

  bool isAuthorized() const;
  uint32_t accessTokenExpiresInSeconds() const;
  String refreshTokenSource() const;

  // Reads the active Spotify Connect playback, device, track, artist, progress, and volume.
  bool refreshPlayback();

  // Reads only available Connect devices, used when no active playback exists.
  bool refreshDevicesOnly();

  // Reads all available Spotify Connect outputs for the Sound outputs screen.
  bool refreshDevices(DeviceListState &devices);

  // Reads the next songs from Spotify's play queue.
  bool refreshQueue(QueueState &queue);

  // Playback control endpoints used by the current screen actions.
  bool pausePlayback();
  bool resumePlayback();
  bool nextTrack();
  bool previousTrack();
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

  // TLS/auth helpers.
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

  // Spotify JSON parsing helpers.
  void applyDevice(JsonVariantConst device);
  void clearPlayback();
  String activeDeviceQuery() const;
  String artistList(JsonArrayConst artists) const;
  String tokenErrorFromPayload(int code, const String &payload) const;
  String tokenErrorNameFromPayload(const String &payload) const;
  String spotifyErrorFromPayload(int code, const String &payload) const;

  SpotifyState &state_;
  Preferences preferences_;
  QueueHandle_t volumeCommandQueue_ = nullptr;
  QueueHandle_t volumeResultQueue_ = nullptr;
  SemaphoreHandle_t requestMutex_ = nullptr;
  String accessToken_;
  String clientId_;
  String refreshToken_;
  bool refreshTokenFromStorage_ = false;
  String refreshTokenSource_ = "Unknown";
  uint32_t accessTokenExpiresAt_ = 0;
};

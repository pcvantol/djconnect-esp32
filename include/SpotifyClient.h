// Home Assistant proxy client for playback state, controls, and async volume updates.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

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

  // Clears in-memory proxy/auth state.
  void clearStoredTokens();

  // Clears in-memory Home Assistant proxy state.
  void reloadCredentials();

  bool isAuthorized() const;
  bool needsCredentialRefresh() const;

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
  bool setShuffle(bool enabled);
  bool setRepeatMode(const String &repeatState);
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
  bool tokenInvalidGrant_ = false;
  uint32_t lastProxyFailureAt_ = 0;
};

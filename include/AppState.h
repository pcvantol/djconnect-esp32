// Shared runtime state for the Spotify remote.
// These structs deliberately contain data only; controllers update them and renderers read them.
#pragma once

#include <Arduino.h>

// Snapshot of the Spotify Connect playback/device state shown on the display.
struct SpotifyState {
  // Spotify Connect device identity and capabilities.
  String deviceId;
  String deviceName;
  String deviceType;

  // Current media metadata. currentType is a fallback such as "track", "episode", or "No active playback".
  String trackName;
  String artistName;
  String currentType;
  String currentUri;
  String contextUri;
  String albumImageUrl;

  // User-facing Spotify/network error. Empty means the playback screen can show normal state.
  String error;
  bool hasPlayback = false;
  bool isPlaying = false;
  bool supportsVolume = false;
  bool shuffle = false;
  String repeatState = "off";

  // Volume and track timing are kept in Spotify units: percent and milliseconds.
  int volume = -1;
  int progressMs = 0;
  int durationMs = 0;

  // millis() timestamp for the last progress_ms received from Spotify, used for local progress estimation.
  uint32_t progressSyncedAt = 0;
};

// Snapshot of the BQ27220 fuel-gauge readings shown in the header.
struct BatteryState {
  bool available = false;
  bool charging = false;
  bool discharging = false;
  bool full = false;
  bool percentEstimated = false;
  int percent = -1;
  int gaugePercent = -1;
  int voltageMv = 0;
  int currentMa = 0;
};

// Short-lived footer message, for example "Volume 55%" or "Spotify authorized".
struct StatusNotice {
  String message = "Starting";
  uint32_t visibleUntil = 0;

  // Makes this notice visible for ttlMs from now.
  void show(const String &newMessage, uint32_t ttlMs = 2500) {
    message = newMessage;
    visibleUntil = millis() + ttlMs;
  }

  // Uses signed subtraction so millis() wraparound does not break expiry checks.
  bool isVisible() const {
    return visibleUntil != 0 && static_cast<int32_t>(millis() - visibleUntil) < 0;
  }

  // Clears expired notices and reports whether the display should be redrawn.
  bool clearIfExpired() {
    if (visibleUntil == 0 || isVisible()) {
      return false;
    }
    visibleUntil = 0;
    return true;
  }
};

// Payload sent from the UI loop to the async Spotify volume worker.
struct VolumeCommand {
  int volume = 0;
  char deviceId[96] = {0};
};

// Result sent back from the async Spotify volume worker to the UI loop.
struct VolumeResult {
  bool ok = false;
  int volume = 0;
  char message[64] = {0};
};

// Compact status snapshot shown on the About screen.
struct AboutStatus {
  String ipAddress;
  String webAddress;
  String mqttState;
  bool wifiConnected = false;
  bool haPaired = false;
  bool mqttConnected = false;
  bool spotifyConnected = false;
};

struct QueueItemState {
  String title;
  String subtitle;
};

struct QueueState {
  bool available = false;
  String error;
  size_t count = 0;
  QueueItemState items[5];
};

struct PlaylistItemState {
  String name;
  String owner;
  String uri;
};

struct PlaylistListState {
  bool available = false;
  String error;
  size_t count = 0;
  PlaylistItemState items[8];
};

struct SpotifyDeviceState {
  String id;
  String name;
  String type;
  bool active = false;
  bool supportsVolume = false;
};

struct DeviceListState {
  bool available = false;
  String error;
  size_t count = 0;
  SpotifyDeviceState devices[8];
};

// Lightweight runtime stats surfaced by the web dashboard.
struct RuntimeDiagnostics {
  uint32_t uptimeMs = 0;
  uint32_t loopCount = 0;
  uint32_t lastLoopDurationMs = 0;
  uint32_t maxLoopDurationMs = 0;
  uint8_t cpuUsagePercent = 0;
  String lastDjText;
};

// Live visual output state published to MQTT and shown on the web dashboard.
struct VisualState {
  bool screenOn = true;
  uint8_t screenBrightnessLevel = 100;
  bool ledOn = true;
};

// MQTT broker settings used for Home Assistant state publishing.
struct MqttSettings {
  bool enabled = false;
  String host;
  uint16_t port = 1883;
  String username;
  String password;
};

enum class MqttCommandType {
  None,
  Next,
  Previous,
  Status,
  Ota,
  Volume,
  TransferOutput,
  StartPlaylist,
  DjResponse,
  ScreenBrightness,
  ScreenDimTimeout,
  DeepSleepTimeout,
  SpeakerVolume,
  Language,
  Theme,
  LogLevel,
};

struct MqttCommand {
  MqttCommandType type = MqttCommandType::None;
  String value;
  int numericValue = 0;
};

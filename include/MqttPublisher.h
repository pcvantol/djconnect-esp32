// Publishes SpotifyDJ state to MQTT and announces Home Assistant discovery entities.
#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "AppState.h"

class MqttPublisher {
public:
  void begin(
      const String &deviceId,
      const MqttSettings &settings,
      const SpotifyState &playback,
      const BatteryState &battery,
      const DeviceListState &deviceList,
      const PlaylistListState &playlists,
      const RuntimeDiagnostics &diagnostics,
      const VisualState &visualState,
      const uint8_t &screenBrightnessPercent,
      const uint8_t &speakerVolumePercent,
      const String &languageCode,
      const String &themeCode,
      const String &logLevel,
      const uint32_t &screenOffTimeoutMs,
      const uint32_t &deviceSleepTimeoutMs);

  void loop();
  void requestPublish();
  void requestStatusPublish();
  void setDeviceFlags(bool paired, bool spotifyConfigured);
  void publishEvent(const char *type, const char *button, const char *event);
  void publishDjResponseEvent(bool spoken, bool displayed);
  bool pollCommand(MqttCommand &command);

  // Publishes Home Assistant availability=offline and disconnects before deep sleep.
  void prepareForSleep();

  // Reports broker connection state for web/About diagnostics.
  bool connected();
  String connectionState();
  uint32_t lastPublishAtMs() const;

private:
  bool connectIfNeeded();
  void publishDiscoveryIfNeeded();
  void updateDiscoveryOptionSignature();
  void publishState();
  void publishDeviceStatus();
  void publishAvailability(bool online);
  void publishSensorDiscovery(
      const char *objectId,
      const char *name,
      const char *valueTemplate,
      const char *unit = nullptr,
      const char *deviceClass = nullptr);
  void publishButtonDiscovery(const char *objectId, const char *name, const char *payload);
  void publishNumberDiscovery(
      const char *objectId,
      const char *name,
      const char *commandTopic,
      int minValue,
      int maxValue,
      int step,
      const char *valueTemplate,
      const char *unit = "%");
  void publishSelectDiscovery(const char *objectId, const char *name, const char *commandTopic, const String *options, size_t optionCount, const char *valueTemplate);
  void handleMessage(char *topic, uint8_t *payload, unsigned int length);
  void enqueueCommand(const MqttCommand &command);
  void resetAuthLockIfSettingsChanged();
  static void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  String stateTopic() const;
  String availabilityTopic() const;
  String commandTopic(const char *suffix) const;
  String deviceCommandTopic() const;
  String deviceStatusTopic() const;
  String deviceEventTopic() const;
  String discoveryTopic(const char *objectId) const;
  String discoveryTopic(const char *component, const char *objectId) const;
  String connectCodeText(int code) const;

  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  String deviceId_ = "spotifydj";
  const MqttSettings *settings_ = nullptr;
  const SpotifyState *playback_ = nullptr;
  const BatteryState *battery_ = nullptr;
  const DeviceListState *deviceList_ = nullptr;
  const PlaylistListState *playlists_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  const VisualState *visualState_ = nullptr;
  const uint8_t *screenBrightnessPercent_ = nullptr;
  const uint8_t *speakerVolumePercent_ = nullptr;
  const String *languageCode_ = nullptr;
  const String *themeCode_ = nullptr;
  const String *logLevel_ = nullptr;
  const uint32_t *screenOffTimeoutMs_ = nullptr;
  const uint32_t *deviceSleepTimeoutMs_ = nullptr;
  bool started_ = false;
  bool discoveryPublished_ = false;
  bool publishRequested_ = false;
  bool statusPublishRequested_ = false;
  bool paired_ = false;
  bool spotifyConfigured_ = false;
  bool commandPending_ = false;
  MqttCommand pendingCommand_;
  String discoveryOptionSignature_;
  String settingsSignature_;
  String lastPlaylistCommand_;
  int lastConnectCode_ = 0;
  uint8_t authFailureCount_ = 0;
  bool authRetryLocked_ = false;
  uint32_t lastConnectAttemptAt_ = 0;
  uint32_t lastPublishAt_ = 0;
};

// Publishes SpotifyDJ state to MQTT and announces Home Assistant discovery entities.
#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include "AppState.h"

class MqttPublisher {
public:
  void begin(
      const MqttSettings &settings,
      const SpotifyState &playback,
      const BatteryState &battery,
      const RuntimeDiagnostics &diagnostics,
      const VisualState &visualState,
      const uint8_t &screenBrightnessPercent,
      const uint32_t &screenOffTimeoutMs);

  void loop();
  void requestPublish();

  // Publishes Home Assistant availability=offline and disconnects before deep sleep.
  void prepareForSleep();

  // Reports broker connection state for web/About diagnostics.
  bool connected();
  String connectionState();
  uint32_t lastPublishAtMs() const;

private:
  bool connectIfNeeded();
  void publishDiscoveryIfNeeded();
  void publishState();
  void publishAvailability(bool online);
  void publishSensorDiscovery(
      const char *objectId,
      const char *name,
      const char *valueTemplate,
      const char *unit = nullptr,
      const char *deviceClass = nullptr);
  String stateTopic() const;
  String availabilityTopic() const;
  String discoveryTopic(const char *objectId) const;
  String connectCodeText(int code) const;

  WiFiClient wifiClient_;
  PubSubClient client_{wifiClient_};
  const MqttSettings *settings_ = nullptr;
  const SpotifyState *playback_ = nullptr;
  const BatteryState *battery_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  const VisualState *visualState_ = nullptr;
  const uint8_t *screenBrightnessPercent_ = nullptr;
  const uint32_t *screenOffTimeoutMs_ = nullptr;
  bool started_ = false;
  bool discoveryPublished_ = false;
  bool publishRequested_ = false;
  int lastConnectCode_ = 0;
  uint32_t lastConnectAttemptAt_ = 0;
  uint32_t lastPublishAt_ = 0;
};

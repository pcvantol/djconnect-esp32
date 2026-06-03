// MQTT and Home Assistant discovery publishing.
#include "MqttPublisher.h"

#include "AppLog.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "Config.h"
#include "LogicHelpers.h"

static const char *DeviceId = "spotifydj";
static const char *DeviceName = "SpotifyDJ";
static const char *StateTopic = "spotifydj/state";
static const char *AvailabilityTopic = "spotifydj/availability";
static constexpr uint32_t PublishIntervalMs = 30000;
static constexpr uint32_t EventPublishMinIntervalMs = 2000;
static constexpr uint32_t ReconnectIntervalMs = 5000;

void MqttPublisher::begin(
    const MqttSettings &settings,
    const SpotifyState &playback,
    const BatteryState &battery,
    const RuntimeDiagnostics &diagnostics,
    const VisualState &visualState,
    const uint8_t &screenBrightnessPercent,
    const uint32_t &screenOffTimeoutMs) {
  settings_ = &settings;
  playback_ = &playback;
  battery_ = &battery;
  diagnostics_ = &diagnostics;
  visualState_ = &visualState;
  screenBrightnessPercent_ = &screenBrightnessPercent;
  screenOffTimeoutMs_ = &screenOffTimeoutMs;
  started_ = true;
  discoveryPublished_ = false;
  publishRequested_ = true;

  client_.setBufferSize(1536);
}

void MqttPublisher::loop() {
  if (!started_ || settings_ == nullptr || !settings_->enabled || settings_->host.isEmpty()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!connectIfNeeded()) {
    return;
  }

  client_.loop();
  publishDiscoveryIfNeeded();

  const uint32_t now = millis();
  const bool periodicDue = now - lastPublishAt_ >= PublishIntervalMs;
  const bool eventDue = publishRequested_ && now - lastPublishAt_ >= EventPublishMinIntervalMs;
  if (periodicDue || eventDue) {
    publishState();
  }
}

void MqttPublisher::requestPublish() {
  publishRequested_ = true;
}

void MqttPublisher::prepareForSleep() {
  if (!started_ || settings_ == nullptr || !settings_->enabled || settings_->host.isEmpty()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (connectIfNeeded()) {
    publishAvailability(false);
    client_.loop();
    delay(80);
    client_.disconnect();
  }
}

bool MqttPublisher::connected() {
  return started_ && settings_ != nullptr && settings_->enabled && client_.connected();
}

String MqttPublisher::connectionState() {
  if (settings_ == nullptr || !settings_->enabled || settings_->host.isEmpty()) {
    return "Disabled";
  }
  if (client_.connected()) {
    return "Connected";
  }
  if (lastConnectCode_ != 0) {
    return connectCodeText(lastConnectCode_) + " (rc=" + String(lastConnectCode_) + ")";
  }
  return "Waiting for broker";
}

uint32_t MqttPublisher::lastPublishAtMs() const {
  return lastPublishAt_;
}

bool MqttPublisher::connectIfNeeded() {
  if (client_.connected()) {
    return true;
  }

  const uint32_t now = millis();
  if (now - lastConnectAttemptAt_ < ReconnectIntervalMs) {
    return false;
  }
  lastConnectAttemptAt_ = now;

  client_.setServer(settings_->host.c_str(), settings_->port);
  const String clientId = String(DeviceId) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  AppLog.print("MQTT connecting to ");
  AppLog.print(settings_->host);
  AppLog.print(":");
  AppLog.print(settings_->port);
  AppLog.print(" as ");
  AppLog.println(settings_->username.isEmpty() ? "anonymous" : settings_->username);

  bool ok = false;
  if (!settings_->username.isEmpty()) {
    ok = client_.connect(
        clientId.c_str(),
        settings_->username.c_str(),
        settings_->password.c_str(),
        availabilityTopic().c_str(),
        0,
        true,
        "offline");
  } else {
    ok = client_.connect(
        clientId.c_str(),
        availabilityTopic().c_str(),
        0,
        true,
        "offline");
  }

  if (ok) {
    AppLog.println("MQTT connected");
    lastConnectCode_ = 0;
    publishAvailability(true);
    discoveryPublished_ = false;
    publishRequested_ = true;
  } else {
    lastConnectCode_ = client_.state();
    AppLog.print("MQTT connect failed ");
    AppLog.print(connectCodeText(lastConnectCode_));
    AppLog.print(" rc=");
    AppLog.println(lastConnectCode_);
  }
  return ok;
}

void MqttPublisher::publishDiscoveryIfNeeded() {
  if (discoveryPublished_) {
    return;
  }

  publishSensorDiscovery("status", "Status", "{{ value_json.playback.status }}");
  publishSensorDiscovery("track", "Track", "{{ value_json.playback.track }}");
  publishSensorDiscovery("artist", "Artist", "{{ value_json.playback.artist }}");
  publishSensorDiscovery("device", "Output device", "{{ value_json.device.name }}");
  publishSensorDiscovery("volume", "Volume", "{{ value_json.device.volume }}", "%");
  publishSensorDiscovery("battery", "Battery", "{{ value_json.battery.percent }}", "%", "battery");
  publishSensorDiscovery("wifi_rssi", "WiFi RSSI", "{{ value_json.wifi.rssi }}", "dBm", "signal_strength");
  publishSensorDiscovery("screen_state", "Screen state", "{{ value_json.screen.state }}");
  publishSensorDiscovery("screen_brightness_level", "Screen brightness level", "{{ value_json.screen.brightness_level }}", "%");
  publishSensorDiscovery("led_state", "LED state", "{{ value_json.led.state }}");
  publishSensorDiscovery("heap_free", "Free heap", "{{ value_json.system.heap_free }}", "B");
  publishSensorDiscovery("loop_load", "Loop load", "{{ value_json.system.loop_load }}", "%");

  discoveryPublished_ = true;
}

void MqttPublisher::publishState() {
  if (!client_.connected() || playback_ == nullptr || battery_ == nullptr || diagnostics_ == nullptr) {
    return;
  }

  JsonDocument doc;
  JsonObject playback = doc["playback"].to<JsonObject>();
  playback["status"] = playback_->isPlaying ? "playing" : playback_->hasPlayback ? "paused" : "idle";
  playback["track"] = playback_->trackName;
  playback["artist"] = playback_->artistName;
  playback["type"] = playback_->currentType;
  playback["progress_ms"] = Logic::estimatedProgressMs(
      playback_->progressMs,
      playback_->durationMs,
      playback_->isPlaying,
      playback_->progressSyncedAt,
      millis());
  playback["duration_ms"] = playback_->durationMs;

  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = playback_->deviceName;
  device["type"] = playback_->deviceType;
  device["volume"] = playback_->volume;

  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["percent"] = battery_->percent;
  battery["estimated"] = battery_->percentEstimated;
  battery["voltage_mv"] = battery_->voltageMv;
  battery["current_ma"] = battery_->currentMa;
  battery["charging"] = battery_->charging;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ip"] = WiFi.localIP().toString();
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi"] = WiFi.RSSI();

  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["brightness"] = screenBrightnessPercent_ == nullptr ? 0 : *screenBrightnessPercent_;
  settings["off_timeout_ms"] = screenOffTimeoutMs_ == nullptr ? 0 : *screenOffTimeoutMs_;

  JsonObject screen = doc["screen"].to<JsonObject>();
  const bool screenOn = visualState_ != nullptr && visualState_->screenOn;
  screen["state"] = screenOn ? "on" : "off";
  screen["brightness_level"] = visualState_ == nullptr ? 0 : visualState_->screenBrightnessLevel;

  JsonObject led = doc["led"].to<JsonObject>();
  const bool ledOn = visualState_ != nullptr && visualState_->ledOn;
  led["state"] = ledOn ? "on" : "off";

  JsonObject system = doc["system"].to<JsonObject>();
  system["uptime_ms"] = millis();
  system["heap_free"] = ESP.getFreeHeap();
  system["heap_used"] = ESP.getHeapSize() - ESP.getFreeHeap();
  system["loop_load"] = diagnostics_->cpuUsagePercent;

  String payload;
  serializeJson(doc, payload);
  client_.publish(stateTopic().c_str(), payload.c_str(), true);
  lastPublishAt_ = millis();
  publishRequested_ = false;
}

void MqttPublisher::publishAvailability(bool online) {
  if (client_.connected()) {
    client_.publish(availabilityTopic().c_str(), online ? "online" : "offline", true);
  }
}

void MqttPublisher::publishSensorDiscovery(
    const char *objectId,
    const char *name,
    const char *valueTemplate,
    const char *unit,
    const char *deviceClass) {
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = String(DeviceId) + "_" + objectId;
  doc["state_topic"] = stateTopic();
  doc["availability_topic"] = availabilityTopic();
  doc["value_template"] = valueTemplate;
  if (unit != nullptr) {
    doc["unit_of_measurement"] = unit;
  }
  if (deviceClass != nullptr) {
    doc["device_class"] = deviceClass;
  }
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DeviceId;
  device["name"] = DeviceName;
  device["manufacturer"] = "LilyGO";
  device["model"] = "T-Embed-CC1101";
  device["sw_version"] = Config::AppVersionNumber;

  String payload;
  serializeJson(doc, payload);
  client_.publish(discoveryTopic(objectId).c_str(), payload.c_str(), true);
}

String MqttPublisher::stateTopic() const {
  return StateTopic;
}

String MqttPublisher::availabilityTopic() const {
  return AvailabilityTopic;
}

String MqttPublisher::discoveryTopic(const char *objectId) const {
  return String("homeassistant/sensor/") + DeviceId + "/" + objectId + "/config";
}

String MqttPublisher::connectCodeText(int code) const {
  switch (code) {
    case MQTT_CONNECTION_TIMEOUT:
      return "Connection timeout";
    case MQTT_CONNECTION_LOST:
      return "Connection lost";
    case MQTT_CONNECT_FAILED:
      return "Connect failed";
    case MQTT_DISCONNECTED:
      return "Disconnected";
    case MQTT_CONNECT_BAD_PROTOCOL:
      return "Bad protocol";
    case MQTT_CONNECT_BAD_CLIENT_ID:
      return "Bad client ID";
    case MQTT_CONNECT_UNAVAILABLE:
      return "Broker unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS:
      return "Bad credentials";
    case MQTT_CONNECT_UNAUTHORIZED:
      return "Not authorized";
    default:
      return "MQTT error";
  }
}

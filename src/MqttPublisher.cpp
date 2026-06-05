// MQTT and Home Assistant discovery publishing.
#include "MqttPublisher.h"

#include "AppLog.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"

static const char *DeviceId = "spotifydj";
static const char *DeviceName = "SpotifyDJ";
static constexpr uint32_t PublishIntervalMs = 30000;
static constexpr uint32_t EventPublishMinIntervalMs = 2000;
static constexpr uint32_t ReconnectIntervalMs = 5000;
static constexpr uint8_t MaxAuthFailuresBeforeLock = 3;

MqttPublisher *activeMqttPublisher = nullptr;

namespace {
uint32_t fnv1a(const String &value) {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < value.length(); index++) {
    hash ^= static_cast<uint8_t>(value[index]);
    hash *= 16777619UL;
  }
  return hash;
}
}  // namespace

void MqttPublisher::begin(
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
    const uint32_t &deviceSleepTimeoutMs) {
  deviceId_ = deviceId.isEmpty() ? "spotifydj" : deviceId;
  settings_ = &settings;
  playback_ = &playback;
  battery_ = &battery;
  deviceList_ = &deviceList;
  playlists_ = &playlists;
  diagnostics_ = &diagnostics;
  visualState_ = &visualState;
  screenBrightnessPercent_ = &screenBrightnessPercent;
  speakerVolumePercent_ = &speakerVolumePercent;
  languageCode_ = &languageCode;
  themeCode_ = &themeCode;
  logLevel_ = &logLevel;
  screenOffTimeoutMs_ = &screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = &deviceSleepTimeoutMs;
  started_ = true;
  discoveryPublished_ = false;
  publishRequested_ = true;
  statusPublishRequested_ = true;
  commandPending_ = false;
  discoveryOptionSignature_ = "";
  settingsSignature_ = "";
  lastPlaylistCommand_ = "";
  lastConnectCode_ = 0;
  authFailureCount_ = 0;
  authRetryLocked_ = false;
  lastConnectAttemptAt_ = 0;
  lastPublishAt_ = 0;

  activeMqttPublisher = this;
  client_.setCallback(mqttCallback);
  client_.setBufferSize(3072);
  if (client_.connected()) {
    publishAvailability(false);
    client_.disconnect();
  }
}

void MqttPublisher::loop() {
  if (!started_ || settings_ == nullptr || !settings_->enabled || settings_->host.isEmpty()) {
    if (client_.connected()) {
      publishAvailability(false);
      client_.disconnect();
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  resetAuthLockIfSettingsChanged();
  if (!connectIfNeeded()) {
    return;
  }

  client_.loop();
  updateDiscoveryOptionSignature();
  publishDiscoveryIfNeeded();

  const uint32_t now = millis();
  const bool periodicDue = now - lastPublishAt_ >= PublishIntervalMs;
  const bool eventDue = publishRequested_ && now - lastPublishAt_ >= EventPublishMinIntervalMs;
  if (periodicDue || eventDue) {
    publishState();
    publishDeviceStatus();
  } else if (statusPublishRequested_) {
    publishDeviceStatus();
  }
}

void MqttPublisher::requestPublish() {
  publishRequested_ = true;
  statusPublishRequested_ = true;
}

void MqttPublisher::requestStatusPublish() {
  statusPublishRequested_ = true;
}

void MqttPublisher::setDeviceFlags(bool paired, bool spotifyConfigured) {
  if (paired_ != paired || spotifyConfigured_ != spotifyConfigured) {
    paired_ = paired;
    spotifyConfigured_ = spotifyConfigured;
    requestStatusPublish();
  } else {
    paired_ = paired;
    spotifyConfigured_ = spotifyConfigured;
  }
}

void MqttPublisher::publishEvent(const char *type, const char *button, const char *event) {
  if (!client_.connected()) {
    return;
  }
  JsonDocument doc;
  doc["type"] = type == nullptr ? "" : type;
  doc["button"] = button == nullptr ? "" : button;
  doc["event"] = event == nullptr ? "" : event;
  String payload;
  serializeJson(doc, payload);
  if (!client_.publish(deviceEventTopic().c_str(), payload.c_str(), false)) {
    AppLog.println("MQTT event publish failed");
  }
}

void MqttPublisher::publishDjResponseEvent(bool spoken, bool displayed) {
  if (!client_.connected()) {
    return;
  }
  JsonDocument doc;
  doc["type"] = "dj_response";
  doc["spoken"] = spoken;
  doc["displayed"] = displayed;
  String payload;
  serializeJson(doc, payload);
  if (!client_.publish(deviceEventTopic().c_str(), payload.c_str(), false)) {
    AppLog.println("MQTT DJ response event publish failed");
  }
}

bool MqttPublisher::pollCommand(MqttCommand &command) {
  if (!commandPending_) {
    return false;
  }
  command = pendingCommand_;
  pendingCommand_ = MqttCommand();
  commandPending_ = false;
  return true;
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
  if (authRetryLocked_) {
    return I18n::text("mqtt_auth_failed_update");
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
  if (authRetryLocked_) {
    return false;
  }

  const uint32_t now = millis();
  if (now - lastConnectAttemptAt_ < ReconnectIntervalMs) {
    return false;
  }
  lastConnectAttemptAt_ = now;

  client_.setServer(settings_->host.c_str(), settings_->port);
  const String clientId = deviceId_;

  AppLog.print("MQTT connecting to ");
  AppLog.print(settings_->host);
  AppLog.print(":");
  AppLog.print(settings_->port);
  AppLog.print(" auth=");
  AppLog.println(settings_->username.isEmpty() ? "anonymous" : "configured");

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
    authFailureCount_ = 0;
    authRetryLocked_ = false;
    publishAvailability(true);
    client_.subscribe(deviceCommandTopic().c_str());
    client_.subscribe(commandTopic("action").c_str());
    client_.subscribe(commandTopic("volume/set").c_str());
    client_.subscribe(commandTopic("output/set").c_str());
    client_.subscribe(commandTopic("playlist/set").c_str());
    client_.subscribe(commandTopic("settings/brightness/set").c_str());
    client_.subscribe(commandTopic("settings/dim_timeout/set").c_str());
    client_.subscribe(commandTopic("settings/deep_sleep/set").c_str());
    client_.subscribe(commandTopic("settings/speaker_volume/set").c_str());
    client_.subscribe(commandTopic("settings/language/set").c_str());
    client_.subscribe(commandTopic("settings/theme/set").c_str());
    client_.subscribe(commandTopic("settings/log_level/set").c_str());
    AppLog.println("MQTT command topics subscribed");
    discoveryPublished_ = false;
    publishRequested_ = true;
  } else {
    lastConnectCode_ = client_.state();
    if (lastConnectCode_ == MQTT_CONNECT_BAD_CREDENTIALS ||
        lastConnectCode_ == MQTT_CONNECT_UNAUTHORIZED) {
      authFailureCount_++;
      if (Logic::shouldLockMqttAuthRetries(lastConnectCode_, authFailureCount_, MaxAuthFailuresBeforeLock)) {
        authRetryLocked_ = true;
      }
    } else {
      authFailureCount_ = 0;
    }
    AppLog.print("MQTT connect failed ");
    AppLog.print(connectCodeText(lastConnectCode_));
    AppLog.print(" rc=");
    AppLog.print(lastConnectCode_);
    if (authRetryLocked_) {
      AppLog.print(" auth retries stopped after ");
      AppLog.print(authFailureCount_);
      AppLog.print(" failures");
    }
    AppLog.println();
  }
  return ok;
}

void MqttPublisher::resetAuthLockIfSettingsChanged() {
  if (settings_ == nullptr) {
    return;
  }
  const String signature = settings_->host + ":" +
                           String(settings_->port) + ":" +
                           settings_->username + ":" +
                           String(fnv1a(settings_->password), HEX);
  if (signature == settingsSignature_) {
    return;
  }
  settingsSignature_ = signature;
  authFailureCount_ = 0;
  authRetryLocked_ = false;
  lastConnectCode_ = 0;
  lastConnectAttemptAt_ = 0;
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
  publishButtonDiscovery("next", "Next song", "next");
  publishButtonDiscovery("previous", "Previous song", "previous");
  publishNumberDiscovery("volume_control", "Volume", commandTopic("volume/set").c_str(), 0, Config::MaxSpotifyVolumePercent, Config::VolumeStepPercent, "{{ value_json.device.volume }}");
  publishNumberDiscovery("screen_brightness", "Screen brightness", commandTopic("settings/brightness/set").c_str(), 25, 100, 25, "{{ value_json.settings.brightness }}");
  publishNumberDiscovery("screen_dim_timeout", "Screen dim timeout", commandTopic("settings/dim_timeout/set").c_str(), 30, 240, 30, "{{ value_json.settings.off_timeout_sec }}", "s");
  publishNumberDiscovery("turn_off_after", "Turn off after", commandTopic("settings/deep_sleep/set").c_str(), 5, 60, 5, "{{ value_json.settings.deep_sleep_min }}", "min");
  publishNumberDiscovery("speaker_volume", "Speaker volume", commandTopic("settings/speaker_volume/set").c_str(), 25, 100, 25, "{{ value_json.settings.speaker_volume }}");

  String languageOptions[2] = {"en", "nl"};
  publishSelectDiscovery("language", "Language", commandTopic("settings/language/set").c_str(), languageOptions, 2, "{{ value_json.settings.language }}");
  String themeOptions[3] = {"auto", "dark", "light"};
  publishSelectDiscovery("theme", "Theme", commandTopic("settings/theme/set").c_str(), themeOptions, 3, "{{ value_json.settings.theme }}");
  String logLevelOptions[4] = {"debug", "info", "warning", "error"};
  publishSelectDiscovery("log_level", "Log level", commandTopic("settings/log_level/set").c_str(), logLevelOptions, 4, "{{ value_json.settings.log_level }}");

  String outputOptions[8];
  size_t outputCount = 0;
  if (deviceList_ != nullptr && deviceList_->available) {
    for (size_t index = 0; index < deviceList_->count && outputCount < 8; index++) {
      if (!deviceList_->devices[index].name.isEmpty()) {
        outputOptions[outputCount++] = deviceList_->devices[index].name;
      }
    }
  }
  publishSelectDiscovery("sound_output", "Sound output", commandTopic("output/set").c_str(), outputOptions, outputCount, "{{ value_json.device.name }}");

  String playlistOptions[8];
  size_t playlistCount = 0;
  if (playlists_ != nullptr && playlists_->available) {
    for (size_t index = 0; index < playlists_->count && playlistCount < 8; index++) {
      if (!playlists_->items[index].name.isEmpty()) {
        playlistOptions[playlistCount++] = playlists_->items[index].name;
      }
    }
  }
  publishSelectDiscovery("playlist_start", "Start playlist", commandTopic("playlist/set").c_str(), playlistOptions, playlistCount, "{{ value_json.playback.last_playlist_command }}");

  discoveryPublished_ = true;
}

void MqttPublisher::updateDiscoveryOptionSignature() {
  String signature;
  if (deviceList_ != nullptr && deviceList_->available) {
    signature += "outputs:";
    for (size_t index = 0; index < deviceList_->count && index < 8; index++) {
      signature += deviceList_->devices[index].name;
      signature += "|";
    }
  }
  if (playlists_ != nullptr && playlists_->available) {
    signature += "playlists:";
    for (size_t index = 0; index < playlists_->count && index < 8; index++) {
      signature += playlists_->items[index].name;
      signature += "|";
    }
  }
  if (signature != discoveryOptionSignature_) {
    discoveryOptionSignature_ = signature;
    discoveryPublished_ = false;
  }
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
  playback["last_playlist_command"] = lastPlaylistCommand_;

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
  settings["speaker_volume"] = speakerVolumePercent_ == nullptr ? 100 : *speakerVolumePercent_;
  settings["language"] = languageCode_ == nullptr ? "en" : *languageCode_;
  settings["theme"] = themeCode_ == nullptr ? "auto" : *themeCode_;
  settings["log_level"] = logLevel_ == nullptr ? "info" : *logLevel_;
  settings["off_timeout_ms"] = screenOffTimeoutMs_ == nullptr ? 0 : *screenOffTimeoutMs_;
  settings["off_timeout_sec"] = screenOffTimeoutMs_ == nullptr ? 0 : (*screenOffTimeoutMs_ / 1000UL);
  settings["deep_sleep_ms"] = deviceSleepTimeoutMs_ == nullptr ? 0 : *deviceSleepTimeoutMs_;
  settings["deep_sleep_min"] = deviceSleepTimeoutMs_ == nullptr ? 0 : (*deviceSleepTimeoutMs_ / 60000UL);

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

  JsonObject dj = doc["dj"].to<JsonObject>();
  dj["last_dj_text"] = diagnostics_->lastDjText;

  String payload;
  serializeJson(doc, payload);
  const bool ok = client_.publish(stateTopic().c_str(), payload.c_str(), true);
  if (!ok) {
    AppLog.println("MQTT state publish failed");
  }
  lastPublishAt_ = millis();
  publishRequested_ = false;
}

void MqttPublisher::publishDeviceStatus() {
  if (!client_.connected() || battery_ == nullptr) {
    return;
  }

  JsonDocument doc;
  doc["device_id"] = deviceId_;
  doc["state"] = "online";
  doc["status"] = "online";
  doc["ota_state"] = "idle";
  doc["update_state"] = "idle";
  doc["firmware"] = Config::AppVersionNumber;
  doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["battery_percent"] = battery_->percent;
  doc["charging"] = battery_->charging || battery_->full;
  doc["paired"] = paired_;
  doc["spotify_configured"] = spotifyConfigured_;

  String payload;
  serializeJson(doc, payload);
  const bool ok = client_.publish(deviceStatusTopic().c_str(), payload.c_str(), true);
  if (!ok) {
    AppLog.println("MQTT status publish failed");
  }
  statusPublishRequested_ = false;
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
  if (!client_.publish(discoveryTopic("sensor", objectId).c_str(), payload.c_str(), true)) {
    AppLog.print("MQTT discovery publish failed: ");
    AppLog.println(objectId);
  }
}

void MqttPublisher::publishButtonDiscovery(const char *objectId, const char *name, const char *payload) {
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = String(DeviceId) + "_" + objectId;
  doc["command_topic"] = commandTopic("action");
  doc["payload_press"] = payload;
  doc["availability_topic"] = availabilityTopic();
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DeviceId;
  device["name"] = DeviceName;
  device["manufacturer"] = "LilyGO";
  device["model"] = "T-Embed-CC1101";
  device["sw_version"] = Config::AppVersionNumber;

  String config;
  serializeJson(doc, config);
  if (!client_.publish(discoveryTopic("button", objectId).c_str(), config.c_str(), true)) {
    AppLog.print("MQTT button discovery publish failed: ");
    AppLog.println(objectId);
  }
}

void MqttPublisher::publishNumberDiscovery(
    const char *objectId,
    const char *name,
    const char *topic,
    int minValue,
    int maxValue,
    int step,
    const char *valueTemplate,
    const char *unit) {
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = String(DeviceId) + "_" + objectId;
  doc["state_topic"] = stateTopic();
  doc["command_topic"] = topic;
  doc["availability_topic"] = availabilityTopic();
  doc["value_template"] = valueTemplate == nullptr ? "{{ value_json.device.volume }}" : valueTemplate;
  doc["min"] = minValue;
  doc["max"] = maxValue;
  doc["step"] = step;
  if (unit != nullptr && strlen(unit) > 0) {
    doc["unit_of_measurement"] = unit;
  }
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DeviceId;
  device["name"] = DeviceName;
  device["manufacturer"] = "LilyGO";
  device["model"] = "T-Embed-CC1101";
  device["sw_version"] = Config::AppVersionNumber;

  String config;
  serializeJson(doc, config);
  if (!client_.publish(discoveryTopic("number", objectId).c_str(), config.c_str(), true)) {
    AppLog.print("MQTT number discovery publish failed: ");
    AppLog.println(objectId);
  }
}

void MqttPublisher::publishSelectDiscovery(
    const char *objectId,
    const char *name,
    const char *topic,
    const String *options,
    size_t optionCount,
    const char *valueTemplate) {
  JsonDocument doc;
  doc["name"] = name;
  doc["unique_id"] = String(DeviceId) + "_" + objectId;
  doc["state_topic"] = stateTopic();
  doc["command_topic"] = topic;
  doc["availability_topic"] = availabilityTopic();
  doc["value_template"] = valueTemplate;
  JsonArray optionArray = doc["options"].to<JsonArray>();
  if (optionCount == 0) {
    optionArray.add("none");
  } else {
    for (size_t index = 0; index < optionCount; index++) {
      optionArray.add(options[index]);
    }
  }
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = DeviceId;
  device["name"] = DeviceName;
  device["manufacturer"] = "LilyGO";
  device["model"] = "T-Embed-CC1101";
  device["sw_version"] = Config::AppVersionNumber;

  String config;
  serializeJson(doc, config);
  if (!client_.publish(discoveryTopic("select", objectId).c_str(), config.c_str(), true)) {
    AppLog.print("MQTT select discovery publish failed: ");
    AppLog.println(objectId);
  }
}

void MqttPublisher::handleMessage(char *topic, uint8_t *payload, unsigned int length) {
  String topicText = topic == nullptr ? "" : String(topic);
  String body;
  for (unsigned int index = 0; index < length; index++) {
    body += static_cast<char>(payload[index]);
  }
  body.trim();

  MqttCommand command;
  if (topicText == deviceCommandTopic()) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      AppLog.println("MQTT device command JSON failed");
      return;
    }
    const String name = doc["command"] | "";
    AppLog.print("MQTT device command received: ");
    AppLog.println(name);
    if (name == "status") {
      command.type = MqttCommandType::Status;
    } else if (name == "ota") {
      command.type = MqttCommandType::Ota;
    } else if (name == "dj_response") {
      command.type = MqttCommandType::DjResponse;
      command.value = doc["text"] | "";
    }
  } else if (topicText == commandTopic("action")) {
    if (body == "next") {
      command.type = MqttCommandType::Next;
    } else if (body == "previous") {
      command.type = MqttCommandType::Previous;
    }
  } else if (topicText == commandTopic("volume/set")) {
    command.type = MqttCommandType::Volume;
    command.numericValue = constrain(body.toInt(), 0, Config::MaxSpotifyVolumePercent);
  } else if (topicText == commandTopic("output/set")) {
    command.type = MqttCommandType::TransferOutput;
    command.value = body;
  } else if (topicText == commandTopic("playlist/set")) {
    command.type = MqttCommandType::StartPlaylist;
    command.value = body;
    lastPlaylistCommand_ = body;
  } else if (topicText == commandTopic("settings/brightness/set")) {
    command.type = MqttCommandType::ScreenBrightness;
    command.numericValue = constrain(body.toInt(), 25, 100);
  } else if (topicText == commandTopic("settings/dim_timeout/set")) {
    command.type = MqttCommandType::ScreenDimTimeout;
    command.numericValue = constrain(body.toInt(), 30, 240);
  } else if (topicText == commandTopic("settings/deep_sleep/set")) {
    command.type = MqttCommandType::DeepSleepTimeout;
    command.numericValue = constrain(body.toInt(), 5, 60);
  } else if (topicText == commandTopic("settings/speaker_volume/set")) {
    command.type = MqttCommandType::SpeakerVolume;
    command.numericValue = constrain(body.toInt(), 25, 100);
  } else if (topicText == commandTopic("settings/language/set")) {
    command.type = MqttCommandType::Language;
    command.value = body;
  } else if (topicText == commandTopic("settings/theme/set")) {
    command.type = MqttCommandType::Theme;
    command.value = body;
  } else if (topicText == commandTopic("settings/log_level/set")) {
    command.type = MqttCommandType::LogLevel;
    command.value = body;
  }

  if (command.type == MqttCommandType::None) {
    AppLog.print("MQTT command ignored topic=");
    AppLog.println(topicText);
    return;
  }
  enqueueCommand(command);
}

void MqttPublisher::enqueueCommand(const MqttCommand &command) {
  pendingCommand_ = command;
  commandPending_ = true;
  AppLog.println("MQTT command queued");
}

void MqttPublisher::mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (activeMqttPublisher != nullptr) {
    activeMqttPublisher->handleMessage(topic, payload, length);
  }
}

String MqttPublisher::stateTopic() const {
  char buffer[96] = {};
  return Logic::formatMqttDeviceTopic(deviceId_.c_str(), "state", buffer, sizeof(buffer))
             ? String(buffer)
             : String("spotifydj/") + deviceId_ + "/state";
}

String MqttPublisher::availabilityTopic() const {
  char buffer[96] = {};
  return Logic::formatMqttDeviceTopic(deviceId_.c_str(), "availability", buffer, sizeof(buffer))
             ? String(buffer)
             : String("spotifydj/") + deviceId_ + "/availability";
}

String MqttPublisher::discoveryTopic(const char *objectId) const {
  return discoveryTopic("sensor", objectId);
}

String MqttPublisher::commandTopic(const char *suffix) const {
  return String("spotifydj/") + deviceId_ + "/command/" + suffix;
}

String MqttPublisher::deviceCommandTopic() const {
  char buffer[96] = {};
  return Logic::formatMqttDeviceTopic(deviceId_.c_str(), "command", buffer, sizeof(buffer))
             ? String(buffer)
             : String("spotifydj/") + deviceId_ + "/command";
}

String MqttPublisher::deviceStatusTopic() const {
  char buffer[96] = {};
  return Logic::formatMqttDeviceTopic(deviceId_.c_str(), "status", buffer, sizeof(buffer))
             ? String(buffer)
             : String("spotifydj/") + deviceId_ + "/status";
}

String MqttPublisher::deviceEventTopic() const {
  char buffer[96] = {};
  return Logic::formatMqttDeviceTopic(deviceId_.c_str(), "event", buffer, sizeof(buffer))
             ? String(buffer)
             : String("spotifydj/") + deviceId_ + "/event";
}

String MqttPublisher::discoveryTopic(const char *component, const char *objectId) const {
  return String("homeassistant/") + component + "/" + DeviceId + "/" + objectId + "/config";
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

// Mobile web dashboard and OTA firmware update endpoint.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "AppState.h"
#include "MqttPublisher.h"
#include "SpotifyClient.h"

class WebPortal {
public:
  using SettingsCallback = void (*)(void *context, uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs);
  using MqttSettingsCallback = void (*)(void *context, const MqttSettings &settings);
  using WifiSettingsCallback = void (*)(void *context, const String &ssid, const String &password);
  using SimpleCallback = void (*)(void *context);

  // Starts the HTTP server and binds it to the live app state snapshots.
  void begin(
      const SpotifyState &playback,
      const BatteryState &battery,
      const RuntimeDiagnostics &diagnostics,
      const VisualState &visualState,
      SpotifyClient &spotify,
      MqttPublisher &mqttPublisher,
      const MqttSettings &mqttSettings,
      const uint8_t &screenBrightnessPercent,
      const uint32_t &screenOffTimeoutMs,
      const uint32_t &deviceSleepTimeoutMs,
      void *callbackContext,
      SettingsCallback settingsCallback,
      MqttSettingsCallback mqttSettingsCallback,
      WifiSettingsCallback wifiSettingsCallback,
      SimpleCallback refreshCallback,
      SimpleCallback hardResetCallback);

  // Handles one HTTP client iteration. Call this frequently from the main loop.
  void handle();

  bool isRunning() const;

private:
  void configureRoutes();
  void handleRoot();
  void handleStatusJson();

  // Returns the live in-memory serial/framework log buffer as text/plain.
  void handleLogsText();

  // Saves display, deep-sleep, and MQTT settings submitted by the dashboard form.
  void handleSettingsPost();
  void handleWifiPost();
  void handleVolumePost();
  void handleDevicesJson();
  void handleQueueJson();
  void handleTransferPost();
  void handlePlaybackCommandPost();
  void handleRefreshPost();
  void handleRebootPost();
  void handleHardResetPost();
  void handleOtaFinished();
  void handleOtaUpload();
  void handleNotFound();

  String maskedSecret(const char *value) const;
  String wifiStatusText() const;
  String batteryLabel() const;
  String formatBytes(uint32_t bytes) const;
  uint32_t estimatedProgressMs() const;

  WebServer server_{80};
  const SpotifyState *playback_ = nullptr;
  const BatteryState *battery_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  const VisualState *visualState_ = nullptr;
  SpotifyClient *spotify_ = nullptr;
  MqttPublisher *mqttPublisher_ = nullptr;
  const MqttSettings *mqttSettings_ = nullptr;
  const uint8_t *screenBrightnessPercent_ = nullptr;
  const uint32_t *screenOffTimeoutMs_ = nullptr;
  const uint32_t *deviceSleepTimeoutMs_ = nullptr;
  void *callbackContext_ = nullptr;
  SettingsCallback settingsCallback_ = nullptr;
  MqttSettingsCallback mqttSettingsCallback_ = nullptr;
  WifiSettingsCallback wifiSettingsCallback_ = nullptr;
  SimpleCallback refreshCallback_ = nullptr;
  SimpleCallback hardResetCallback_ = nullptr;
  bool running_ = false;
  bool otaOk_ = false;
};

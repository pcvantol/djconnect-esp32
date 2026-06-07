// Mobile web dashboard and OTA firmware update endpoint.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "AppState.h"
#include "DisplayManager.h"
#include "LedRing.h"
#include "SoundManager.h"
#include "SpotifyClient.h"

class WebPortal {
public:
  using SettingsCallback = void (*)(
      void *context,
      uint8_t brightnessPercent,
      uint32_t offTimeoutMs,
      uint32_t sleepTimeoutMs,
      uint8_t speakerVolumePercent,
      const String &languageCode,
      const String &themeCode,
      const String &logLevel);
  using WifiSettingsCallback = void (*)(void *context, const String &ssid, const String &password);
  using VoiceTextCallback = bool (*)(void *context, const String &text, String &message, String &audioUrl);
  using SimpleCallback = void (*)(void *context);

  // Starts the HTTP server and binds it to the live app state snapshots.
  void begin(
      const SpotifyState &playback,
      const BatteryState &battery,
      const RuntimeDiagnostics &diagnostics,
      const VisualState &visualState,
      SpotifyClient &spotify,
      LedRing &ledRing,
      DisplayManager &display,
      SoundManager &sound,
      const uint8_t &screenBrightnessPercent,
      const uint8_t &speakerVolumePercent,
      const bool &homeAssistantPaired,
      const String &languageCode,
      const String &themeCode,
      const String &logLevel,
      const uint32_t &screenOffTimeoutMs,
      const uint32_t &deviceSleepTimeoutMs,
      void *callbackContext,
      SettingsCallback settingsCallback,
      WifiSettingsCallback wifiSettingsCallback,
      VoiceTextCallback voiceTextCallback,
      SimpleCallback refreshCallback,
      SimpleCallback resetPairingCallback,
      SimpleCallback hardResetCallback);

  // Handles one HTTP client iteration. Call this frequently from the main loop.
  void handle();

  bool isRunning() const;

  // Exposes the shared HTTP server so feature modules can register extra API routes on port 80.
  WebServer &server();

private:
  void configureRoutes();
  void handleRoot();
  void handleStatusJson();
  void handleDiagnosticsJson();

  // Returns the live in-memory serial/framework log buffer as text/plain.
  void handleLogsText();

  // Saves display and deep-sleep settings submitted by the dashboard form.
  void handleSettingsPost();
  void handlePlayModePost();
  void handleWifiPost();
  void handleVolumePost();
  void handleDevicesJson();
  void handlePlaylistsJson();
  void handleQueueJson();
  void handleTransferPost();
  void handlePlaybackCommandPost();
  void handleVoiceTextPost();
  void handleRefreshPost();
  void handleResetPairingPost();
  void handleRebootPost();
  void handleHardResetPost();
  void handleOtaFinished();
  void handleOtaUpload();
  void handleNotFound();

  // Shared helpers for Spotify-only routes so unavailable playback never looks like a portal error.
  void sendSpotifyUnavailableText();
  void sendSpotifyUnavailableJson(const char *arrayKey);

  String maskedSecret(const char *value) const;
  String wifiStatusText() const;
  String batteryLabel() const;
  String appVersionLabel() const;
  String formatBytes(uint32_t bytes) const;

  // Keeps small portal strings localized without pulling the full device i18n table into HTML.
  String localizedText(const char *en, const char *nl) const;
  uint32_t estimatedProgressMs() const;

  WebServer server_{80};
  const SpotifyState *playback_ = nullptr;
  const BatteryState *battery_ = nullptr;
  const RuntimeDiagnostics *diagnostics_ = nullptr;
  const VisualState *visualState_ = nullptr;
  SpotifyClient *spotify_ = nullptr;
  LedRing *ledRing_ = nullptr;
  DisplayManager *display_ = nullptr;
  SoundManager *sound_ = nullptr;
  const uint8_t *screenBrightnessPercent_ = nullptr;
  const uint8_t *speakerVolumePercent_ = nullptr;
  const bool *homeAssistantPaired_ = nullptr;
  const String *languageCode_ = nullptr;
  const String *themeCode_ = nullptr;
  const String *logLevel_ = nullptr;
  const uint32_t *screenOffTimeoutMs_ = nullptr;
  const uint32_t *deviceSleepTimeoutMs_ = nullptr;
  void *callbackContext_ = nullptr;
  SettingsCallback settingsCallback_ = nullptr;
  WifiSettingsCallback wifiSettingsCallback_ = nullptr;
  VoiceTextCallback voiceTextCallback_ = nullptr;
  SimpleCallback refreshCallback_ = nullptr;
  SimpleCallback resetPairingCallback_ = nullptr;
  SimpleCallback hardResetCallback_ = nullptr;
  bool running_ = false;
  bool otaOk_ = false;
  size_t otaUploadedBytes_ = 0;
  size_t otaLastProgressCue_ = 0;
};

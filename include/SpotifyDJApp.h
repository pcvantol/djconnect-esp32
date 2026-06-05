#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "AlbumArtManager.h"
#include "BleWifiProvisioning.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DisplayManager.h"
#include "InputController.h"
#include "I18n.h"
#include "LedRing.h"
#include "MqttPublisher.h"
#include "PowerController.h"
#include "ProvisioningController.h"
#include "SoundManager.h"
#include "SoftResetMonitor.h"
#include "SpotifyDJMenu.h"
#include "SpotifyClient.h"
#include "../src/SpotifyDJAssistClient.h"
#include "../src/SpotifyDJApiServer.h"
#include "../src/SpotifyDJDevice.h"
#include "../src/SpotifyDJDiscovery.h"
#include "../src/SpotifyDJOTA.h"
#include "../src/SpotifyDJPairing.h"
#include "VoiceHttpClient.h"
#include "VoiceRecorder.h"
#include "VoiceState.h"
#include "WebPortal.h"

class SpotifyDJApp {
public:
  // Initializes hardware, connects WiFi, authorizes Spotify, and draws the first screen.
  void begin();

  // Runs input handling, queued Spotify work, periodic refreshes, and display power management.
  void loop();

private:
  // Connects to WiFi and performs the TLS clock sync when secure Spotify TLS is enabled.
  void loadProvisioning();
  bool shouldStartProvisioningPortal() const;
  bool connectWiFi(uint32_t timeoutMs = Config::WifiConnectTimeoutMs, bool bootScreen = false);
  void handleWifiConnectFailureLoop(uint32_t loopStartedAt);
  void renderWifiConnectFailureMenu();
  void applyWifiConnectFailureSelection();
  bool syncClock();
  void startWebPortalIfNeeded();
  void runCaptivePortal();
  String captivePortalPage(
      const String &message,
      bool error,
      const String &ssid = "",
      const String &clientId = "",
      const String &refreshToken = "",
      const String &spotifyMarket = "NL",
      const MqttSettings &mqttSettings = MqttSettings()) const;
  bool testAndSaveProvisioning(
      const String &ssid,
      const String &password,
      const String &clientId,
      const String &refreshToken,
      const String &spotifyMarket,
      const MqttSettings &mqttSettings,
      String &message);
  bool handleBleProvisioningPayload(const String &payload, String &message);

  // Routes physical input events to the Spotify actions or refresh commands they trigger.
  void handleInputEvents(const InputEvents &events);
  void handlePlaybackInputEvents(const InputEvents &events);
  void handleMenuInputEvents(const InputEvents &events);
  void handleEncoderTurn(int encoderSteps);

  // Menu navigation helpers.
  void openRootMenu();
  void openScreen(UiScreen screen);
  void goBackOneScreen();
  void moveMenuSelection(int encoderSteps);
  void selectCurrentMenuItem();
  // Applies selected display dim timeout and persists it to NVS.
  void applyDimTimeoutSelection();

  // Applies selected active screen brightness and persists it to NVS.
  void applyBrightnessSelection();

  // Applies selected on-board speaker cue volume and persists it to NVS.
  void applySpeakerVolumeSelection();

  // Applies selected Spotify shuffle/repeat play mode.
  void applyPlayModeSelection();

  // Applies selected deep-sleep idle timeout and persists it to NVS.
  void applySleepTimeoutSelection();

  // Stores display/sleep settings shared by device menu and web dashboard.
  void saveDisplaySettings();

  // Clears local credentials/tokens/caches and reboots into provisioning AP mode.
  void hardResetToProvisioning();
  void resetHomeAssistantPairing();
  bool isMenuActive() const;
  size_t menuItemCount(UiScreen screen) const;
  size_t selectedIndexForScreen(UiScreen screen) const;
  size_t &selectedIndexRefForScreen(UiScreen screen);
  void applyTheme();

  // Sends the latest encoder volume change after the user stops turning the knob.
  void flushPendingVolume();
  void processVolumeResult();
  void processMqttCommands();
  void handleMqttCommand(const MqttCommand &command);
  bool transferToOutputByNameOrId(const String &output);
  bool startPlaylistByNameOrUri(const String &playlist);

  // Playback commands exposed by the current playback screen.
  void pauseOrResume();
  void startLikedProxyPlaylist();
  void handleVoiceButton();
  void stopVoiceRecordingAndSendText();
  void goToNextTrack();
  void goToPreviousTrack();
  void refreshPlaybackAndBattery();
  void refreshMqttControlLists();
  void openAlbumArtScreen();
  void openQueueScreen();
  void startSelectedPlaylist();
  void openSoundOutputsScreen();
  void transferToSelectedOutput();

  // Periodic background polling for data shown on the playback screen.
  void pollBatteryIfDue();
  void pollPlaybackIfDue();

  // Updates the display and LED ring from the current state snapshot.
  void renderNow();
  bool connectionHealthy();
  void renderMenuNow();
  bool chargerConnected() const;
  bool shouldReturnToSleepAfterTimerWake() const;
  bool updateLowBatteryGuard();
  void renderLowBatteryGuard();
  void evaluateBatteryTransition();
  // Builds the compact live status snapshot shown on the About screen.
  AboutStatus aboutStatus();

  // Applies idle brightness policy and returns to Now Playing when the screen turns off.
  void updateVisualPower();

  // Publishes offline state, configures wake GPIOs, and enters ESP32-S3 deep sleep.
  void configureWatchdog();
  void serviceWatchdog();
  void responsiveDelay(uint32_t durationMs);
  void logHeapIfDue();
  void enterDeepSleep();
  void enterDeepSleepWithoutDisplay();
  void recordLoopMetrics(uint32_t loopStartedAt);
  // Applies settings posted from the web dashboard and persists them.
  void applyWebSettings(uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs, uint8_t speakerVolumePercent, const String &languageCode, const String &themeCode);
  void applyWebMqttSettings(const MqttSettings &settings);
  void syncHomeAssistantMqttSettings();
  void requestWebWifiSettings(const String &ssid, const String &password);
  void processPendingWifiSettings();
  bool handleDjResponseText(const String &text, const String &audioUrl, bool &spoken);
  bool playDjResponseAudioUrl(const String &audioUrl);
  void applyProvisionedLanguage(const String &languageCode);
  void applyProvisionedSpotifyCredentials();
  void setupHomeAssistantLayer();
  void sendHomeAssistantStatusIfDue(bool force = false);
  bool handleHomeAssistantPairingMode(uint32_t loopStartedAt);
  static void applyWebSettingsCallback(
      void *context,
      uint8_t brightnessPercent,
      uint32_t offTimeoutMs,
      uint32_t sleepTimeoutMs,
      uint8_t speakerVolumePercent,
      const String &languageCode,
      const String &themeCode);
  static void applyWebMqttSettingsCallback(void *context, const MqttSettings &settings);
  static void applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password);
  static bool sendWebVoiceTextCallback(void *context, const String &text, String &message, String &audioUrl);
  static void refreshFromWebCallback(void *context);
  static void resetPairingFromWebCallback(void *context);
  static void hardResetFromWebCallback(void *context);
  static bool djResponseCallback(void *context, const String &text, const String &audioUrl, bool &spoken);
  static void languageProvisionedCallback(void *context, const String &languageCode);
  static void spotifyProvisionedCallback(void *context);
  void showNotice(const String &message, uint32_t ttlMs = 2500);
  int displayedVolume() const;

  SpotifyState playback_;
  BatteryState battery_;
  QueueState queue_;
  PlaylistListState playlists_;
  DeviceListState deviceList_;
  StatusNotice notice_;
  DisplayManager display_;
  BatteryMonitor batteryMonitor_{battery_};
  AlbumArtManager albumArt_;
  BleWifiProvisioning bleProvisioning_;
  InputController input_;
  LedRing ledRing_;
  SoundManager sound_;
  VoiceRecorder voiceRecorder_;
  SpotifyDJAssistClient assistClient_;
  VoiceHttpClient voiceClient_;
  SoftResetMonitor softResetMonitor_;
  SpotifyClient spotify_{playback_};
  WebPortal webPortal_;
  SpotifyDJDevice haDevice_;
  SpotifyDJDiscovery haDiscovery_;
  SpotifyDJPairing haPairing_;
  SpotifyDJApiServer haApiServer_;
  SpotifyDJOTA haOta_;
  MqttPublisher mqttPublisher_;
  PowerController power_;
  ProvisioningController provisioning_;
  RuntimeDiagnostics diagnostics_;
  VisualState visualState_;

  UiScreen activeScreen_ = UiScreen::NowPlaying;
  UiScreen menuStack_[SpotifyDJMenu::MenuStackCapacity] = {};
  size_t menuStackSize_ = 0;
  size_t rootMenuSelection_ = 0;
  size_t settingsSelection_ = 0;
  size_t playlistSelection_ = 0;
  size_t aboutSelection_ = 0;
  size_t dimTimeoutSelection_ = 1;
  size_t brightnessSelection_ = 3;
  size_t languageSelection_ = 0;
  size_t themeSelection_ = 0;
  size_t speakerVolumeSelection_ = 3;
  size_t playModeSelection_ = 0;
  size_t sleepTimeoutSelection_ = 0;
  size_t hardResetSelection_ = 0;
  size_t wifiFailureSelection_ = 0;
  size_t soundOutputSelection_ = 0;
  uint32_t screenOffTimeoutMs_ = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs_ = Config::DeviceSleepAfterMs;
  uint8_t screenBrightnessPercent_ = 100;
  uint8_t speakerVolumePercent_ = 100;
  Language language_ = Language::English;
  String languageCode_ = "en";
  String themeCode_ = "dark";
  String wifiSsid_;
  String wifiPassword_;
  String pendingWifiSsid_;
  String pendingWifiPassword_;
  MqttSettings mqttSettings_;
  bool setupModeRequested_ = false;
  bool suppressInputUntilRelease_ = false;
  bool deepSleepStarted_ = false;
  bool pendingWifiSettings_ = false;
  bool pendingWifiPasswordProvided_ = false;
  bool wifiConnectFailed_ = false;
  bool lowBatteryGuardActive_ = false;
  bool criticalBatteryGuardActive_ = false;
  bool chargingBatteryGuardActive_ = false;
  bool lowBatteryTimerWake_ = false;
  bool chargingCompleteSoundPlayed_ = false;
  bool volumeFeedbackEnabled_ = true;
  bool voiceRecording_ = false;
  VoiceState voiceState_ = VoiceState::Idle;
  bool topHoldMenuHintVisible_ = false;
  bool menuTopHoldActive_ = false;
  bool haPairingScreenActive_ = false;
  uint32_t pendingWifiSettingsRequestedAt_ = 0;
  uint32_t menuTopHoldStartedAt_ = 0;
  uint32_t wifiConnectFailedAt_ = 0;
  uint32_t lowBatteryGuardStartedAt_ = 0;
  int lastBatteryPercent_ = -1;

  int pendingVolume_ = -1;
  uint32_t pendingVolumeChangedAt_ = 0;
  uint32_t lastPlaybackPollAt_ = 0;
  uint32_t lastBatteryPollAt_ = 0;
  uint32_t lastPauseToggleAt_ = 0;
  uint32_t lastReconnectAttemptAt_ = 0;
  uint32_t lastLogsRenderAt_ = 0;
  uint32_t lastHaStatusAt_ = 0;
  uint32_t lastMqttProvisioningSyncAt_ = 0;
  uint32_t haPairingStartedAt_ = 0;
  uint32_t lastHaPairingScreenAt_ = 0;
  uint32_t loopMetricsWindowStartedAt_ = 0;
  uint32_t loopMetricsBusyMs_ = 0;
  uint32_t lastHeapLogAt_ = 0;
  uint32_t lastSlowLoopLogAt_ = 0;
};

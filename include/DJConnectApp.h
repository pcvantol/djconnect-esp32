#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "AlbumArtManager.h"
#include "BleWifiProvisioning.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DjResponseAudioPlayer.h"
#include "DisplayManager.h"
#include "InputController.h"
#include "I18n.h"
#include "LedRing.h"
#include "PowerController.h"
#include "ProvisioningController.h"
#include "SoundManager.h"
#include "SoftResetMonitor.h"
#include "DJConnectMenu.h"
#include "SpotifyClient.h"
#include "../src/DJConnectApiServer.h"
#include "../src/DJConnectDevice.h"
#include "../src/DJConnectDiscovery.h"
#include "../src/DJConnectOTA.h"
#include "../src/DJConnectPairing.h"
#include "VoiceHttpClient.h"
#include "VoiceRecorder.h"
#include "VoiceState.h"
#include "WakeWordEngine.h"
#include "WebPortal.h"

class DJConnectApp {
public:
  // Initializes hardware, connects WiFi, links the HA playback proxy, and draws the first screen.
  void begin();

  // Runs input handling, queued playback work, periodic refreshes, and display power management.
  void loop();

private:
  // Loads stored setup settings before WiFi, HA and UI startup.
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
      const String &ssid = "") const;
  bool testAndSaveProvisioning(
      const String &ssid,
      const String &password,
      String &message);
  bool handleBleProvisioningPayload(const String &payload, String &message);

  // Routes physical input events to playback actions or refresh commands they trigger.
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

  // Applies selected playback shuffle state through the Home Assistant proxy.
  void applyShuffleSelection();

  // Applies selected playback repeat state through the Home Assistant proxy.
  void applyRepeatSelection();

  // Applies selected log level and persists it to NVS.
  void applyLogLevelSelection();

  // Applies selected deep-sleep idle timeout and persists it to NVS.
  void applySleepTimeoutSelection();

  // Stores display/sleep settings shared by device menu and web dashboard.
  void saveDisplaySettings();

  // Reboots into the WiFi setup portal without clearing Home Assistant pairing.
  void changeWifiToProvisioning();

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
  bool handleDeviceCommand(const DeviceCommand &command, String &message);
  void processStressTest();
  void toggleStressTest();
  void stopStressTest(const String &reason);
  void runStressTestStep();
  void resetPong();
  void updatePong();
  void resetAsteroids();
  void updateAsteroids();
  void fireAsteroids();
  void resetFlyer();
  void updateFlyer();
  void fireFlyer();
  void updateGameHighScore(int &highScore, int score);
  bool transferToOutputByNameOrId(const String &output);
  bool startPlaylistByNameOrUri(const String &playlist);

  // Playback commands exposed by the current playback screen.
  void pauseOrResume();
  void startLikedProxyPlaylist();
  void handleVoiceButton();
  void stopVoiceRecordingAndSendText();
  void cancelVoiceFlow(const char *reason);
  void goToNextTrack();
  void goToPreviousTrack();
  void refreshPlaybackAndBattery();
  void openAlbumArtScreen();
  void openQueueScreen();
  void startSelectedQueueItem();
  void startSelectedPlaylist();
  void openSoundOutputsScreen();
  void transferToSelectedOutput();
  bool playbackProxyReady() const;
  PlaybackConnectionState playbackConnectionState() const;

  // Periodic background polling for data shown on the playback screen.
  void pollBatteryIfDue();
  void pollPlaybackIfDue();

  // Updates the display and LED ring from the current state snapshot.
  void renderNow();

  // Hides the temporary DJ response overlay when it times out or the user acknowledges it.
  bool dismissDjResponseOverlay();
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
  void applyWebSettings(uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs, uint8_t speakerVolumePercent, const String &languageCode, const String &themeCode, const String &logLevel);
  void requestWebWifiSettings(const String &ssid, const String &password);
  void processPendingWifiSettings();

  // Shows HA DJ response text and optionally plays the HA-generated WAV/MP3 response URL.
  bool handleDjResponseText(const String &text, const String &audioUrl, bool &spoken);
  void showDjResponseOverlay(const String &title, const String &text, uint32_t ttlMs);

  // Applies credentials and language pushed by Home Assistant without touching local fallbacks.
  void applyProvisionedLanguage(const String &languageCode);
  bool checkBootstrapFirmwareUpdate();
  void setupHomeAssistantLayer();
  void sendHomeAssistantStatusIfDue(bool force = false);
  void markHomeAssistantPairingInvalid(const String &message);
  bool handleHomeAssistantPairingMode(uint32_t loopStartedAt);
  static void applyWebSettingsCallback(
      void *context,
      uint8_t brightnessPercent,
      uint32_t offTimeoutMs,
      uint32_t sleepTimeoutMs,
      uint8_t speakerVolumePercent,
      const String &languageCode,
      const String &themeCode,
      const String &logLevel);
  static void applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password);
  static bool sendWebVoiceTextCallback(void *context, const String &text, String &message, String &audioUrl);
  static void wakeWordDetectedCallback(void *context);
  static void voiceActivityCallback(void *context);
  static void softResetCueCallback(void *context);
  static void refreshFromWebCallback(void *context);
  static void resetPairingFromWebCallback(void *context);
  static void hardResetFromWebCallback(void *context);
  static bool djResponseCallback(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType);
  static void languageProvisionedCallback(void *context, const String &languageCode);
  static bool deviceCommandCallback(void *context, const DeviceCommand &command, String &message);
  static void directPairCallback(void *context);
  void noteDirectPairingReceived();
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
  DjResponseAudioPlayer djAudio_;
  VoiceRecorder voiceRecorder_;
  WakeWordEngine wakeWord_;
  VoiceHttpClient voiceClient_;
  SoftResetMonitor softResetMonitor_;
  SpotifyClient spotify_{playback_};
  WebPortal webPortal_;
  DJConnectDevice haDevice_;
  DJConnectDiscovery haDiscovery_;
  DJConnectPairing haPairing_;
  DJConnectApiServer haApiServer_;
  DJConnectOTA haOta_;
  PowerController power_;
  ProvisioningController provisioning_;
  RuntimeDiagnostics diagnostics_;
  VisualState visualState_;
  String lastDjAudioType_ = "none";

  UiScreen activeScreen_ = UiScreen::NowPlaying;
  UiScreen menuStack_[DJConnectMenu::MenuStackCapacity] = {};
  size_t menuStackSize_ = 0;
  size_t rootMenuSelection_ = 0;
  size_t settingsSelection_ = 0;
  size_t playlistSelection_ = 0;
  size_t queueSelection_ = 0;
  size_t aboutSelection_ = 0;
  size_t gamesSelection_ = 0;
  size_t helpSelection_ = 0;
  size_t dimTimeoutSelection_ = 1;
  size_t brightnessSelection_ = 3;
  size_t languageSelection_ = 0;
  size_t themeSelection_ = 0;
  size_t logLevelSelection_ = 1;
  size_t speakerVolumeSelection_ = 3;
  size_t shuffleSelection_ = 0;
  size_t repeatSelection_ = 0;
  size_t sleepTimeoutSelection_ = 0;
  size_t hardResetSelection_ = 0;
  size_t wifiFailureSelection_ = 0;
  bool wifiFailureConfirmHardReset_ = false;
  size_t soundOutputSelection_ = 0;
  int pongPaddleY_ = 86;
  int pongBallX_ = 160;
  int pongBallY_ = 86;
  int pongVelocityX_ = 3;
  int pongVelocityY_ = 2;
  int pongScore_ = 0;
  int pongHighScore_ = 0;
  uint32_t pongMissFlashUntil_ = 0;
  int asteroidShipX_ = 160;
  int asteroidShipY_ = 138;
  int asteroidX_ = 160;
  int asteroidY_ = 48;
  int asteroidVelocityX_ = 2;
  int asteroidVelocityY_ = 2;
  int asteroidBulletY_ = 0;
  int asteroidScore_ = 0;
  int asteroidHighScore_ = 0;
  bool asteroidBulletActive_ = false;
  uint32_t asteroidFlashUntil_ = 0;
  int flyerPlaneY_ = 86;
  int flyerObstacleX_ = 300;
  int flyerObstacleY_ = 86;
  int flyerShotX_ = 0;
  int flyerScore_ = 0;
  int flyerHighScore_ = 0;
  bool flyerShotActive_ = false;
  uint32_t flyerFlashUntil_ = 0;
  uint32_t screenOffTimeoutMs_ = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs_ = Config::DeviceSleepAfterMs;
  uint8_t screenBrightnessPercent_ = 100;
  uint8_t speakerVolumePercent_ = 100;
  Language language_ = Language::English;
  String languageCode_ = "en";
  String themeCode_ = "dark";
  String logLevel_ = "info";
  bool homeAssistantPaired_ = false;
  String wifiSsid_;
  String wifiPassword_;
  String pendingWifiSsid_;
  String pendingWifiPassword_;
  bool setupModeRequested_ = false;
  bool helpShown_ = false;
  bool suppressInputUntilRelease_ = false;
  bool deepSleepStarted_ = false;
  bool pendingWifiSettings_ = false;
  bool pendingWifiPasswordProvided_ = false;
  bool djResponseOverlayVisible_ = false;
  String djResponseOverlayTitle_;
  bool wifiConnectFailed_ = false;
  bool lowBatteryGuardActive_ = false;
  bool criticalBatteryGuardActive_ = false;
  bool chargingBatteryGuardActive_ = false;
  bool lowBatteryTimerWake_ = false;
  bool chargingCompleteSoundPlayed_ = false;
  bool volumeFeedbackEnabled_ = true;
  bool stressTestActive_ = false;
  bool voiceRecording_ = false;
  bool voiceStopPending_ = false;
  bool voiceStartedByWakeWord_ = false;
  bool nextVoiceStartFromWakeWord_ = false;
  volatile bool voiceCancelRequested_ = false;
  VoiceState voiceState_ = VoiceState::Idle;
  bool webVoiceTextOnlyActive_ = false;
  bool webVoiceTextOnlyConsumeNext_ = false;
  uint32_t webVoiceTextOnlyUntil_ = 0;
  bool topHoldMenuHintVisible_ = false;
  bool menuTopHoldActive_ = false;
  bool haPairingScreenActive_ = false;
  bool haPairingPendingValidation_ = false;
  bool playbackRefreshAfterPairing_ = false;
  uint32_t pendingWifiSettingsRequestedAt_ = 0;
  uint32_t voiceSilenceStartedAt_ = 0;
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
  size_t logsScrollBack_ = 0;
  uint32_t lastHaStatusAt_ = 0;
  uint32_t haPairingStartedAt_ = 0;
  uint32_t lastHaPairingScreenAt_ = 0;
  uint32_t lastWakeWordGateLogAt_ = 0;
  uint32_t djResponseOverlayUntil_ = 0;
  uint32_t loopMetricsWindowStartedAt_ = 0;
  uint32_t loopMetricsBusyMs_ = 0;
  uint32_t lastHeapLogAt_ = 0;
  uint32_t lastSlowLoopLogAt_ = 0;
  uint32_t stressTestStartedAt_ = 0;
  uint32_t lastStressTestStepAt_ = 0;
  uint32_t lastPongFrameAt_ = 0;
  uint32_t lastAsteroidsFrameAt_ = 0;
  uint32_t lastFlyerFrameAt_ = 0;
  uint32_t stressTestStepCount_ = 0;
  uint32_t heapTrendBaselineMinFree_ = 0;
  uint32_t heapTrendBaselineLargestBlock_ = 0;
  uint32_t heapTrendPreviousMinFree_ = 0;
  uint32_t heapTrendPreviousLargestBlock_ = 0;
};

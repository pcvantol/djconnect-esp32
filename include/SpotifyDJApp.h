#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "AlbumArtManager.h"
#include "BatteryMonitor.h"
#include "Config.h"
#include "DisplayManager.h"
#include "InputController.h"
#include "LedRing.h"
#include "MqttPublisher.h"
#include "SoundManager.h"
#include "SoftResetMonitor.h"
#include "SpotifyClient.h"
#include "../src/SpotifyDJApiServer.h"
#include "../src/SpotifyDJDevice.h"
#include "../src/SpotifyDJDiscovery.h"
#include "../src/SpotifyDJOTA.h"
#include "../src/SpotifyDJPairing.h"
#include "WebPortal.h"

class SpotifyDJApp {
public:
  // Initializes hardware, connects WiFi, authorizes Spotify, and draws the first screen.
  void begin();

  // Runs input handling, queued Spotify work, periodic refreshes, and display power management.
  void loop();

private:
  enum class UiScreen {
    NowPlaying,
    AlbumArt,
    Queue,
    SoundOutputs,
    Logs,
    RootMenu,
    About,
    Settings,
    DimTimeout,
    Brightness,
    SleepTimeout,
    HardResetConfirm,
  };

  // Connects to WiFi and performs the TLS clock sync when secure Spotify TLS is enabled.
  void loadProvisioning();
  bool shouldStartProvisioningPortal() const;
  bool connectWiFi(uint32_t timeoutMs = Config::WifiConnectTimeoutMs, bool bootScreen = false);
  void handleWifiConnectFailureLoop(uint32_t loopStartedAt);
  bool syncClock();
  void startWebPortalIfNeeded();
  void runCaptivePortal();
  String captivePortalPage(const String &message, bool error) const;
  bool testAndSaveProvisioning(
      const String &ssid,
      const String &password,
      const String &clientId,
      const String &refreshToken,
      const String &spotifyMarket,
      const MqttSettings &mqttSettings,
      String &message);

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

  // Applies selected deep-sleep idle timeout and persists it to NVS.
  void applySleepTimeoutSelection();

  // Stores display/sleep settings shared by device menu and web dashboard.
  void saveDisplaySettings();

  // Clears local credentials/tokens/caches and reboots into provisioning AP mode.
  void hardResetToProvisioning();
  bool isMenuActive() const;
  size_t menuItemCount(UiScreen screen) const;
  size_t selectedIndexForScreen(UiScreen screen) const;
  size_t &selectedIndexRefForScreen(UiScreen screen);
  uint32_t dimTimeoutValueMs(size_t index) const;
  uint8_t brightnessValuePercent(size_t index) const;
  uint32_t sleepTimeoutValueMs(size_t index) const;

  // Sends the latest encoder volume change after the user stops turning the knob.
  void flushPendingVolume();
  void processVolumeResult();

  // Playback commands exposed by the current playback screen.
  void pauseOrResume();
  void goToNextTrack();
  void goToPreviousTrack();
  void refreshPlaybackAndBattery();
  void openAlbumArtScreen();
  void openQueueScreen();
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
  bool updateLowBatteryGuard();
  void renderLowBatteryGuard();
  void evaluateBatteryTransition();
  // Builds the compact live status snapshot shown on the About screen.
  AboutStatus aboutStatus();

  // Applies idle brightness policy and returns to Now Playing when the screen turns off.
  void updateVisualPower();

  // Publishes offline state, configures wake GPIOs, and enters ESP32-S3 deep sleep.
  void enterDeepSleep();
  void recordLoopMetrics(uint32_t loopStartedAt);
  // Applies settings posted from the web dashboard and persists them.
  void applyWebSettings(uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs);
  void applyWebMqttSettings(const MqttSettings &settings);
  void requestWebWifiSettings(const String &ssid, const String &password);
  void processPendingWifiSettings();
  void setupHomeAssistantLayer();
  void sendHomeAssistantStatusIfDue(bool force = false);
  bool handleHomeAssistantPairingMode(uint32_t loopStartedAt);
  static void applyWebSettingsCallback(void *context, uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs);
  static void applyWebMqttSettingsCallback(void *context, const MqttSettings &settings);
  static void applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password);
  static void refreshFromWebCallback(void *context);
  static void hardResetFromWebCallback(void *context);
  void showNotice(const String &message, uint32_t ttlMs = 2500);
  int displayedVolume() const;

  static constexpr size_t MenuStackCapacity = 5;
  static constexpr size_t DimTimeoutOptionCount = 4;
  static constexpr size_t BrightnessOptionCount = 4;
  static constexpr size_t SleepTimeoutOptionCount = 4;
  static constexpr size_t HardResetOptionCount = 2;
  static constexpr size_t SettingsItemCount = 7;

  SpotifyState playback_;
  BatteryState battery_;
  QueueState queue_;
  DeviceListState deviceList_;
  StatusNotice notice_;
  DisplayManager display_;
  BatteryMonitor batteryMonitor_{battery_};
  AlbumArtManager albumArt_;
  InputController input_;
  LedRing ledRing_;
  SoundManager sound_;
  SoftResetMonitor softResetMonitor_;
  SpotifyClient spotify_{playback_};
  WebPortal webPortal_;
  SpotifyDJDevice haDevice_;
  SpotifyDJDiscovery haDiscovery_;
  SpotifyDJPairing haPairing_;
  SpotifyDJApiServer haApiServer_;
  SpotifyDJOTA haOta_;
  MqttPublisher mqttPublisher_;
  RuntimeDiagnostics diagnostics_;
  VisualState visualState_;

  UiScreen activeScreen_ = UiScreen::NowPlaying;
  UiScreen menuStack_[MenuStackCapacity] = {};
  size_t menuStackSize_ = 0;
  size_t rootMenuSelection_ = 0;
  size_t settingsSelection_ = 0;
  size_t dimTimeoutSelection_ = 1;
  size_t brightnessSelection_ = 3;
  size_t sleepTimeoutSelection_ = 0;
  size_t hardResetSelection_ = 0;
  size_t soundOutputSelection_ = 0;
  uint32_t screenOffTimeoutMs_ = Config::DisplayOffAfterMs;
  uint32_t deviceSleepTimeoutMs_ = Config::DeviceSleepAfterMs;
  uint8_t screenBrightnessPercent_ = 100;
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
  bool topHoldRestartHintVisible_ = false;
  bool haPairingScreenActive_ = false;
  uint32_t pendingWifiSettingsRequestedAt_ = 0;
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
  uint32_t haPairingStartedAt_ = 0;
  uint32_t lastHaPairingScreenAt_ = 0;
  uint32_t loopMetricsWindowStartedAt_ = 0;
  uint32_t loopMetricsBusyMs_ = 0;
};

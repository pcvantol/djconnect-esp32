// Top-level application orchestration.
// This file wires together input, Spotify, display, battery, LED ring, and periodic refresh timing.
#include "DJConnectApp.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <climits>
#include <cstring>
#include <time.h>

#include "AppLog.h"
#include "Config.h"
#include "GitHubTls.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "ScopedWatchdogPause.h"
#include "assets/djconnect_favicon_ico.h"
#include "assets/djconnect_icon_192_png.h"

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

namespace {
static const char *const TopHoldMenuHint = "hold to open menu";

String dimTimeoutLabel(uint32_t valueMs) {
  if (valueMs == 30000UL) {
    return I18n::language() == Language::Dutch ? "30 seconden" : "30 seconds";
  }
  if (valueMs == 60000UL) {
    return I18n::language() == Language::Dutch ? "1 minuut" : "1 minute";
  }
  return String(valueMs / 60000UL) + (I18n::language() == Language::Dutch ? " minuten" : " minutes");
}

String minuteLabel(uint32_t valueMs) {
  const uint32_t minutes = valueMs / 60000UL;
  if (I18n::language() == Language::Dutch) {
    return String(minutes) + (minutes == 1 ? " minuut" : " minuten");
  }
  return String(minutes) + (minutes == 1 ? " minute" : " minutes");
}

}  // namespace

using namespace DJConnectMenu;

void DJConnectApp::begin() {
  pinMode(Config::DisplayBacklightPin, OUTPUT);
  digitalWrite(Config::DisplayBacklightPin, LOW);

  Serial.begin(115200);
  AppLog.begin();
  AppLog.println(Config::BuildMarker);
  if (psramFound()) {
    AppLog.print("PSRAM: enabled size=");
    AppLog.print(ESP.getPsramSize());
    AppLog.print(" free=");
    AppLog.println(ESP.getFreePsram());
  } else {
    AppLog.println("PSRAM: unavailable");
  }
  configureWatchdog();
  responsiveDelay(500);

  batteryMonitor_.begin();
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  if (shouldReturnToSleepAfterTimerWake()) {
    AppLog.println("Wake probe: no charger detected, returning to deep sleep");
    enterDeepSleepWithoutDisplay();
    return;
  }

  // Hardware/services are initialized before WiFi so boot messages and recovery controls are available early.
  display_.begin();
  input_.begin();
  haDevice_.begin(&battery_, &display_);
  haPairing_.begin(haDevice_, &haDiscovery_);
  haPairing_.setLanguageProvisionedCallback(languageProvisionedCallback, this);
  softResetMonitor_.begin(battery_, softResetCueCallback, this);
  albumArt_.begin();
  ledRing_.begin();
  sound_.begin();
  djAudio_.begin(sound_, &ledRing_);
  djAudio_.setActivityCallback(voiceActivityCallback, this);
  voiceRecorder_.begin();
  wakeWord_.begin();
  wakeWord_.setCallback(wakeWordDetectedCallback, this);
  voiceClient_.begin(haDevice_);
  voiceClient_.setActivityCallback(voiceActivityCallback, this);
  homeAssistantPaired_ = false;
  loadProvisioning();
  wakeWord_.setEnabled(wakeWordEnabled_);
  sound_.playStartup();
  spotify_.setHomeAssistantDevice(haDevice_);
  spotify_.begin();

  if (shouldStartProvisioningPortal()) {
    runCaptivePortal();
    return;
  }

  if (updateLowBatteryGuard()) {
    return;
  }

  display_.showBootMessage(I18n::text("boot_booting"), battery_);
  responsiveDelay(3000);
  ledRing_.playStartupRainbow();
  display_.showBootMessage(I18n::text("boot_booting"), battery_);
  connectWiFi(Config::WifiConnectTimeoutMs, true);

  if (WiFi.status() == WL_CONNECTED) {
    if (!haDevice_.isPaired() && checkBootstrapFirmwareUpdate()) {
      return;
    }
    startWebPortalIfNeeded();
    setupHomeAssistantLayer();
    if (!haDevice_.isPaired()) {
      haPairingScreenActive_ = true;
      haPairingStartedAt_ = millis();
      lastHaPairingScreenAt_ = millis();
      haDevice_.displayPairingCode();
      lastBatteryPollAt_ = millis();
      loopMetricsWindowStartedAt_ = millis();
      return;
    }
    display_.showBootMessage(I18n::text("boot_connecting_playback"), battery_);
    sendHomeAssistantStatusIfDue(true);
    lastPlaybackPollAt_ = millis();
    playbackPollPausedUntil_ = millis() + Config::PlaybackBootGraceMs;
    playbackRefreshAfterPairing_ = true;
  }

  lastBatteryPollAt_ = millis();
  loopMetricsWindowStartedAt_ = millis();
  if (wifiConnectFailed_) {
    return;
  }
  renderNow();
}

void DJConnectApp::loop() {
  const uint32_t loopStartedAt = millis();
  serviceWatchdog();

  if (updateLowBatteryGuard()) {
    recordLoopMetrics(loopStartedAt);
    responsiveDelay(20);
    return;
  }

  if (wifiConnectFailed_) {
    handleWifiConnectFailureLoop(loopStartedAt);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    playback_.error = "WiFi disconnected";
    wifiConnectFailed_ = true;
    wifiConnectFailedAt_ = millis();
    display_.forceBacklightPercent(100);
    renderWifiConnectFailureMenu();
    ledRing_.setPowerPercent(0);
    recordLoopMetrics(loopStartedAt);
    responsiveDelay(10);
    return;
  }

  startWebPortalIfNeeded();

  if (handleHomeAssistantPairingMode(loopStartedAt)) {
    return;
  }

  // Main loop order keeps input responsive, then drains async work, then performs slower polling.
  const InputEvents events = input_.poll();
  handleInputEvents(events);
  if (djResponseOverlayVisible_ && static_cast<int32_t>(millis() - djResponseOverlayUntil_) >= 0) {
    djResponseOverlayVisible_ = false;
    display_.resetDjResponseOverlayCache();
    if (!voiceRecording_ && (voiceState_ == VoiceState::Done || voiceState_ == VoiceState::Error)) {
      voiceState_ = VoiceState::Idle;
    }
    renderNow();
  } else if (djResponseOverlayVisible_ ||
             voiceState_ == VoiceState::WaitingForResult ||
             voiceState_ == VoiceState::SendingCommand) {
    ledRing_.showDjResponseAnimation();
    visualState_.ledOn = ledRing_.isOn();
  }
  if (webVoiceTextOnlyActive_ && static_cast<int32_t>(millis() - webVoiceTextOnlyUntil_) >= 0) {
    webVoiceTextOnlyActive_ = false;
    webVoiceTextOnlyConsumeNext_ = false;
  }
  processPendingWebVoiceText();
  if (voiceRecording_) {
    if (!voiceRecorder_.update()) {
      const String error = voiceRecorder_.error();
      voiceRecorder_.abort();
      voiceRecording_ = false;
      voiceStartedByWakeWord_ = false;
      nextVoiceStartFromWakeWord_ = false;
      voiceSilenceStartedAt_ = 0;
      voiceState_ = VoiceState::Error;
      voiceClient_.sendStatus(false, "error", error);
      showNotice(error, 3000);
      renderNow();
    } else if (Logic::shouldAutoStopVoiceRecording(voiceRecorder_.elapsedMs(), Config::VoiceMaxRecordMs)) {
      AppLog.println("Voice: maximum recording duration reached, stopping");
      stopVoiceRecordingAndSendText();
    } else if (voiceStopPending_ && voiceRecorder_.elapsedMs() >= Config::VoiceMinRecordMs) {
      AppLog.println("Voice: minimum recording duration reached, stopping");
      stopVoiceRecordingAndSendText();
    } else if (voiceStartedByWakeWord_ && voiceRecorder_.elapsedMs() >= Config::VoiceMinRecordMs) {
      const uint16_t rms = voiceRecorder_.currentRms();
      if (rms <= Config::VoiceSilenceRmsThreshold) {
        if (voiceSilenceStartedAt_ == 0) {
          voiceSilenceStartedAt_ = millis();
        } else if (millis() - voiceSilenceStartedAt_ >= Config::VoiceSilenceStopMs) {
          AppLog.print("Voice: silence detected after wake word, stopping rms=");
          AppLog.println(rms);
          stopVoiceRecordingAndSendText();
        }
      } else {
        voiceSilenceStartedAt_ = 0;
      }
    }
  }
  if (!voiceRecording_ &&
      wakeWordEnabled_ &&
      voiceState_ == VoiceState::Idle &&
      !djResponseOverlayVisible_ &&
      activeScreen_ != UiScreen::AlbumArt &&
      homeAssistantPaired_) {
    wakeWord_.loop(voiceRecorder_);
  } else if (wakeWord_.available() && millis() - lastWakeWordGateLogAt_ > 5000) {
    String reason;
    if (!wakeWordEnabled_) {
      reason = "disabled";
    } else if (voiceRecording_) {
      reason = "recording active";
    } else if (voiceState_ != VoiceState::Idle) {
      reason = String("voice state ") + String(static_cast<int>(voiceState_));
    } else if (djResponseOverlayVisible_) {
      reason = "DJ announcement active";
    } else if (activeScreen_ == UiScreen::AlbumArt) {
      reason = "album art active";
    } else if (!homeAssistantPaired_) {
      reason = "HA not paired";
    } else {
      reason = "voice unavailable";
    }
    AppLog.line(String("Wake word: waiting, ") + reason);
    lastWakeWordGateLogAt_ = millis();
  }
  flushPendingVolume();
  processVolumeResult();
  processStressTest();
  webPortal_.handle();
  processPendingWifiSettings();
  haApiServer_.loop();
  albumArt_.cleanupIfDue();

  if (notice_.clearIfExpired()) {
    renderNow();
  }

  if (!isMenuActive()) {
    const bool titleScrollAdvanced = display_.advanceTitleScrollIfNeeded(playback_);
    const bool artistScrollAdvanced = display_.advanceArtistScrollIfNeeded(playback_);
    if (titleScrollAdvanced || artistScrollAdvanced) {
      renderNow();
    }
  } else if (activeScreen_ == UiScreen::AlbumArt) {
    const bool titleScrollAdvanced = display_.advanceTitleScrollIfNeeded(playback_, 140, 4);
    const bool artistScrollAdvanced = display_.advanceArtistScrollIfNeeded(playback_, 140, 4);
    if (titleScrollAdvanced || artistScrollAdvanced) {
      display_.renderAlbumArtMarqueeText(playback_, titleScrollAdvanced, artistScrollAdvanced);
    }
  } else if (activeScreen_ == UiScreen::Logs && millis() - lastLogsRenderAt_ >= 500) {
    renderNow();
  } else if (activeScreen_ == UiScreen::Pong) {
    updatePong();
  } else if (activeScreen_ == UiScreen::Asteroids) {
    updateAsteroids();
  } else if (activeScreen_ == UiScreen::Flyer) {
    updateFlyer();
  } else if (activeScreen_ == UiScreen::MazeChase) {
    updateMazeChase();
  }

  updateVisualPower();
  if (!deepSleepStarted_ && display_.idleMs() >= deviceSleepTimeoutMs_) {
    enterDeepSleep("idle timeout");
  }
  pollBatteryIfDue();
  pollPlaybackIfDue();
  sendHomeAssistantStatusIfDue();

  recordLoopMetrics(loopStartedAt);
  logHeapIfDue();
  responsiveDelay(5);
}

void DJConnectApp::loadProvisioning() {
  const ProvisioningSettings settings = provisioning_.load();
  wifiSsid_ = settings.wifiSsid;
  wifiPassword_ = settings.wifiPassword;
  screenOffTimeoutMs_ = settings.screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = settings.deviceSleepTimeoutMs;
  screenBrightnessPercent_ = settings.screenBrightnessPercent;
  language_ = settings.language;
  I18n::setLanguage(language_);
  languageCode_ = I18n::languageCode();
  themeCode_ = settings.themeCode;
  logLevel_ = settings.logLevel;
  AppLog.setLevel(logLevel_);
  wakeWordEnabled_ = settings.wakeWordEnabled;
  speakerVolumePercent_ = settings.speakerVolumePercent;
  volumeFeedbackEnabled_ = settings.volumeFeedbackEnabled;
  pongHighScore_ = static_cast<int>(min(settings.pongHighScore, static_cast<uint32_t>(INT_MAX)));
  asteroidHighScore_ = static_cast<int>(min(settings.asteroidsHighScore, static_cast<uint32_t>(INT_MAX)));
  flyerHighScore_ = static_cast<int>(min(settings.flyerHighScore, static_cast<uint32_t>(INT_MAX)));
  mazeHighScore_ = static_cast<int>(min(settings.mazeHighScore, static_cast<uint32_t>(INT_MAX)));
  setupModeRequested_ = settings.setupModeRequested;
  helpShown_ = settings.helpShown;

  for (size_t index = 0; index < DimTimeoutOptionCount; index++) {
    if (dimTimeoutValueMs(index) == screenOffTimeoutMs_) {
      dimTimeoutSelection_ = index;
      break;
    }
  }
  for (size_t index = 0; index < BrightnessOptionCount; index++) {
    if (brightnessValuePercent(index) == screenBrightnessPercent_) {
      brightnessSelection_ = index;
      break;
    }
  }
  languageSelection_ = language_ == Language::Dutch ? 1 : 0;
  for (size_t index = 0; index < ThemeOptionCount; index++) {
    if (themeValue(index) == themeCode_) {
      themeSelection_ = index;
      break;
    }
  }
  for (size_t index = 0; index < LogLevelOptionCount; index++) {
    if (logLevelValue(index) == logLevel_) {
      logLevelSelection_ = index;
      break;
    }
  }
  for (size_t index = 0; index < SpeakerVolumeOptionCount; index++) {
    if (speakerVolumeValuePercent(index) == speakerVolumePercent_) {
      speakerVolumeSelection_ = index;
      break;
    }
  }
  sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  applyTheme();
  sound_.setVolumePercent(speakerVolumePercent_);

  AppLog.print("WiFi credentials source: ");
  AppLog.println(wifiSsid_.isEmpty() ? "missing" : "NVS");
}

bool DJConnectApp::shouldStartProvisioningPortal() const {
  return setupModeRequested_ || wifiSsid_.isEmpty();
}

void DJConnectApp::handleWifiConnectFailureLoop(uint32_t loopStartedAt) {
  display_.forceBacklightPercent(100);
  ledRing_.setPowerPercent(0);

  const InputEvents events = input_.poll();
  if (events.encoderSteps != 0) {
    int nextSelection = static_cast<int>(wifiFailureSelection_) + events.encoderSteps;
    const size_t itemCount = wifiFailureConfirmHardReset_ ? ConfirmOptionCount : WifiFailureOptionCount;
    while (nextSelection < 0) {
      nextSelection += itemCount;
    }
    wifiFailureSelection_ = static_cast<size_t>(nextSelection) % itemCount;
    if (volumeFeedbackEnabled_) {
      sound_.playMenuTick(events.encoderSteps);
    }
    renderWifiConnectFailureMenu();
  }

  if (events.encoderClick) {
    input_.clearPendingButtonActions();
    applyWifiConnectFailureSelection();
  }

  if (!deepSleepStarted_ && wifiConnectFailedAt_ != 0 && millis() - wifiConnectFailedAt_ >= Config::WifiFailureSleepAfterMs) {
    enterDeepSleep();
  }

  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
  recordLoopMetrics(loopStartedAt);
  responsiveDelay(10);
}

void DJConnectApp::renderWifiConnectFailureMenu() {
  StatusNotice notice;
  notice.show(I18n::text("center_select"), 1000);
  if (wifiFailureConfirmHardReset_) {
    MenuItemView items[ConfirmOptionCount] = {
        {I18n::text("confirm_no_go_back")},
        {I18n::text("confirm_yes_wipe_setup")},
    };
    display_.renderMenuList(I18n::text("factory_reset_title"), items, ConfirmOptionCount, wifiFailureSelection_, notice);
    return;
  }

  MenuItemView items[WifiFailureOptionCount] = {
      {I18n::text("retry_connect")},
      {I18n::text("restart_device")},
      {I18n::text("turn_off_device")},
      {I18n::text("factory_reset")},
  };
  display_.renderMenuList(I18n::text("wifi_failed"), items, WifiFailureOptionCount, wifiFailureSelection_, notice);
}

void DJConnectApp::applyWifiConnectFailureSelection() {
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }

  if (wifiFailureConfirmHardReset_) {
    if (wifiFailureSelection_ == 0) {
      wifiFailureConfirmHardReset_ = false;
      wifiFailureSelection_ = WifiFailureOptionCount - 1;
      renderWifiConnectFailureMenu();
    } else {
      AppLog.println("WiFi recovery: factory reset confirmed");
      hardResetToProvisioning();
    }
    return;
  }

  if (wifiFailureSelection_ == 0) {
    AppLog.println("WiFi recovery: retry connect selected");
    wifiConnectFailed_ = false;
    wifiConnectFailedAt_ = 0;
    wifiFailureConfirmHardReset_ = false;
    showNotice("Retry WiFi", 1500);
    if (connectWiFi(Config::WifiConnectTimeoutMs, true) && WiFi.status() == WL_CONNECTED) {
      startWebPortalIfNeeded();
      display_.showBootMessage(I18n::text("boot_authorizing_spotify"), battery_);
      lastPlaybackPollAt_ = millis();
      renderNow();
    } else {
      renderWifiConnectFailureMenu();
    }
    return;
  }

  if (wifiFailureSelection_ == 1) {
    AppLog.println("WiFi recovery: restart selected");
    display_.showBootMessage(I18n::text("restarting"), battery_);
    responsiveDelay(300);
    ESP.restart();
    return;
  }

  if (wifiFailureSelection_ == 2) {
    AppLog.println("WiFi recovery: turn off selected");
    display_.showBootMessage(I18n::text("turning_off"), battery_);
    responsiveDelay(300);
    enterDeepSleep();
    return;
  }

  AppLog.println("WiFi recovery: factory reset selected");
  wifiFailureConfirmHardReset_ = true;
  wifiFailureSelection_ = 0;
  renderWifiConnectFailureMenu();
}

bool DJConnectApp::connectWiFi(uint32_t timeoutMs, bool bootScreen) {
  if (bootScreen) {
    display_.forceBacklightPercent(100);
  } else {
    display_.wakeForUserActivity();
  }
  display_.showBootMessage(I18n::text("boot_connecting_wifi"), battery_);
  if (wifiSsid_.isEmpty()) {
    playback_.error = "WiFi credentials missing";
    showNotice(playback_.error, 5000);
    wifiConnectFailed_ = true;
    wifiConnectFailedAt_ = millis();
    if (bootScreen) {
      display_.forceBacklightPercent(100);
      renderWifiConnectFailureMenu();
    }
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  responsiveDelay(100);
  WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str());

  const uint32_t startedAt = millis();
  uint32_t lastWifiDotAt = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    ledRing_.showWifiConnectingAnimation();
    responsiveDelay(50);
    const uint32_t now = millis();
    if (now - lastWifiDotAt >= 500) {
      lastWifiDotAt = now;
      AppLog.print(".");
    }
  }
  AppLog.println();

  if (WiFi.status() == WL_CONNECTED) {
    playback_.error = "";
    showNotice("WiFi connected");
    AppLog.print("IP: ");
    AppLog.println(WiFi.localIP());
    syncClock();
    wifiConnectFailed_ = false;
    wifiConnectFailedAt_ = 0;
    return true;
  } else {
    playback_.error = I18n::text("spotify_not_connected");
    showNotice(playback_.error, 5000);
    wifiConnectFailed_ = true;
    wifiConnectFailedAt_ = millis();
    WiFi.disconnect(false, false);
    if (bootScreen) {
      display_.forceBacklightPercent(100);
      renderWifiConnectFailureMenu();
    }
    return false;
  }
}

void DJConnectApp::startWebPortalIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || webPortal_.isRunning()) {
    return;
  }

  webPortal_.begin(
      playback_,
      battery_,
      diagnostics_,
      visualState_,
      spotify_,
      ledRing_,
      display_,
      sound_,
      screenBrightnessPercent_,
      speakerVolumePercent_,
      homeAssistantPaired_,
      languageCode_,
      themeCode_,
      logLevel_,
      wakeWordEnabled_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_,
      this,
      applyWebSettingsCallback,
      applyWebWifiSettingsCallback,
      sendWebVoiceTextCallback,
      refreshFromWebCallback,
      resetPairingFromWebCallback,
      hardResetFromWebCallback);

  setupHomeAssistantLayer();
}

bool DJConnectApp::syncClock() {
#if SPOTIFY_ALLOW_INSECURE_TLS
  return true;
#else
  // TLS certificate validation needs a sane wall clock on ESP32.
  display_.showBootMessage(I18n::text("boot_syncing_clock"), battery_);
  configTzTime(Config::AmsterdamTimezone, "pool.ntp.org", "time.google.com", "time.nist.gov");

  const uint32_t startedAt = millis();
  time_t now = time(nullptr);
  while (now < 1704067200 && millis() - startedAt < 12000) {
    responsiveDelay(250);
    AppLog.print(".");
    now = time(nullptr);
  }
  AppLog.println();

  if (now < 1704067200) {
    playback_.error = "Clock sync failed";
    showNotice(playback_.error, 5000);
    return false;
  }

  playback_.error = "";
  showNotice("Clock synced");
  struct tm local = {};
  char buffer[32] = {};
  if (localtime_r(&now, &local) != nullptr) {
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &local);
    AppLog.print("Clock synced Amsterdam: ");
    AppLog.println(buffer);
  }
  return true;
#endif
}

void DJConnectApp::runCaptivePortal() {
  display_.wakeForUserActivity();
  display_.forceBacklightPercent(100);
  auto setupScreenMessage = []() {
    const String connectLine = I18n::language() == Language::Dutch
                                   ? String("Verbind a.u.b. met \"") + Config::ProvisioningApSsid + "\""
                                   : String("Please connect to \"") + Config::ProvisioningApSsid + "\"";
    return connectLine +
           "\n" + I18n::text("setup_portal_active_10m") +
           "\n" + I18n::text("setup_turn_off_hint");
  };
  display_.showBootMessage(setupScreenMessage(), battery_);
  ledRing_.showSetupRainbowBreath();
  bleProvisioning_.begin(haDevice_.getDeviceId());

  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::ProvisioningApSsid);
  const IPAddress apIp = WiFi.softAPIP();

  DNSServer dnsServer;
  dnsServer.start(53, "*", apIp);

  WebServer server(80);
  String portalMessage = "Please fill in your WiFi credentials.";
  bool portalError = false;
  String formSsid;

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid));
  });

  server.on("/favicon.ico", HTTP_GET, [&]() {
    server.send_P(200, "image/x-icon", reinterpret_cast<const char *>(DJCONNECT_FAVICON_ICO), DJCONNECT_FAVICON_ICO_LEN);
  });

  server.on("/icon-192.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(DJCONNECT_ICON_192_PNG), DJCONNECT_ICON_192_PNG_LEN);
  });
  server.on("/apple-touch-icon.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(DJCONNECT_ICON_192_PNG), DJCONNECT_ICON_192_PNG_LEN);
  });
  server.on("/apple-touch-icon-precomposed.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(DJCONNECT_ICON_192_PNG), DJCONNECT_ICON_192_PNG_LEN);
  });

  server.on("/submit", HTTP_POST, [&]() {
    String ssid = server.arg("ssid");
    const String password = server.arg("password");
    ssid.trim();
    formSsid = ssid;
    if (ssid.isEmpty()) {
      portalMessage = "SSID is required.";
      portalError = true;
      server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid));
      return;
    }

    String message;
    if (testAndSaveProvisioning(ssid, password, message)) {
      server.send(200, "text/html", captivePortalPage(message, false, formSsid));
      responsiveDelay(1500);
      ESP.restart();
      return;
    }

    portalMessage = message;
    portalError = true;
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid));
  });

  server.onNotFound([&]() {
    server.sendHeader("Location", String("http://") + apIp.toString(), true);
    server.send(302, "text/plain", "");
  });

  server.begin();

  AppLog.print("Setup portal AP: ");
  AppLog.print(Config::ProvisioningApSsid);
  AppLog.print(" / http://");
  AppLog.println(apIp);

  const uint32_t setupStartedAt = millis();
  uint32_t lastSetupPromptAt = 0;
  uint32_t lastBatteryRefreshAt = 0;
  for (;;) {
    const InputEvents events = input_.poll();
    if (events.topButtonPress || events.topButtonHeld || events.topButtonLongClick) {
      input_.clearPendingButtonActions();
    }
    if (events.encoderClick) {
      AppLog.println("Setup portal: turn off selected");
      display_.showBootMessage(I18n::text("turning_off"), battery_);
      responsiveDelay(300);
      enterDeepSleep();
    }

    dnsServer.processNextRequest();
    server.handleClient();
    String blePayload;
    if (bleProvisioning_.pollPayload(blePayload)) {
      String message;
      bleProvisioning_.setStatus("testing", "Testing WiFi credentials");
      if (handleBleProvisioningPayload(blePayload, message)) {
        bleProvisioning_.setStatus("success", "WiFi saved, restarting");
        bleProvisioning_.end();
        display_.showBootMessage(I18n::text("boot_ble_setup_ok"), battery_);
        responsiveDelay(1200);
        ESP.restart();
        return;
      }
      portalMessage = message;
      portalError = true;
      bleProvisioning_.setStatus("error", message);
      display_.showBootMessage(I18n::text("boot_ble_setup_failed"), battery_);
    }
    display_.forceBacklightPercent(100);
    ledRing_.showSetupRainbowBreath();
    const uint32_t now = millis();

    if (lastBatteryRefreshAt == 0 || now - lastBatteryRefreshAt >= Config::BatteryPollIntervalMs) {
      lastBatteryRefreshAt = now;
      batteryMonitor_.refresh();
      evaluateBatteryTransition();
      display_.showBootMessage(setupScreenMessage(), battery_);
    }

    if (now - setupStartedAt <= Config::SetupPromptBeepDurationMs &&
        (lastSetupPromptAt == 0 || now - lastSetupPromptAt >= Config::SetupPromptBeepIntervalMs)) {
      lastSetupPromptAt = now;
      sound_.playSetupPrompt();
    }

    if (!deepSleepStarted_ && now - setupStartedAt >= Config::ProvisioningPortalTimeoutMs) {
      display_.showBootMessage(I18n::text("boot_setup_timeout_sleeping"), battery_);
      responsiveDelay(600);
      enterDeepSleep();
    }
    responsiveDelay(5);
  }
}

bool DJConnectApp::handleBleProvisioningPayload(const String &payload, String &message) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, payload);
  if (jsonError) {
    message = "BLE setup JSON failed.";
    AppLog.print("BLE provisioning JSON failed, bytes=");
    AppLog.print(payload.length());
    AppLog.print(", error=");
    AppLog.println(jsonError.c_str());
    return false;
  }

  String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  ssid.trim();

  if (ssid.isEmpty()) {
    message = "BLE setup requires ssid.";
    return false;
  }

  AppLog.print("BLE provisioning WiFi SSID provided, chars=");
  AppLog.println(ssid.length());
  return testAndSaveProvisioning(ssid, password, message);
}

String DJConnectApp::captivePortalPage(
    const String &message,
    bool error,
    const String &ssid) const {
  auto escaped = [](String value) {
    value.replace("&", "&amp;");
    value.replace("\"", "&quot;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    return value;
  };
  String page;
  const String escapedModel = escaped(Config::DeviceModel);
  page.reserve(4600);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<meta name='theme-color' content='#080b0c'>");
  page += F("<meta name='mobile-web-app-capable' content='yes'>");
  page += F("<meta name='apple-mobile-web-app-capable' content='yes'>");
  page += F("<meta name='apple-mobile-web-app-title' content='DJConnect Setup ");
  page += escapedModel;
  page += F("'>");
  page += F("<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>");
  page += F("<meta name='application-name' content='DJConnect Setup ");
  page += escapedModel;
  page += F("'>");
  page += F("<link rel='shortcut icon' href='/favicon.ico?v=3' sizes='any'>");
  page += F("<link rel='icon' href='/favicon.ico?v=3' sizes='any'>");
  page += F("<link rel='icon' type='image/png' sizes='192x192' href='/icon-192.png?v=3'>");
  page += F("<link rel='apple-touch-icon' sizes='180x180' href='/apple-touch-icon.png?v=3'>");
  page += F("<link rel='apple-touch-icon' sizes='192x192' href='/apple-touch-icon.png?v=3'>");
  page += F("<link rel='apple-touch-icon-precomposed' sizes='180x180' href='/apple-touch-icon-precomposed.png?v=3'>");
  page += F("<link rel='apple-touch-icon-precomposed' sizes='192x192' href='/apple-touch-icon-precomposed.png?v=3'>");
  page += F("<title>DJConnect Setup ");
  page += escapedModel;
  page += F("</title><style>");
  page += F("*{box-sizing:border-box}body{margin:0;background:#080b0c;color:#f3f7f5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}");
  page += F("main{width:100%;max-width:520px;margin:0 auto;padding:18px}.brand{display:flex;align-items:center;gap:10px;margin:8px 0 4px}.brand img{width:42px;height:42px;border-radius:8px}h1{font-size:26px;margin:0}.model{display:block;color:#9aa6a2;font-size:13px;font-weight:700;margin-top:2px}p{color:#9aa6a2;line-height:1.4}");
  page += F(".box{background:#111718;border:1px solid #233033;border-radius:8px;padding:14px;margin-top:14px}");
  page += F(".timeout{border:1px solid #4c3d17;background:#1a1608;color:#ffe28a;border-radius:8px;padding:10px 12px;margin:12px 0}");
  page += F(".msg{border-radius:8px;padding:10px 12px;margin:12px 0;background:");
  page += error ? F("#3a1714;color:#ffd1c9") : F("#173721;color:#baf7ca");
  page += F("}label{display:grid;gap:6px;margin:12px 0;color:#a8b3af;font-size:13px}");
  page += F("input,button{display:block;width:100%;max-width:100%;min-height:44px;border-radius:8px;border:1px solid #233033;background:#0c1112;color:#f3f7f5;padding:9px 10px;font-size:16px}");
  page += F("button{background:#173721;border-color:#25593a;color:#baf7ca;font-weight:700;margin-top:8px}");
  page += F("</style></head><body><main><div class='brand'><img src='/icon-192.png?v=3' alt=''><h1>DJConnect Setup<span class='model'>");
  page += escapedModel;
  page += F("</span></h1></div>");
  page += F("<p>");
  page += language_ == Language::Dutch ? F("Vul je WiFi gegevens in.") : F("Please fill in your WiFi credentials.");
  page += F("</p>");
  page += F("<div class='timeout'>");
  page += language_ == Language::Dutch
              ? F("Deze setup portal blijft 10 minuten wakker. Daarna schakelt het device automatisch uit.")
              : F("This setup portal stays awake for 10 minutes. After that the device turns off automatically.");
  page += F("</div>");
  page += F("<div class='msg'>");
  page += message;
  page += F("</div><form class='box' method='post' action='/submit'>");
  page += F("<label>WiFi SSID<input name='ssid' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' required value=\"");
  page += escaped(ssid);
  page += F("\"></label>");
  page += language_ == Language::Dutch
              ? F("<label>WiFi wachtwoord<input name='password' type='password' autocomplete='current-password' autocapitalize='none' autocorrect='off' spellcheck='false'></label>")
              : F("<label>WiFi password<input name='password' type='password' autocomplete='current-password' autocapitalize='none' autocorrect='off' spellcheck='false'></label>");
  page += language_ == Language::Dutch
              ? F("<button type='submit'>Opslaan &amp; verbinden</button></form>")
              : F("<button type='submit'>Save &amp; Connect</button></form>");
  page += F("</main></body></html>");
  return page;
}

bool DJConnectApp::testAndSaveProvisioning(
    const String &ssid,
    const String &password,
    String &message) {
  display_.showBootMessage(I18n::text("boot_testing_wifi"), battery_);
  ledRing_.showWifiTestingAnimation();

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t startedAt = millis();
  uint32_t lastProgressDotAt = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    const uint32_t now = millis();
    ledRing_.showWifiTestingAnimation();
    responsiveDelay(50);
    if (lastProgressDotAt == 0 || now - lastProgressDotAt >= 250) {
      lastProgressDotAt = now;
      AppLog.print(".");
    }
  }
  AppLog.println();

  if (WiFi.status() != WL_CONNECTED) {
    message = "WiFi test failed. Check SSID/password.";
    WiFi.disconnect(false);
    return false;
  }

  AppLog.print("Provisioning WiFi OK, IP: ");
  AppLog.println(WiFi.localIP());

  if (!syncClock()) {
    message = playback_.error.isEmpty() ? "Clock sync failed." : playback_.error;
    WiFi.disconnect(false);
    return false;
  }

  provisioning_.saveSetupProvisioning(ssid, password);

  wifiSsid_ = ssid;
  wifiPassword_ = password;
  setupModeRequested_ = false;
  message = I18n::text("setup_success_restart");
  display_.showBootMessage(I18n::text("boot_setup_ok"), battery_);
  return true;
}

void DJConnectApp::handleInputEvents(const InputEvents &events) {
  if (suppressInputUntilRelease_) {
    input_.clearPendingButtonActions();
    if (!events.buttonHeld) {
      suppressInputUntilRelease_ = false;
    }
    return;
  }

  if (events.touched) {
    if (display_.backlightPercent() == 0) {
      wakeDisplayWithSplash();
      input_.clearPendingButtonActions();
      suppressInputUntilRelease_ = events.buttonHeld;
      return;
    }
    display_.wakeForUserActivity();
  }

  const bool voiceBusy =
      voiceState_ == VoiceState::WaitingForResult ||
      voiceState_ == VoiceState::SendingCommand ||
      djResponseOverlayVisible_;
  if ((events.encoderPress || events.encoderClick) && voiceBusy) {
    input_.clearPendingButtonActions();
    cancelVoiceFlow("encoder");
    return;
  }

  if (events.encoderClick && dismissDjResponseOverlay()) {
    input_.clearPendingButtonActions();
    return;
  }

  if (!isMenuActive() && events.topButtonHeld && !topHoldMenuHintVisible_) {
    topHoldMenuHintVisible_ = true;
    showNotice(TopHoldMenuHint, 900);
  } else if (!events.topButtonHeld) {
    topHoldMenuHintVisible_ = false;
  }

  if (events.encoderPress && volumeFeedbackEnabled_ && activeScreen_ != UiScreen::AlbumArt) {
    sound_.playButtonPress();
  }

  if (homeAssistantPaired_ && events.encoderRelease && voiceRecording_) {
    input_.clearPendingButtonActions();
    if (voiceRecorder_.elapsedMs() < Config::VoiceMinRecordMs) {
      voiceStopPending_ = true;
      AppLog.print("Voice: release before minimum duration, delaying stop ms=");
      AppLog.println(voiceRecorder_.elapsedMs());
      return;
    }
    stopVoiceRecordingAndSendText();
    return;
  }

  if (menuTopHoldActive_) {
    if (!events.topButtonHeld) {
      menuTopHoldActive_ = false;
    } else {
      if (millis() - menuTopHoldStartedAt_ >= 1500 && activeScreen_ != UiScreen::NowPlaying) {
        if (volumeFeedbackEnabled_) {
          sound_.playConfirm();
        }
        activeScreen_ = UiScreen::NowPlaying;
        menuStackSize_ = 0;
        renderNow();
      }
      return;
    }
  }

  if (isMenuActive()) {
    handleMenuInputEvents(events);
    return;
  }

  handlePlaybackInputEvents(events);
}

void DJConnectApp::handlePlaybackInputEvents(const InputEvents &events) {
  if (events.encoderSteps != 0) {
    handleEncoderTurn(events.encoderSteps);
  }

  if (events.encoderClick) {
    if (playback_.hasPlayback) {
      pauseOrResume();
    } else {
      startLikedProxyPlaylist();
    }
  }

  if (events.topButtonClick) {
    if (volumeFeedbackEnabled_) {
      sound_.playMenuTick(1);
    }
    goToNextTrack();
  }

  if (events.topButtonDoubleClick) {
    if (volumeFeedbackEnabled_) {
      sound_.playMenuTick(-1);
    }
    goToPreviousTrack();
  }

  if (events.encoderLongClick) {
    AppLog.println("Voice: encoder long press");
    handleVoiceButton();
  }

  if (events.topButtonLongClick) {
    openRootMenu();
  }

  // Holding the top button for the longer reset threshold is still handled by SoftResetMonitor.
}

void DJConnectApp::handleMenuInputEvents(const InputEvents &events) {
  if (activeScreen_ == UiScreen::Pong && events.encoderSteps != 0) {
    pongPaddleY_ = constrain(pongPaddleY_ + (events.encoderSteps * 5), 42, 126);
    renderNow();
    return;
  }
  if (activeScreen_ == UiScreen::Asteroids && events.encoderSteps != 0) {
    asteroidShipX_ = constrain(asteroidShipX_ + (events.encoderSteps * 7), 24, 296);
    renderNow();
    return;
  }
  if (activeScreen_ == UiScreen::Flyer && events.encoderSteps != 0) {
    flyerPlaneY_ = constrain(flyerPlaneY_ + (events.encoderSteps * 6), 52, 138);
    renderNow();
    return;
  }
  if (activeScreen_ == UiScreen::MazeChase && events.encoderSteps != 0) {
    mazePlayerX_ = constrain(mazePlayerX_ + (events.encoderSteps * 10), 30, 290);
    renderNow();
    return;
  }

  if (activeScreen_ == UiScreen::Logs && events.encoderSteps != 0) {
    const size_t available = AppLog.availableLines();
    const size_t maxScrollBack = available > 9 ? available - 9 : 0;
    const int next = static_cast<int>(logsScrollBack_) - events.encoderSteps;
    logsScrollBack_ = static_cast<size_t>(constrain(next, 0, static_cast<int>(maxScrollBack)));
    if (volumeFeedbackEnabled_) {
      sound_.playMenuTick(events.encoderSteps);
    }
    renderNow();
    return;
  }

  if (activeScreen_ == UiScreen::Pong && events.encoderLongClick) {
    resetPong();
    if (volumeFeedbackEnabled_) {
      sound_.playConfirm();
    }
    renderNow();
    return;
  }
  if (activeScreen_ == UiScreen::Asteroids) {
    if (events.encoderPress) {
      fireAsteroids();
      return;
    }
    if (events.encoderClick) {
      return;
    }
    if (events.encoderLongClick) {
      resetAsteroids();
      renderNow();
      return;
    }
  }
  if (activeScreen_ == UiScreen::Flyer) {
    if (events.encoderPress) {
      fireFlyer();
      return;
    }
    if (events.encoderClick) {
      return;
    }
    if (events.encoderLongClick) {
      resetFlyer();
      renderNow();
      return;
    }
  }
  if (activeScreen_ == UiScreen::MazeChase) {
    if (events.encoderPress) {
      switchMazeLane();
      return;
    }
    if (events.encoderClick) {
      return;
    }
    if (events.encoderLongClick) {
      resetMazeChase();
      renderNow();
      return;
    }
  }

  if (events.encoderSteps != 0) {
    moveMenuSelection(events.encoderSteps);
  }

  if (events.topButtonLongClick) {
    if (volumeFeedbackEnabled_) {
      sound_.playBack();
    }
    activeScreen_ = UiScreen::NowPlaying;
    menuStackSize_ = 0;
    renderNow();
    return;
  }

  if (events.topButtonPress) {
    input_.consumeTopButtonPress();
    menuTopHoldActive_ = true;
    menuTopHoldStartedAt_ = millis();
    goBackOneScreen();
    return;
  }

  if (events.encoderClick) {
    selectCurrentMenuItem();
  }
  // Encoder long press is intentionally unused in menu screens.
  // Holding the top button for 10 seconds keeps its hardware reset role.
}

void DJConnectApp::openRootMenu() {
  activeScreen_ = UiScreen::RootMenu;
  menuStackSize_ = 0;
  ledRing_.clear();
  visualState_.ledOn = ledRing_.isOn();
  if (volumeFeedbackEnabled_) {
    sound_.playMenuOpen();
  }
  showNotice(I18n::text("menu"), 900);
  renderNow();
}

void DJConnectApp::openScreen(UiScreen screen) {
  if (menuStackSize_ < MenuStackCapacity) {
    menuStack_[menuStackSize_++] = activeScreen_;
  }
  activeScreen_ = screen;
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
}

void DJConnectApp::goBackOneScreen() {
  if (activeScreen_ == UiScreen::NowPlaying) {
    return;
  }

  if (menuStackSize_ == 0) {
    activeScreen_ = UiScreen::NowPlaying;
  } else {
    activeScreen_ = menuStack_[--menuStackSize_];
  }
  if (volumeFeedbackEnabled_) {
    sound_.playBack();
  }
  renderNow();
}

void DJConnectApp::moveMenuSelection(int encoderSteps) {
  const size_t itemCount = menuItemCount(activeScreen_);
  if (itemCount == 0) {
    return;
  }

  size_t &selection = selectedIndexRefForScreen(activeScreen_);
  const size_t previousSelection = selection;
  int nextSelection = static_cast<int>(selection) + encoderSteps;
  while (nextSelection < 0) {
    nextSelection += itemCount;
  }
  selection = static_cast<size_t>(nextSelection) % itemCount;
  if (selection != previousSelection && volumeFeedbackEnabled_) {
    sound_.playMenuTick(encoderSteps);
  }

  if (activeScreen_ == UiScreen::Brightness) {
    screenBrightnessPercent_ = brightnessValuePercent(brightnessSelection_);
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  } else if (activeScreen_ == UiScreen::Language) {
    language_ = languageSelection_ == 1 ? Language::Dutch : Language::English;
    I18n::setLanguage(language_);
  } else if (activeScreen_ == UiScreen::SpeakerVolume) {
    speakerVolumePercent_ = speakerVolumeValuePercent(speakerVolumeSelection_);
    sound_.setVolumePercent(speakerVolumePercent_);
  }

  renderNow();
}

void DJConnectApp::selectCurrentMenuItem() {
  switch (activeScreen_) {
    case UiScreen::RootMenu:
      if (rootMenuSelection_ == 0) {
        openAlbumArtScreen();
      } else if (rootMenuSelection_ == 1) {
        openScreen(UiScreen::Queue);
        showNotice(I18n::text("loading_queue"), 1200);
        spotify_.refreshQueue(queue_);
        renderNow();
      } else if (rootMenuSelection_ == 2) {
        openScreen(UiScreen::Playlists);
        playlistSelection_ = 0;
        showNotice(I18n::text("loading_playlists"), 1200);
        spotify_.refreshPlaylists(playlists_);
        renderNow();
      } else if (rootMenuSelection_ == 3) {
        openScreen(UiScreen::SoundOutputs);
        soundOutputSelection_ = 0;
        showNotice(I18n::text("loading_outputs"), 1200);
        spotify_.refreshDevices(deviceList_);
        renderNow();
      } else if (rootMenuSelection_ == 4) {
        shuffleSelection_ = playback_.shuffle ? 1 : 0;
        openScreen(UiScreen::ShuffleMode);
      } else if (rootMenuSelection_ == 5) {
        repeatSelection_ = 0;
        for (size_t index = 0; index < RepeatOptionCount; index++) {
          if (repeatValue(index) == playback_.repeatState) {
            repeatSelection_ = index;
            break;
          }
        }
        openScreen(UiScreen::RepeatMode);
      } else if (rootMenuSelection_ == 6) {
        openScreen(UiScreen::Games);
      } else if (rootMenuSelection_ == 7) {
        openScreen(UiScreen::Help);
      } else if (rootMenuSelection_ == 8) {
        openScreen(UiScreen::Settings);
      } else if (rootMenuSelection_ == 9) {
        openScreen(UiScreen::About);
      } else if (rootMenuSelection_ == 10) {
        logsScrollBack_ = 0;
        openScreen(UiScreen::Logs);
      }
      break;

    case UiScreen::Games:
      if (gamesSelection_ == 0) {
        resetPong();
        openScreen(UiScreen::Pong);
      } else if (gamesSelection_ == 1) {
        resetAsteroids();
        openScreen(UiScreen::Asteroids);
      } else if (gamesSelection_ == 2) {
        resetFlyer();
        openScreen(UiScreen::Flyer);
      } else {
        resetMazeChase();
        openScreen(UiScreen::MazeChase);
      }
      break;

    case UiScreen::Playlists:
      startSelectedPlaylist();
      break;

    case UiScreen::SoundOutputs:
      transferToSelectedOutput();
      break;

    case UiScreen::Settings:
      if (settingsSelection_ == 0) {
        openScreen(UiScreen::Brightness);
      } else if (settingsSelection_ == 1) {
        openScreen(UiScreen::DimTimeout);
      } else if (settingsSelection_ == 2) {
        openScreen(UiScreen::SleepTimeout);
      } else if (settingsSelection_ == 3) {
        languageSelection_ = language_ == Language::Dutch ? 1 : 0;
        openScreen(UiScreen::Language);
      } else if (settingsSelection_ == 4) {
        for (size_t index = 0; index < ThemeOptionCount; index++) {
          if (themeValue(index) == themeCode_) {
            themeSelection_ = index;
            break;
          }
        }
        openScreen(UiScreen::Theme);
      } else if (settingsSelection_ == 5) {
        for (size_t index = 0; index < LogLevelOptionCount; index++) {
          if (logLevelValue(index) == logLevel_) {
            logLevelSelection_ = index;
            break;
          }
        }
        openScreen(UiScreen::LogLevel);
      } else if (settingsSelection_ == 6) {
        volumeFeedbackEnabled_ = !volumeFeedbackEnabled_;
        saveDisplaySettings();
        AppLog.print("Settings: audio feedback ");
        AppLog.println(volumeFeedbackEnabled_ ? "enabled" : "disabled");
        showNotice(String(I18n::text("audio_feedback")) + " " + I18n::onOff(volumeFeedbackEnabled_), 1600);
        renderNow();
      } else if (settingsSelection_ == 7) {
        applyWakeWordEnabled(!wakeWordEnabled_, true);
      } else if (settingsSelection_ == 8) {
        openScreen(UiScreen::SpeakerVolume);
      } else if (settingsSelection_ == 9) {
        toggleStressTest();
      } else if (settingsSelection_ == 10) {
        display_.showBootMessage(I18n::text("turning_off"), battery_);
        responsiveDelay(250);
        enterDeepSleep();
      } else if (settingsSelection_ == 11) {
        hardResetSelection_ = 0;
        openScreen(UiScreen::ChangeWifiConfirm);
      } else if (settingsSelection_ == 12) {
        sound_.playHardReset();
        display_.showBootMessage(I18n::text("restarting"), battery_);
        responsiveDelay(320);
        ESP.restart();
      } else if (settingsSelection_ == 13) {
        hardResetSelection_ = 0;
        openScreen(UiScreen::ResetPairingConfirm);
      } else if (settingsSelection_ == 14) {
        hardResetSelection_ = 0;
        openScreen(UiScreen::HardResetConfirm);
      }
      break;

    case UiScreen::DimTimeout:
      applyDimTimeoutSelection();
      break;

    case UiScreen::Brightness:
      applyBrightnessSelection();
      break;

    case UiScreen::Language:
      language_ = languageSelection_ == 1 ? Language::Dutch : Language::English;
      I18n::setLanguage(language_);
      saveDisplaySettings();
      showNotice(String(I18n::text("language")) + " " + languageLabel(language_), 2000);
      renderNow();
      break;

    case UiScreen::Theme:
      themeCode_ = themeValue(themeSelection_);
      applyTheme();
      saveDisplaySettings();
      showNotice(String(I18n::text("theme")) + " " + themeLabel(themeCode_), 2000);
      renderNow();
      display_.showBootMessage(I18n::text("restarting"), battery_);
      responsiveDelay(450);
      ESP.restart();
      break;

    case UiScreen::LogLevel:
      applyLogLevelSelection();
      break;

    case UiScreen::SpeakerVolume:
      applySpeakerVolumeSelection();
      break;

    case UiScreen::ShuffleMode:
      applyShuffleSelection();
      break;

    case UiScreen::RepeatMode:
      applyRepeatSelection();
      break;

    case UiScreen::SleepTimeout:
      applySleepTimeoutSelection();
      break;

    case UiScreen::ResetPairingConfirm:
      if (hardResetSelection_ == 0) {
        goBackOneScreen();
      } else {
        resetHomeAssistantPairing();
      }
      break;

    case UiScreen::ChangeWifiConfirm:
      if (hardResetSelection_ == 0) {
        goBackOneScreen();
      } else {
        changeWifiToProvisioning();
      }
      break;

    case UiScreen::HardResetConfirm:
      if (hardResetSelection_ == 0) {
        goBackOneScreen();
      } else {
        hardResetToProvisioning();
      }
      break;

    case UiScreen::About:
    case UiScreen::Help:
    case UiScreen::NowPlaying:
    case UiScreen::Logs:
    case UiScreen::Pong:
    case UiScreen::Asteroids:
    case UiScreen::Flyer:
    case UiScreen::MazeChase:
      break;
    case UiScreen::Queue:
      startSelectedQueueItem();
      break;
  }
}

void DJConnectApp::applyDimTimeoutSelection() {
  screenOffTimeoutMs_ = dimTimeoutValueMs(dimTimeoutSelection_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  AppLog.print("Settings: screen dim timeout ");
  AppLog.println(dimTimeoutLabel(screenOffTimeoutMs_));
  showNotice(String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_), 2000);
  renderNow();
}

void DJConnectApp::applyBrightnessSelection() {
  screenBrightnessPercent_ = brightnessValuePercent(brightnessSelection_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  AppLog.print("Settings: screen brightness ");
  AppLog.print(screenBrightnessPercent_);
  AppLog.println("%");
  showNotice(String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%", 2000);
  renderNow();
}

void DJConnectApp::applySpeakerVolumeSelection() {
  speakerVolumePercent_ = speakerVolumeValuePercent(speakerVolumeSelection_);
  sound_.setVolumePercent(speakerVolumePercent_);
  saveDisplaySettings();
  AppLog.print("Settings: speaker volume ");
  AppLog.print(speakerVolumePercent_);
  AppLog.println("%");
  showNotice(String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%", 2000);
  renderNow();
}

void DJConnectApp::applyWakeWordEnabled(bool enabled, bool notify) {
  wakeWordEnabled_ = enabled;
  wakeWord_.setEnabled(wakeWordEnabled_);
  if (!wakeWordEnabled_) {
    wakeWord_.releaseResources();
  }
  saveDisplaySettings();
  AppLog.print("Settings: wake word ");
  AppLog.println(wakeWordEnabled_ ? "enabled" : "disabled");
  if (notify) {
    showNotice(String(I18n::text("wake_word")) + " " + I18n::onOff(wakeWordEnabled_), 1800);
  }
  renderNow();
  sendHomeAssistantStatusIfDue(true);
}

void DJConnectApp::applyLogLevelSelection() {
  logLevel_ = logLevelValue(logLevelSelection_);
  saveDisplaySettings();
  AppLog.setLevel(logLevel_);
  showNotice(String(I18n::text("log_level")) + " " + logLevelLabel(logLevel_), 2000);
  renderNow();
}

void DJConnectApp::applyShuffleSelection() {
  const bool enabled = shuffleValue(shuffleSelection_);
  AppLog.print("Playback: setting shuffle ");
  AppLog.println(enabled ? "on" : "off");
  showNotice(I18n::text("shuffle"), 1600);
  renderNow();
  if (spotify_.setShuffle(enabled)) {
    AppLog.print("Playback: shuffle set to ");
    AppLog.println(enabled ? "on" : "off");
    showNotice(shuffleLabel(enabled), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: shuffle failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::applyRepeatSelection() {
  const String repeat = repeatValue(repeatSelection_);
  AppLog.print("Playback: setting repeat ");
  AppLog.println(repeat);
  showNotice(I18n::text("repeat"), 1600);
  renderNow();
  if (spotify_.setRepeatMode(repeat)) {
    AppLog.print("Playback: repeat set to ");
    AppLog.println(repeat);
    showNotice(repeatLabel(repeat), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: repeat failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::applySleepTimeoutSelection() {
  // The selected timeout controls full ESP32-S3 deep sleep, not just the display backlight.
  deviceSleepTimeoutMs_ = sleepTimeoutValueMs(sleepTimeoutSelection_);
  saveDisplaySettings();
  AppLog.print("Settings: turn off after ");
  AppLog.print(deviceSleepTimeoutMs_ / 60000UL);
  AppLog.println(" min");
  showNotice(String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_), 2000);
  renderNow();
}

void DJConnectApp::saveDisplaySettings() {
  languageCode_ = I18n::languageCode();
  provisioning_.saveDisplaySettings(
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_,
      screenBrightnessPercent_,
      languageCode_,
      themeCode_,
      logLevel_,
      speakerVolumePercent_,
      wakeWordEnabled_,
      volumeFeedbackEnabled_);
}

void DJConnectApp::hardResetToProvisioning() {
  AppLog.println("Factory reset: clearing local credentials, tokens, settings and caches");
  sound_.playHardReset();
  display_.wakeForUserActivity();
  display_.showBootMessage(I18n::text("boot_factory_reset"), battery_);
  ledRing_.showSolid(CRGB::Yellow, 100);

  voiceRecorder_.abort();
  haDevice_.clearPairing();
  spotify_.clearStoredTokens();

  provisioning_.requestSetupMode();

  if (LittleFS.begin(true)) {
    fs::File root = LittleFS.open("/");
    fs::File file = root.openNextFile();
    while (file) {
      const String path = file.name();
      file.close();
      if (!path.isEmpty()) {
        LittleFS.remove(path);
      }
      file = root.openNextFile();
    }
    root.close();
    AppLog.println("Local LittleFS caches cleared");
  }

  WiFi.disconnect(true, true);
  responsiveDelay(600);
  ESP.restart();
}

void DJConnectApp::changeWifiToProvisioning() {
  AppLog.println("WiFi change: restarting to setup portal, Home Assistant pairing kept");
  voiceRecorder_.abort();
  display_.wakeForUserActivity();
  display_.showBootMessage(I18n::text("boot_setup_device"), battery_);
  sound_.playSoftReset();
  provisioning_.requestWifiChangeMode();
  WiFi.disconnect(false, false);
  responsiveDelay(450);
  ESP.restart();
}

void DJConnectApp::resetHomeAssistantPairing() {
  AppLog.println("Home Assistant: clearing pairing and restarting to pairing mode");
  voiceRecorder_.abort();
  homeAssistantPaired_ = false;
  playback_.error = "";
  display_.wakeForUserActivity();
  display_.showBootMessage(I18n::text("boot_reset_pairing"), battery_);
  sound_.playSoftReset();
  ledRing_.showSolid(CRGB::White, 100);
  responsiveDelay(140);
  ledRing_.setPowerPercent(0);
  responsiveDelay(90);
  ledRing_.showSolid(CRGB::White, 100);
  responsiveDelay(140);
  haDevice_.clearHomeAssistantPairing();
  AppLog.println("Home Assistant: pairing reset complete, restarting");
  responsiveDelay(450);
  ESP.restart();
}

bool DJConnectApp::isMenuActive() const {
  return isMenuScreen(activeScreen_);
}

size_t DJConnectApp::menuItemCount(UiScreen screen) const {
  if (screen == UiScreen::Queue) {
    return queue_.available && queue_.count > 0 ? queue_.count : 1;
  }
  return itemCount(screen, playlists_, deviceList_);
}

size_t DJConnectApp::selectedIndexForScreen(UiScreen screen) const {
  switch (screen) {
    case UiScreen::RootMenu:
      return rootMenuSelection_;
    case UiScreen::Settings:
      return settingsSelection_;
    case UiScreen::Games:
      return gamesSelection_;
    case UiScreen::Help:
      return helpSelection_;
    case UiScreen::DimTimeout:
      return dimTimeoutSelection_;
    case UiScreen::Playlists:
      return playlistSelection_;
    case UiScreen::Brightness:
      return brightnessSelection_;
    case UiScreen::Language:
      return languageSelection_;
    case UiScreen::Theme:
      return themeSelection_;
    case UiScreen::LogLevel:
      return logLevelSelection_;
    case UiScreen::SpeakerVolume:
      return speakerVolumeSelection_;
    case UiScreen::ShuffleMode:
      return shuffleSelection_;
    case UiScreen::RepeatMode:
      return repeatSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
    case UiScreen::ChangeWifiConfirm:
    case UiScreen::ResetPairingConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
      return aboutSelection_;
    case UiScreen::Queue:
      return queueSelection_;
    case UiScreen::AlbumArt:
    case UiScreen::Logs:
    case UiScreen::Pong:
    case UiScreen::Asteroids:
    case UiScreen::Flyer:
    case UiScreen::MazeChase:
    case UiScreen::NowPlaying:
      return 0;
  }
  return 0;
}

size_t &DJConnectApp::selectedIndexRefForScreen(UiScreen screen) {
  switch (screen) {
    case UiScreen::RootMenu:
      return rootMenuSelection_;
    case UiScreen::Settings:
      return settingsSelection_;
    case UiScreen::Games:
      return gamesSelection_;
    case UiScreen::Help:
      return helpSelection_;
    case UiScreen::DimTimeout:
      return dimTimeoutSelection_;
    case UiScreen::Playlists:
      return playlistSelection_;
    case UiScreen::Brightness:
      return brightnessSelection_;
    case UiScreen::Language:
      return languageSelection_;
    case UiScreen::Theme:
      return themeSelection_;
    case UiScreen::LogLevel:
      return logLevelSelection_;
    case UiScreen::SpeakerVolume:
      return speakerVolumeSelection_;
    case UiScreen::ShuffleMode:
      return shuffleSelection_;
    case UiScreen::RepeatMode:
      return repeatSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
    case UiScreen::ChangeWifiConfirm:
    case UiScreen::ResetPairingConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
      return aboutSelection_;
    case UiScreen::Queue:
      return queueSelection_;
    case UiScreen::AlbumArt:
    case UiScreen::Logs:
    case UiScreen::Pong:
    case UiScreen::Asteroids:
    case UiScreen::Flyer:
    case UiScreen::MazeChase:
    case UiScreen::NowPlaying:
      return rootMenuSelection_;
  }
  return rootMenuSelection_;
}

void DJConnectApp::applyTheme() {
  // The ESP has no browser preference, so auto maps to the normal dark TFT palette.
  display_.setLightTheme(themeCode_ == "light");
}

void DJConnectApp::handleEncoderTurn(int encoderSteps) {
  if (!playback_.hasPlayback || !playback_.supportsVolume) {
    return;
  }
  if (volumeFeedbackEnabled_) {
    sound_.playVolumeTick(encoderSteps);
  }
  // Show volume instantly while delaying the HTTPS call until the user stops turning.
  int baseVolume = pendingVolume_ >= 0 ? pendingVolume_ : playback_.volume;
  if (baseVolume < 0) {
    baseVolume = Config::MaxSpotifyVolumePercent / 2;
  }

  pendingVolume_ = constrain(
      baseVolume + (encoderSteps * Config::VolumeStepPercent),
      0,
      Config::MaxSpotifyVolumePercent);
  pendingVolumeChangedAt_ = millis();
  showNotice(String(I18n::text("volume")) + " " + String(pendingVolume_) + "%", 1200);
  renderNow();
}

void DJConnectApp::flushPendingVolume() {
  if (pendingVolume_ < 0 || millis() - pendingVolumeChangedAt_ < Config::VolumeFlushDelayMs) {
    return;
  }

  // After the debounce window, commit the latest pending volume to Spotify asynchronously.
  const int volume = pendingVolume_;
  pendingVolume_ = -1;

  if (spotify_.queueVolume(volume)) {
    playback_.volume = volume;
    playback_.error = "";
    showNotice(String(I18n::text("volume")) + " " + String(volume) + "%", 2000);
  } else {
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::processVolumeResult() {
  VolumeResult result;
  if (!spotify_.pollVolumeResult(result)) {
    return;
  }

  if (result.ok) {
    playback_.error = "";
    playback_.volume = result.volume;
    showNotice(result.message, 2000);
  } else {
    playback_.error = result.message;
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::processStressTest() {
  if (!stressTestActive_) {
    return;
  }
  if (voiceRecording_ || haPairingScreenActive_ || lowBatteryGuardActive_ || criticalBatteryGuardActive_ || chargingBatteryGuardActive_) {
    stopStressTest("guard state active");
    return;
  }

  const uint32_t now = millis();
  if (now - stressTestStartedAt_ >= Config::StressTestDurationMs) {
    stopStressTest("duration complete");
    return;
  }
  if (now - lastStressTestStepAt_ < Config::StressTestStepIntervalMs) {
    return;
  }
  lastStressTestStepAt_ = now;
  runStressTestStep();
}

void DJConnectApp::toggleStressTest() {
  if (stressTestActive_) {
    stopStressTest("manual stop");
    return;
  }
  stressTestActive_ = true;
  stressTestStartedAt_ = millis();
  lastStressTestStepAt_ = 0;
  stressTestStepCount_ = 0;
  AppLog.println("Stress test: started");
  showNotice(I18n::text("stress_test_started"), 1800);
  renderNow();
}

void DJConnectApp::stopStressTest(const String &reason) {
  if (!stressTestActive_) {
    return;
  }
  stressTestActive_ = false;
  AppLog.print("Stress test: stopped steps=");
  AppLog.print(stressTestStepCount_);
  AppLog.print(" reason=");
  AppLog.println(reason);
  activeScreen_ = UiScreen::Settings;
  showNotice(I18n::text("stress_test_stopped"), 2200);
  renderNow();
}

void DJConnectApp::runStressTestStep() {
  stressTestStepCount_++;
  const uint32_t randomValue = esp_random();
  switch (randomValue % 8) {
    case 0:
      activeScreen_ = UiScreen::NowPlaying;
      break;
    case 1:
      activeScreen_ = UiScreen::RootMenu;
      rootMenuSelection_ = (rootMenuSelection_ + 1) % RootMenuItemCount;
      break;
    case 2:
      activeScreen_ = UiScreen::Settings;
      settingsSelection_ = (settingsSelection_ + 1) % SettingsItemCount;
      break;
    case 3:
      activeScreen_ = UiScreen::About;
      aboutSelection_ = (aboutSelection_ + 1) % AboutItemCount;
      break;
    case 4:
      activeScreen_ = UiScreen::Logs;
      break;
    case 5:
      activeScreen_ = UiScreen::Queue;
      break;
    case 6:
      activeScreen_ = UiScreen::SoundOutputs;
      soundOutputSelection_ = (soundOutputSelection_ + 1) % max(menuItemCount(UiScreen::SoundOutputs), static_cast<size_t>(1));
      break;
    default:
      activeScreen_ = UiScreen::Playlists;
      playlistSelection_ = (playlistSelection_ + 1) % max(menuItemCount(UiScreen::Playlists), static_cast<size_t>(1));
      break;
  }

  if (stressTestStepCount_ % 20 == 0) {
    AppLog.print("Stress test: step=");
    AppLog.print(stressTestStepCount_);
    AppLog.print(" free_heap=");
    AppLog.print(ESP.getFreeHeap());
    AppLog.print(" largest_block=");
    AppLog.println(ESP.getMaxAllocHeap());
  }
  renderNow();
}

void DJConnectApp::resetPong() {
  pongPaddleY_ = 86;
  pongBallX_ = 160;
  pongBallY_ = 86;
  pongVelocityX_ = 3;
  pongVelocityY_ = 2;
  pongScore_ = 0;
  pongMissFlashUntil_ = 0;
  lastPongFrameAt_ = 0;
}

void DJConnectApp::updateGameHighScore(int &highScore, int score) {
  if (score <= highScore) {
    return;
  }
  highScore = score;
  provisioning_.saveGameHighScores(
      static_cast<uint32_t>(max(pongHighScore_, 0)),
      static_cast<uint32_t>(max(asteroidHighScore_, 0)),
      static_cast<uint32_t>(max(flyerHighScore_, 0)),
      static_cast<uint32_t>(max(mazeHighScore_, 0)));
}

void DJConnectApp::updatePong() {
  const uint32_t now = millis();
  if (!display_.isOn() || display_.backlightPercent() == 0) {
    lastPongFrameAt_ = now;
    return;
  }
  if (now - lastPongFrameAt_ < 33) {
    return;
  }
  lastPongFrameAt_ = now;

  pongBallX_ += pongVelocityX_;
  pongBallY_ += pongVelocityY_;
  if (pongBallY_ <= 42 || pongBallY_ >= 156) {
    pongVelocityY_ = -pongVelocityY_;
    pongBallY_ = constrain(pongBallY_, 42, 156);
    if (volumeFeedbackEnabled_) {
      sound_.playPongBounce();
    }
  }
  if (pongBallX_ >= 306) {
    pongVelocityX_ = -abs(pongVelocityX_);
    if (volumeFeedbackEnabled_) {
      sound_.playPongBounce();
    }
  }
  if (pongBallX_ <= 30) {
    if (pongBallY_ >= pongPaddleY_ - 4 && pongBallY_ <= pongPaddleY_ + 38) {
      pongVelocityX_ = abs(pongVelocityX_);
      pongScore_++;
      updateGameHighScore(pongHighScore_, pongScore_);
      if (volumeFeedbackEnabled_) {
        sound_.playConfirm();
      }
    } else {
      pongBallX_ = 160;
      pongBallY_ = 86;
      pongVelocityX_ = 3;
      pongVelocityY_ = (esp_random() & 1) ? 2 : -2;
      pongScore_ = 0;
      pongMissFlashUntil_ = now + 450;
      if (volumeFeedbackEnabled_) {
        sound_.playPongMiss();
      }
    }
  }
  renderNow();
}

void DJConnectApp::resetAsteroids() {
  asteroidShipX_ = 160;
  asteroidShipY_ = 138;
  asteroidX_ = 40 + static_cast<int>(esp_random() % 240);
  asteroidY_ = 48;
  asteroidVelocityX_ = (esp_random() & 1) ? 2 : -2;
  asteroidVelocityY_ = 2;
  asteroidBulletActive_ = false;
  asteroidBulletY_ = 0;
  asteroidScore_ = 0;
  asteroidFlashUntil_ = 0;
  lastAsteroidsFrameAt_ = 0;
}

void DJConnectApp::fireAsteroids() {
  if (asteroidBulletActive_) {
    return;
  }
  asteroidBulletActive_ = true;
  asteroidBulletY_ = asteroidShipY_ - 18;
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
}

void DJConnectApp::updateAsteroids() {
  if (!display_.isOn()) {
    return;
  }
  const uint32_t now = millis();
  if (lastAsteroidsFrameAt_ == 0) {
    lastAsteroidsFrameAt_ = now;
    renderNow();
    return;
  }
  if (now - lastAsteroidsFrameAt_ < 36) {
    return;
  }
  lastAsteroidsFrameAt_ = now;
  asteroidX_ += asteroidVelocityX_;
  asteroidY_ += asteroidVelocityY_;
  if (asteroidX_ < 24 || asteroidX_ > 296) {
    asteroidVelocityX_ = -asteroidVelocityX_;
  }
  if (asteroidBulletActive_) {
    asteroidBulletY_ -= 8;
    if (asteroidBulletY_ < 38) {
      asteroidBulletActive_ = false;
    } else if (abs(asteroidX_ - asteroidShipX_) < 16 && abs(asteroidY_ - asteroidBulletY_) < 16) {
      asteroidScore_++;
      updateGameHighScore(asteroidHighScore_, asteroidScore_);
      asteroidBulletActive_ = false;
      asteroidX_ = 30 + static_cast<int>(esp_random() % 260);
      asteroidY_ = 46;
      asteroidVelocityX_ = (esp_random() & 1) ? 2 : -2;
      asteroidVelocityY_ = 2 + static_cast<int>(min(asteroidScore_ / 5, 3));
      if (volumeFeedbackEnabled_) {
        sound_.playPongBounce();
      }
    }
  }
  if (asteroidY_ > 150) {
    asteroidFlashUntil_ = now + 350;
    if (volumeFeedbackEnabled_) {
      sound_.playPongMiss();
    }
    asteroidX_ = 30 + static_cast<int>(esp_random() % 260);
    asteroidY_ = 46;
    asteroidScore_ = 0;
  }
  renderNow();
}

void DJConnectApp::resetFlyer() {
  flyerPlaneY_ = 86;
  flyerObstacleX_ = 300;
  flyerObstacleY_ = 52 + static_cast<int>(esp_random() % 92);
  flyerShotActive_ = false;
  flyerShotX_ = 0;
  flyerScore_ = 0;
  flyerFlashUntil_ = 0;
  lastFlyerFrameAt_ = 0;
}

void DJConnectApp::fireFlyer() {
  if (flyerShotActive_) {
    return;
  }
  flyerShotActive_ = true;
  flyerShotX_ = 58;
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
}

void DJConnectApp::updateFlyer() {
  if (!display_.isOn()) {
    return;
  }
  const uint32_t now = millis();
  if (lastFlyerFrameAt_ == 0) {
    lastFlyerFrameAt_ = now;
    renderNow();
    return;
  }
  if (now - lastFlyerFrameAt_ < 36) {
    return;
  }
  lastFlyerFrameAt_ = now;
  flyerObstacleX_ -= 4 + min(flyerScore_ / 6, 4);
  if (flyerShotActive_) {
    flyerShotX_ += 9;
    if (flyerShotX_ > 310) {
      flyerShotActive_ = false;
    } else if (abs(flyerShotX_ - flyerObstacleX_) < 16 && abs(flyerPlaneY_ - flyerObstacleY_) < 24) {
      flyerScore_++;
      updateGameHighScore(flyerHighScore_, flyerScore_);
      flyerShotActive_ = false;
      flyerObstacleX_ = 310;
      flyerObstacleY_ = 52 + static_cast<int>(esp_random() % 92);
      if (volumeFeedbackEnabled_) {
        sound_.playPongBounce();
      }
    }
  }
  if (flyerObstacleX_ < 24) {
    flyerObstacleX_ = 310;
    flyerObstacleY_ = 52 + static_cast<int>(esp_random() % 92);
    flyerScore_++;
    updateGameHighScore(flyerHighScore_, flyerScore_);
  }
  if (flyerObstacleX_ < 64 && flyerObstacleX_ > 28 && abs(flyerPlaneY_ - flyerObstacleY_) < 28) {
    flyerFlashUntil_ = now + 350;
    flyerScore_ = 0;
    flyerObstacleX_ = 310;
    flyerObstacleY_ = 52 + static_cast<int>(esp_random() % 92);
    if (volumeFeedbackEnabled_) {
      sound_.playPongMiss();
    }
  }
  renderNow();
}

void DJConnectApp::resetMazeChase() {
  mazePlayerX_ = 52;
  mazePlayerLane_ = 1;
  mazeGhostX_ = 278;
  mazeGhostLane_ = static_cast<int>(esp_random() % 3);
  mazePelletX_ = 90 + static_cast<int>(esp_random() % 170);
  mazePelletLane_ = static_cast<int>(esp_random() % 3);
  mazeScore_ = 0;
  mazePowerUntil_ = 0;
  mazeFlashUntil_ = 0;
  lastMazeFrameAt_ = 0;
}

void DJConnectApp::switchMazeLane() {
  mazePlayerLane_ = (mazePlayerLane_ + 1) % 3;
  if (volumeFeedbackEnabled_) {
    sound_.playMenuTick(1);
  }
  renderNow();
}

void DJConnectApp::updateMazeChase() {
  if (!display_.isOn()) {
    return;
  }
  const uint32_t now = millis();
  if (lastMazeFrameAt_ == 0) {
    lastMazeFrameAt_ = now;
    renderNow();
    return;
  }
  if (now - lastMazeFrameAt_ < 48) {
    return;
  }
  lastMazeFrameAt_ = now;

  const bool ghostVulnerable = now < mazePowerUntil_;
  const int chaseSpeed = ghostVulnerable ? 1 : 1 + min(mazeScore_ / 10, 3);
  if (mazeGhostX_ > mazePlayerX_) {
    mazeGhostX_ -= chaseSpeed;
  } else {
    mazeGhostX_ += chaseSpeed;
  }
  const uint32_t laneChance = ghostVulnerable ? 20 : 14;
  if ((esp_random() % laneChance) == 0) {
    if (mazeGhostLane_ < mazePlayerLane_) {
      mazeGhostLane_++;
    } else if (mazeGhostLane_ > mazePlayerLane_) {
      mazeGhostLane_--;
    }
  }

  if (abs(mazePlayerX_ - mazePelletX_) < 13 && mazePlayerLane_ == mazePelletLane_) {
    mazeScore_++;
    updateGameHighScore(mazeHighScore_, mazeScore_);
    mazePowerUntil_ = now + 6000;
    mazePelletX_ = 42 + static_cast<int>(esp_random() % 236);
    mazePelletLane_ = static_cast<int>(esp_random() % 3);
    if (volumeFeedbackEnabled_) {
      sound_.playConfirm();
    }
  }

  if (abs(mazePlayerX_ - mazeGhostX_) < 15 && mazePlayerLane_ == mazeGhostLane_) {
    if (ghostVulnerable) {
      mazeScore_ += 5;
      updateGameHighScore(mazeHighScore_, mazeScore_);
      mazeGhostX_ = mazePlayerX_ < 160 ? 278 : 42;
      mazeGhostLane_ = static_cast<int>(esp_random() % 3);
      mazeFlashUntil_ = now + 180;
      if (volumeFeedbackEnabled_) {
        sound_.playConfirm();
      }
      renderNow();
      return;
    }
    mazeFlashUntil_ = now + 350;
    mazeScore_ = 0;
    mazePowerUntil_ = 0;
    mazePlayerX_ = 52;
    mazePlayerLane_ = 1;
    mazeGhostX_ = 278;
    mazeGhostLane_ = static_cast<int>(esp_random() % 3);
    mazePelletX_ = 90 + static_cast<int>(esp_random() % 170);
    mazePelletLane_ = static_cast<int>(esp_random() % 3);
    if (volumeFeedbackEnabled_) {
      sound_.playPongMiss();
    }
  }
  renderNow();
}

bool DJConnectApp::handleDeviceCommand(const DeviceCommand &command, String &message) {
  if (command.type == DeviceCommandType::Status) {
    AppLog.println("Device command: status");
    refreshPlaybackAndBattery();
    sendHomeAssistantStatusIfDue(true);
    message = "Status sent";
    return true;
  }
  if (command.type == DeviceCommandType::Ota) {
    message = "OTA requires /api/device/ota payload";
    return false;
  }
  if (command.type == DeviceCommandType::DjResponse) {
    AppLog.println("Device command: DJ response");
    bool spoken = false;
    const bool ok = handleDjResponseText(command.value, command.audioUrl, spoken);
    message = ok ? "DJ response displayed" : "DJ response failed";
    return ok;
  }
  if (command.type == DeviceCommandType::ScreenBrightness) {
    screenBrightnessPercent_ = constrain(command.numericValue, 25, 100);
    for (size_t index = 0; index < BrightnessOptionCount; index++) {
      if (brightnessValuePercent(index) == screenBrightnessPercent_) {
        brightnessSelection_ = index;
      }
    }
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("Device command: screen brightness ");
    AppLog.println(screenBrightnessPercent_);
    showNotice(String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%", 1800);
    renderNow();
    message = "Screen brightness updated";
    return true;
  }
  if (command.type == DeviceCommandType::ScreenDimTimeout) {
    screenOffTimeoutMs_ = constrain(static_cast<uint32_t>(command.numericValue) * 1000UL, 30000UL, 240000UL);
    for (size_t index = 0; index < DimTimeoutOptionCount; index++) {
      if (dimTimeoutValueMs(index) == screenOffTimeoutMs_) {
        dimTimeoutSelection_ = index;
      }
    }
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("Device command: screen dim timeout ");
    AppLog.println(screenOffTimeoutMs_);
    showNotice(String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_), 1800);
    renderNow();
    message = "Screen timeout updated";
    return true;
  }
  if (command.type == DeviceCommandType::DeepSleepTimeout) {
    deviceSleepTimeoutMs_ = constrain(static_cast<uint32_t>(command.numericValue) * 60000UL, 300000UL, 3600000UL);
    sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("Device command: turn off after ");
    AppLog.println(deviceSleepTimeoutMs_);
    showNotice(String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_), 1800);
    renderNow();
    message = "Turn off timeout updated";
    return true;
  }
  if (command.type == DeviceCommandType::SpeakerVolume) {
    speakerVolumePercent_ = constrain(command.numericValue, 25, 100);
    for (size_t index = 0; index < SpeakerVolumeOptionCount; index++) {
      if (speakerVolumeValuePercent(index) == speakerVolumePercent_) {
        speakerVolumeSelection_ = index;
      }
    }
    sound_.setVolumePercent(speakerVolumePercent_);
    saveDisplaySettings();
    AppLog.print("Device command: speaker volume ");
    AppLog.println(speakerVolumePercent_);
    showNotice(String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%", 1800);
    renderNow();
    message = "Speaker volume updated";
    return true;
  }
  if (command.type == DeviceCommandType::Language) {
    language_ = I18n::languageFromCode(command.value);
    I18n::setLanguage(language_);
    languageCode_ = I18n::languageCode();
    languageSelection_ = language_ == Language::Dutch ? 1 : 0;
    saveDisplaySettings();
    AppLog.print("Device command: language ");
    AppLog.println(languageCode_);
    showNotice(String(I18n::text("language")) + " " + languageLabel(language_), 1800);
    renderNow();
    message = "Language updated";
    return true;
  }
  if (command.type == DeviceCommandType::Theme) {
    String theme = command.value;
    theme.toLowerCase();
    if (theme != "auto" && theme != "light") {
      theme = "dark";
    }
    themeCode_ = theme;
    for (size_t index = 0; index < ThemeOptionCount; index++) {
      if (themeValue(index) == themeCode_) {
        themeSelection_ = index;
        break;
      }
    }
    applyTheme();
    saveDisplaySettings();
    AppLog.print("Device command: theme ");
    AppLog.println(themeCode_);
    showNotice(String(I18n::text("theme")) + " " + themeLabel(themeCode_), 1800);
    renderNow();
    message = "Theme updated";
    return true;
  }
  if (command.type == DeviceCommandType::LogLevel) {
    String level = command.value;
    level.toLowerCase();
    if (level != "debug" && level != "warning" && level != "error") {
      level = "info";
    }
    logLevel_ = level;
    for (size_t index = 0; index < LogLevelOptionCount; index++) {
      if (logLevelValue(index) == logLevel_) {
        logLevelSelection_ = index;
        break;
      }
    }
    saveDisplaySettings();
    AppLog.setLevel(logLevel_);
    showNotice(String(I18n::text("log_level")) + " " + logLevelLabel(logLevel_), 1800);
    renderNow();
    message = "Log level updated";
    return true;
  }
  if (command.type == DeviceCommandType::WakeWord) {
    applyWakeWordEnabled(command.numericValue != 0, true);
    message = "Wake word updated";
    return true;
  }

  if (!playbackProxyReady()) {
    AppLog.println("Device command ignored: playback not connected");
    message = I18n::text("spotify_not_connected");
    return false;
  }

  switch (command.type) {
    case DeviceCommandType::Play:
      AppLog.println("Device command: play");
      if (playback_.isPlaying) {
        message = I18n::text("playing");
        return true;
      }
      pauseOrResume();
      message = playback_.error.isEmpty() ? I18n::text("playing") : playback_.error;
      return playback_.error.isEmpty();

    case DeviceCommandType::Pause:
      AppLog.println("Device command: pause");
      if (!playback_.isPlaying) {
        message = I18n::text("paused");
        return true;
      }
      pauseOrResume();
      message = playback_.error.isEmpty() ? I18n::text("paused") : playback_.error;
      return playback_.error.isEmpty();

    case DeviceCommandType::PlayPause:
      AppLog.println("Device command: play/pause");
      pauseOrResume();
      message = playback_.error.isEmpty() ? (playback_.isPlaying ? I18n::text("playing") : I18n::text("paused")) : playback_.error;
      return playback_.error.isEmpty();

    case DeviceCommandType::Next:
      AppLog.println("Device command: next song");
      goToNextTrack();
      message = I18n::text("next_track");
      return true;

    case DeviceCommandType::Previous:
      AppLog.println("Device command: previous song");
      goToPreviousTrack();
      message = I18n::text("previous_track");
      return true;

    case DeviceCommandType::Status:
    case DeviceCommandType::Ota:
    case DeviceCommandType::DjResponse:
    case DeviceCommandType::ScreenBrightness:
    case DeviceCommandType::ScreenDimTimeout:
    case DeviceCommandType::DeepSleepTimeout:
    case DeviceCommandType::SpeakerVolume:
    case DeviceCommandType::Language:
    case DeviceCommandType::Theme:
    case DeviceCommandType::LogLevel:
    case DeviceCommandType::WakeWord:
      break;

    case DeviceCommandType::Shuffle:
      AppLog.print("Device command: shuffle ");
      AppLog.println(command.numericValue != 0 ? "on" : "off");
      if (!spotify_.setShuffle(command.numericValue != 0)) {
        message = playback_.error;
        return false;
      }
      playback_.shuffle = command.numericValue != 0;
      message = shuffleLabel(playback_.shuffle);
      lastPlaybackPollAt_ = 0;
      renderNow();
      return true;

    case DeviceCommandType::Repeat:
      AppLog.print("Device command: repeat ");
      AppLog.println(command.value);
      if (!spotify_.setRepeatMode(command.value)) {
        message = playback_.error;
        return false;
      }
      playback_.repeatState = command.value == "repeat_once" ? "track" : (command.value == "repeat_infinite" ? "context" : command.value);
      message = repeatLabel(playback_.repeatState);
      lastPlaybackPollAt_ = 0;
      renderNow();
      return true;

    case DeviceCommandType::Volume:
      if (!playback_.hasPlayback || !playback_.supportsVolume) {
        AppLog.println("Device command ignored: volume unavailable");
        message = "Volume unavailable";
        return false;
      }
      AppLog.print("Device command: volume ");
      AppLog.println(command.numericValue);
      if (spotify_.queueVolume(constrain(command.numericValue, 0, Config::MaxSpotifyVolumePercent))) {
        playback_.volume = constrain(command.numericValue, 0, Config::MaxSpotifyVolumePercent);
        playback_.error = "";
        renderNow();
        message = "Volume queued";
        return true;
      }
      message = "Volume queue unavailable";
      return false;
      break;

    case DeviceCommandType::TransferOutput:
      AppLog.print("Device command: transfer output ");
      AppLog.println(command.value);
      if (!transferToOutputByNameOrId(command.value)) {
        AppLog.print("Device command failed: ");
        AppLog.println(playback_.error);
        message = playback_.error;
        return false;
      }
      message = "Output switched";
      return true;

    case DeviceCommandType::StartPlaylist:
      AppLog.print("Device command: start playlist ");
      AppLog.println(command.value);
      if (!startPlaylistByNameOrUri(command.value)) {
        AppLog.print("Device command failed: ");
        AppLog.println(playback_.error);
        message = playback_.error;
        return false;
      }
      message = I18n::text("playlist_started");
      return true;

    case DeviceCommandType::None:
      break;
  }
  if (!command.value.isEmpty()) {
    AppLog.print("Device command unsupported: ");
    AppLog.println(command.value);
  }
  message = "Unsupported command";
  return false;
}

void DJConnectApp::pauseOrResume() {
  if (!playbackProxyReady()) {
    AppLog.println("Playback: play/pause ignored, proxy unavailable");
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  const uint32_t now = millis();
  if (now - lastPauseToggleAt_ < 900) {
    AppLog.println("Playback: play/pause ignored, debounce");
    return;
  }
  lastPauseToggleAt_ = now;

  if (playback_.isPlaying) {
    AppLog.println("Playback: pause requested");
    if (spotify_.pausePlayback()) {
      playback_.isPlaying = false;
      AppLog.println("Playback: pause accepted");
      showNotice(I18n::text("paused"));
      lastPlaybackPollAt_ = 0;
    } else {
      AppLog.print("Playback: pause failed: ");
      AppLog.println(playback_.error);
      showNotice(playback_.error, 3500);
    }
  } else {
    AppLog.println("Playback: play requested");
    if (spotify_.resumePlayback()) {
      playback_.isPlaying = true;
      playback_.progressSyncedAt = millis();
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      AppLog.println("Playback: play accepted");
      showNotice(I18n::text("playing"));
      lastPlaybackPollAt_ = 0;
    } else {
      AppLog.print("Playback: play failed: ");
      AppLog.println(playback_.error);
      showNotice(playback_.error, 3500);
    }
  }
  renderNow();
}

void DJConnectApp::startLikedProxyPlaylist() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  AppLog.println("Playback: starting default playlist");
  showNotice(I18n::text("starting_liked_proxy"), 1600);
  renderNow();
  if (spotify_.startLikedProxyPlaylist()) {
    AppLog.println("Playback: default playlist command accepted");
    if (spotify_.setShuffle(true)) {
      playback_.shuffle = true;
      AppLog.println("Playback: default playlist shuffle enabled");
    } else {
      AppLog.print("Playback: default playlist shuffle failed: ");
      AppLog.println(playback_.error);
    }
    if (spotify_.setRepeatMode("off")) {
      playback_.repeatState = "off";
      AppLog.println("Playback: default playlist repeat disabled");
    } else {
      AppLog.print("Playback: default playlist repeat failed: ");
      AppLog.println(playback_.error);
    }
    playback_.trackName = I18n::text("starting_liked_proxy");
    playback_.artistName = "";
    playback_.hasPlayback = true;
    playback_.isPlaying = true;
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    display_.restartTitleScroll();
    display_.restartArtistScroll();
    showNotice(I18n::text("liked_proxy_started"), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: default playlist failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::handleVoiceButton() {
  if (!homeAssistantPaired_) {
    AppLog.println("Voice: HA not paired, using pause/resume");
    pauseOrResume();
    return;
  }
  if (voiceRecording_) {
    AppLog.println("Voice: stop requested while recording");
    stopVoiceRecordingAndSendText();
    return;
  }

#if defined(DJCONNECT_DEBUG_TEXT_COMMAND)
  AppLog.println("Voice: debug text command");
  String message;
  voiceState_ = VoiceState::SendingCommand;
  voiceClient_.sendStatus(false, "sending_command");
  if (voiceClient_.sendRecognizedText("Ik wil het nieuwste album van Pearl Jam horen", message)) {
    voiceState_ = VoiceState::Done;
    showNotice(message, 2500);
  } else {
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", message);
    showNotice(message, 3500);
  }
  renderNow();
  return;
#endif

  String message;
  voiceCancelRequested_ = false;
  voiceState_ = VoiceState::Connecting;
  AppLog.println("Voice: starting WAV recording");
  showNotice(I18n::text("voice_connecting"), 3000);
  renderNow();
  if (volumeFeedbackEnabled_) {
    sound_.playPttStartBlocking(Config::VoiceCueSettleMs);
  }
  if (!voiceRecorder_.start()) {
    const String error = voiceRecorder_.error();
    AppLog.print("Voice: mic start failed: ");
    AppLog.println(error);
    nextVoiceStartFromWakeWord_ = false;
    voiceStartedByWakeWord_ = false;
    voiceSilenceStartedAt_ = 0;
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showNotice(error, 3000);
    renderNow();
    return;
  }
  voiceRecording_ = true;
  voiceStopPending_ = false;
  voiceStartedByWakeWord_ = nextVoiceStartFromWakeWord_;
  nextVoiceStartFromWakeWord_ = false;
  voiceSilenceStartedAt_ = 0;
  voiceState_ = VoiceState::Listening;
  AppLog.println("Voice: listening");
  ledRing_.playPulse(CRGB::Yellow);
  voiceClient_.sendStatus(true, "recording");
  diagnostics_.lastDjText = I18n::text("voice_listening");
  display_.resetDjResponseOverlayCache();
  djResponseOverlayTitle_ = "DJConnect";
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + Config::VoiceMaxRecordMs + 1000;
  showNotice(I18n::text("voice_listening"), Config::VoiceMaxRecordMs + 1000);
  renderNow();
}

void DJConnectApp::stopVoiceRecordingAndSendText() {
  if (!voiceRecording_) {
    return;
  }
  voiceRecording_ = false;
  voiceStopPending_ = false;
  voiceStartedByWakeWord_ = false;
  nextVoiceStartFromWakeWord_ = false;
  voiceSilenceStartedAt_ = 0;
  voiceState_ = VoiceState::WaitingForResult;
  AppLog.print("Voice: recording stopped, elapsed_ms=");
  AppLog.println(voiceRecorder_.elapsedMs());
  diagnostics_.lastDjText = I18n::text("voice_processing");
  display_.resetDjResponseOverlayCache();
  djResponseOverlayTitle_ = "DJConnect";
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + 3000;
  showNotice(I18n::text("voice_processing"), 3000);
  renderNow();
  bool stopped = false;
  {
    ScopedWatchdogPause watchdogPause;
    stopped = voiceRecorder_.stop();
  }
  if (volumeFeedbackEnabled_) {
    sound_.playPttStop();
  }
  if (!stopped) {
    const String error = voiceRecorder_.error();
    AppLog.print("Voice: mic stop failed: ");
    AppLog.println(error);
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showDjResponseOverlay(I18n::text("voice_dj_response"), error, 6000);
    renderNow();
    return;
  }

  String message;
  voiceState_ = VoiceState::SendingCommand;
  AppLog.println("Voice: uploading WAV to HA integration");
  voiceClient_.sendStatus(false, "sending_command");
  showNotice("DJConnect...", 2500);
  renderNow();
  String audioUrl;
  if (voiceClient_.uploadWav(voiceRecorder_.wavPath(), message, &audioUrl)) {
    if (voiceCancelRequested_) {
      AppLog.println("Voice: cancelled after HA response");
      voiceClient_.sendStatus(false, "idle");
      voiceCancelRequested_ = false;
      voiceState_ = VoiceState::Idle;
      renderNow();
      return;
    }
    voiceState_ = VoiceState::Done;
    AppLog.println("Voice: command accepted");
    ledRing_.playPulse(CRGB::Green);
    voiceClient_.sendStatus(false, "idle");
    bool spoken = false;
    if (message != "Voice command sent") {
      handleDjResponseText(message, audioUrl, spoken);
    } else if (!audioUrl.isEmpty()) {
      spoken = djAudio_.play(audioUrl).spoken;
      showDjResponseOverlay(
          I18n::text("voice_dj_response"),
          spoken ? I18n::text("voice_response_played") : I18n::text("voice_response_audio_failed"),
          6000);
    } else {
      showDjResponseOverlay(I18n::text("voice_dj_response"), I18n::text("voice_no_dj_response"), 6000);
    }
  } else {
    voiceState_ = VoiceState::Error;
    AppLog.print("Voice: command failed: ");
    AppLog.println(message);
    voiceClient_.sendStatus(false, "error", message);
    if (voiceClient_.pairingInvalidated()) {
      markHomeAssistantPairingInvalid(message);
      return;
    }
    showDjResponseOverlay(I18n::text("voice_dj_response"), message, 6000);
  }
  voiceRecorder_.abort();
  if (voiceState_ == VoiceState::Done || voiceState_ == VoiceState::Error) {
    voiceState_ = VoiceState::Idle;
  }
  renderNow();
}

void DJConnectApp::processPendingWebVoiceText() {
  if (!webVoiceTextPending_) {
    return;
  }
  if (static_cast<int32_t>(millis() - pendingWebVoiceTextProcessAfter_) < 0) {
    return;
  }
  if (voiceRecording_ || voiceState_ != VoiceState::Idle) {
    return;
  }

  const String text = pendingWebVoiceText_;
  pendingWebVoiceText_ = "";
  webVoiceTextPending_ = false;
  pendingWebVoiceTextProcessAfter_ = 0;
  lastPlaybackPollAt_ = millis();
  lastHaStatusAt_ = millis();
  AppLog.print("Web voice: processing queued text chars=");
  AppLog.println(text.length());

  voiceCancelRequested_ = false;
  voiceState_ = VoiceState::SendingCommand;
  voiceRecording_ = false;
  webVoiceTextOnlyActive_ = true;
  webVoiceTextOnlyConsumeNext_ = true;
  webVoiceTextOnlyUntil_ = millis() + Config::WebVoiceTextOnlySuppressMs;
  notice_.visibleUntil = 0;
  renderNow();

  String message;
  String audioUrl;
  bool ok = false;
  {
    ScopedWatchdogPause watchdogPause;
    ok = voiceClient_.sendRecognizedText(text, message, &audioUrl);
  }
  serviceWatchdog();
  yield();

  if (ok) {
    if (message != "Voice command sent") {
      showDjResponseOverlay(I18n::text("voice_dj_response"), message, 6000);
      lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "skipped";
      AppLog.print("DJ response displayed chars=");
      AppLog.println(message.length());
    } else {
      showDjResponseOverlay(I18n::text("voice_dj_response"), I18n::text("voice_no_dj_response"), 6000);
      lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "skipped";
    }
  } else if (voiceClient_.pairingInvalidated()) {
    markHomeAssistantPairingInvalid(message);
  } else {
    showDjResponseOverlay(I18n::text("voice_dj_response"), message, 6000);
  }

  voiceState_ = VoiceState::Idle;
  voiceRecording_ = false;
  lastPlaybackPollAt_ = millis();
  lastHaStatusAt_ = millis();
  notice_.visibleUntil = 0;
  renderNow();
  serviceWatchdog();
  yield();
}

void DJConnectApp::cancelVoiceFlow(const char *reason) {
  AppLog.print("Voice: cancel requested");
  if (reason != nullptr && strlen(reason) > 0) {
    AppLog.print(" by ");
    AppLog.print(reason);
  }
  AppLog.println();
  voiceCancelRequested_ = true;
  if (voiceRecording_) {
    voiceRecorder_.abort();
    voiceRecording_ = false;
  }
  voiceStopPending_ = false;
  voiceStartedByWakeWord_ = false;
  nextVoiceStartFromWakeWord_ = false;
  voiceSilenceStartedAt_ = 0;
  voiceState_ = VoiceState::Idle;
  webVoiceTextPending_ = false;
  pendingWebVoiceText_ = "";
  pendingWebVoiceTextProcessAfter_ = 0;
  webVoiceTextOnlyActive_ = false;
  webVoiceTextOnlyConsumeNext_ = false;
  djResponseOverlayVisible_ = false;
  display_.resetDjResponseOverlayCache();
  sound_.requestStopStreaming();
  ledRing_.clear();
  showNotice("Cancelled", 1200);
  renderNow();
}

void DJConnectApp::goToNextTrack() {
  if (!playbackProxyReady()) {
    AppLog.println("Playback: next track ignored, proxy unavailable");
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  AppLog.println("Playback: next track requested");
  if (spotify_.nextTrack()) {
    AppLog.println("Playback: next track accepted");
    sound_.playNextTrack();
    // Optimistic UI while Spotify switches tracks; the next poll replaces this with real metadata.
    playback_.trackName = I18n::text("loading_next_track");
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice(I18n::text("next_track"));

    if (!playback_.isPlaying && spotify_.resumePlayback()) {
      AppLog.println("Playback: resume after next accepted");
      playback_.isPlaying = true;
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      showNotice(I18n::text("playing"));
    }

    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: next track failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::goToPreviousTrack() {
  if (!playbackProxyReady()) {
    AppLog.println("Playback: previous track ignored, proxy unavailable");
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  AppLog.println("Playback: previous track requested");
  if (spotify_.previousTrack()) {
    AppLog.println("Playback: previous track accepted");
    sound_.playPreviousTrack();
    // Optimistic UI while Spotify switches tracks; the next poll replaces this with real metadata.
    playback_.trackName = I18n::text("loading_previous_track");
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice(I18n::text("previous_track"));
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: previous track failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::startSelectedPlaylist() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  if (!playlists_.available || playlists_.count == 0 || playlistSelection_ >= playlists_.count) {
    showNotice(playlists_.error.isEmpty() ? I18n::text("no_playlists") : playlists_.error.c_str(), 3000);
    renderNow();
    return;
  }

  const PlaylistItemState &playlist = playlists_.items[playlistSelection_];
  AppLog.print("Playback: starting playlist ");
  AppLog.println(playlist.name);
  showNotice(I18n::text("starting_playlist"), 1600);
  renderNow();
  if (spotify_.startPlaylist(playlist.uri)) {
    AppLog.println("Playback: playlist command accepted");
    playback_.hasPlayback = true;
    playback_.isPlaying = true;
    playback_.trackName = I18n::text("starting_playlist");
    playback_.artistName = playlist.name;
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    display_.restartTitleScroll();
    display_.restartArtistScroll();
    showNotice(I18n::text("playlist_started"), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: playlist failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

bool DJConnectApp::transferToOutputByNameOrId(const String &output) {
  if (output.isEmpty() || output == "none") {
    playback_.error = "Output missing";
    return false;
  }

  if (!deviceList_.available || deviceList_.count == 0) {
    spotify_.refreshDevices(deviceList_);
  }

  for (size_t index = 0; index < deviceList_.count; index++) {
    const SpotifyDeviceState &device = deviceList_.devices[index];
    String lowerDeviceName = device.name;
    String lowerOutput = output;
    lowerDeviceName.toLowerCase();
    lowerOutput.toLowerCase();
    const bool iphoneAlias = lowerOutput == "iphone" && lowerDeviceName.indexOf("iphone") >= 0;
    if (device.id == output || device.name == output || iphoneAlias) {
      if (!spotify_.transferPlayback(device.id, true)) {
        return false;
      }
      lastPlaybackPollAt_ = 0;
      spotify_.refreshPlayback();
      showNotice(String(I18n::text("playing_on")) + " " + device.name, 2200);
      renderNow();
      return true;
    }
  }

  playback_.error = "Output not found";
  return false;
}

bool DJConnectApp::startPlaylistByNameOrUri(const String &playlist) {
  if (playlist.isEmpty() || playlist == "none") {
    playback_.error = "Playlist missing";
    return false;
  }

  if (!playlists_.available || playlists_.count == 0) {
    spotify_.refreshPlaylists(playlists_);
  }

  for (size_t index = 0; index < playlists_.count; index++) {
    const PlaylistItemState &item = playlists_.items[index];
    if (item.uri == playlist || item.name == playlist) {
      if (!spotify_.startPlaylist(item.uri)) {
        return false;
      }
      playback_.hasPlayback = true;
      playback_.isPlaying = true;
      playback_.trackName = I18n::text("starting_playlist");
      playback_.artistName = item.name;
      playback_.progressMs = 0;
      playback_.durationMs = 0;
      playback_.progressSyncedAt = millis();
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      showNotice(I18n::text("playlist_started"), 2200);
      lastPlaybackPollAt_ = 0;
      renderNow();
      return true;
    }
  }

  playback_.error = "Playlist not found";
  return false;
}

void DJConnectApp::refreshPlaybackAndBattery() {
  showNotice(I18n::text("refreshing"));
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  if (playbackProxyReady()) {
    {
      ScopedWatchdogPause watchdogPause;
      spotify_.refreshPlayback();
    }
    serviceWatchdog();
    yield();
  } else {
    playback_.error = I18n::text("ha_pairing_invalid");
    showNotice(playback_.error, 2500);
  }
  lastBatteryPollAt_ = millis();
  lastPlaybackPollAt_ = millis();
  renderNow();
}

void DJConnectApp::pollBatteryIfDue() {
  if (millis() - lastBatteryPollAt_ <= Config::BatteryPollIntervalMs) {
    return;
  }

  lastBatteryPollAt_ = millis();
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  renderNow();
}

void DJConnectApp::pollPlaybackIfDue() {
  if (static_cast<int32_t>(millis() - playbackPollPausedUntil_) < 0) {
    return;
  }
  if (webVoiceTextPending_ ||
      webVoiceTextOnlyActive_ ||
      djResponseOverlayVisible_ ||
      voiceState_ == VoiceState::SendingCommand ||
      voiceState_ == VoiceState::WaitingForResult) {
    lastPlaybackPollAt_ = millis();
    return;
  }
  if (millis() - lastPlaybackPollAt_ <= Config::PlaybackPollIntervalMs) {
    return;
  }

  lastPlaybackPollAt_ = millis();
  if (!playbackProxyReady()) {
    return;
  }
  {
    ScopedWatchdogPause watchdogPause;
    spotify_.refreshPlayback();
  }
  serviceWatchdog();
  yield();
  if (spotify_.needsCredentialRefresh()) {
    if (haPairingPendingValidation_) {
      AppLog.println("Home Assistant: pending pairing rejected by playback proxy");
      haDevice_.clearHomeAssistantPairing();
      homeAssistantPaired_ = false;
      haPairingPendingValidation_ = false;
      haPairingScreenActive_ = true;
      lastHaStatusAt_ = 0;
      haDevice_.displayPairingCode();
      return;
    }
    markHomeAssistantPairingInvalid(I18n::text("ha_pairing_invalid"));
    return;
  }
  renderNow();
}

void DJConnectApp::openAlbumArtScreen() {
  if (!playback_.hasPlayback) {
    showNotice(I18n::text("no_current_song"), 1800);
    renderNow();
    return;
  }
  activeScreen_ = UiScreen::AlbumArt;
  menuStackSize_ = 0;
  display_.resetAlbumArtRenderCache();
  showNotice(I18n::text("current_song"), 900);
  renderNow();
  requestAlbumArtWithWakeWordPaused();
  renderNow();
}

void DJConnectApp::requestAlbumArtWithWakeWordPaused() {
  if (wakeWord_.available()) {
    AppLog.println("Album art: releasing wake-word runtime for download");
    wakeWord_.releaseResources();
  }
  albumArt_.requestCurrentSongArt(playback_);
  serviceWatchdog();
  yield();
}

void DJConnectApp::openQueueScreen() {
  activeScreen_ = UiScreen::Queue;
  menuStackSize_ = 0;
  queueSelection_ = 0;
  showNotice(I18n::text("loading_queue"), 1200);
  renderNow();
  if (playbackProxyReady()) {
    spotify_.refreshQueue(queue_);
    if (queueSelection_ >= queue_.count) {
      queueSelection_ = 0;
    }
  } else {
    queue_.available = false;
    queue_.error = I18n::text("ha_pairing_invalid");
  }
  renderNow();
}

void DJConnectApp::startSelectedQueueItem() {
  if (!queue_.available || queue_.count == 0 || queueSelection_ >= queue_.count) {
    showNotice(queue_.error.isEmpty() ? I18n::text("queue_empty") : queue_.error, 1800);
    renderNow();
    return;
  }

  const QueueItemState &item = queue_.items[queueSelection_];
  if (item.uri.isEmpty()) {
    showNotice(I18n::text("queue_empty"), 1800);
    renderNow();
    return;
  }

  showNotice(item.title, 1200);
  renderNow();
  String contextUri = playback_.contextUri;
  if (contextUri.isEmpty()) {
    contextUri = queue_.contextUri;
  }
  if (spotify_.playQueueItem(item.uri, contextUri)) {
    refreshPlaybackAndBattery();
    if (activeScreen_ == UiScreen::Queue && playbackProxyReady()) {
      showNotice(I18n::text("loading_queue"), 900);
      renderNow();
      spotify_.refreshQueue(queue_);
      if (queueSelection_ >= queue_.count) {
        queueSelection_ = queue_.count == 0 ? 0 : queue_.count - 1;
      }
      renderNow();
    }
    return;
  }
  showNotice(playback_.error.isEmpty() ? "Queue start failed" : playback_.error, 2200);
  renderNow();
}

void DJConnectApp::openSoundOutputsScreen() {
  activeScreen_ = UiScreen::SoundOutputs;
  menuStackSize_ = 0;
  soundOutputSelection_ = 0;
  showNotice(I18n::text("loading_outputs"), 1200);
  renderNow();
  if (playbackProxyReady()) {
    spotify_.refreshDevices(deviceList_);
  } else {
    deviceList_.available = false;
    deviceList_.error = I18n::text("ha_pairing_invalid");
  }
  renderNow();
}

bool DJConnectApp::playbackProxyReady() const {
  return homeAssistantPaired_ && !haPairingPendingValidation_ && spotify_.isAuthorized();
}

PlaybackConnectionState DJConnectApp::playbackConnectionState() const {
  if (!playbackProxyReady()) {
    return PlaybackConnectionState::Error;
  }
  if (Logic::haPlaybackErrorIsConnectionError(playback_.error.c_str())) {
    return PlaybackConnectionState::Error;
  }
  if (!playback_.hasPlayback) {
    return PlaybackConnectionState::Idle;
  }
  return PlaybackConnectionState::Ok;
}

void DJConnectApp::transferToSelectedOutput() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  if (soundOutputSelection_ == 0) {
    if (spotify_.pausePlayback()) {
      playback_.isPlaying = false;
      showNotice(I18n::text("paused"), 1800);
      lastPlaybackPollAt_ = 0;
    } else {
      showNotice(playback_.error, 3500);
    }
    renderNow();
    return;
  }

  const size_t deviceIndex = soundOutputSelection_ - DJConnectMenuModel::FixedSoundOutputCount;
  if (!deviceList_.available || deviceList_.count == 0 || deviceIndex >= deviceList_.count) {
    showNotice(I18n::text("no_outputs"), 2000);
    renderNow();
    return;
  }

  const SpotifyDeviceState &device = deviceList_.devices[deviceIndex];
  showNotice(String(I18n::text("starting_output")) + " " + device.name, 1800);
  renderNow();
  if (spotify_.transferPlayback(device.id, true)) {
    showNotice(String(I18n::text("playing_on")) + " " + device.name, 2500);
    lastPlaybackPollAt_ = 0;
    spotify_.refreshPlayback();
  } else {
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void DJConnectApp::renderNow() {
  if (lowBatteryGuardActive_ || criticalBatteryGuardActive_) {
    renderLowBatteryGuard();
    return;
  }

  if (djResponseOverlayVisible_) {
    display_.renderDjResponseOverlay(djResponseOverlayTitle_, diagnostics_.lastDjText);
    ledRing_.showDjResponseAnimation();
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }

  if (haPairingScreenActive_) {
    haDevice_.displayPairingCode();
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }

  const bool gameScreen = activeScreen_ == UiScreen::Pong || activeScreen_ == UiScreen::Asteroids || activeScreen_ == UiScreen::Flyer || activeScreen_ == UiScreen::MazeChase;

  // Render from the same snapshot into both visual outputs so screen and ring agree.
  if (isMenuActive()) {
    renderMenuNow();
  } else {
    display_.renderPlaybackScreen(
        playback_,
        battery_,
        notice_,
        displayedVolume(),
        homeAssistantPaired_,
        playbackConnectionState());
  }
  if (activeScreen_ == UiScreen::Pong && display_.backlightPercent() > 0) {
    ledRing_.showPongPaddle(pongPaddleY_);
    ledRing_.setPowerPercent(display_.backlightPercent());
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }
  if (activeScreen_ == UiScreen::Asteroids && display_.backlightPercent() > 0) {
    ledRing_.showGamePosition(asteroidShipX_, 24, 296, CRGB(64, 150, 255));
    ledRing_.setPowerPercent(display_.backlightPercent());
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }
  if (activeScreen_ == UiScreen::Flyer && display_.backlightPercent() > 0) {
    ledRing_.showGamePosition(flyerPlaneY_, 52, 138, CRGB(92, 204, 255));
    ledRing_.setPowerPercent(display_.backlightPercent());
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }
  if (activeScreen_ == UiScreen::MazeChase && display_.backlightPercent() > 0) {
    ledRing_.showGamePosition(mazePlayerX_, 30, 290, CRGB(255, 220, 40));
    ledRing_.setPowerPercent(display_.backlightPercent());
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }
  if (gameScreen || isMenuActive()) {
    ledRing_.clear();
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }
  if (connectionHealthy()) {
    if (playback_.hasPlayback && playback_.supportsVolume && displayedVolume() >= 0) {
      ledRing_.showVolume(displayedVolume());
    } else {
      ledRing_.clear();
    }
  } else {
    ledRing_.showSolid(CRGB::Red, display_.backlightPercent());
  }
  if (playback_.hasPlayback && playback_.supportsVolume && displayedVolume() >= 0) {
    ledRing_.setPowerPercent(display_.backlightPercent());
  }
  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
}

bool DJConnectApp::dismissDjResponseOverlay() {
  if (!djResponseOverlayVisible_) {
    return false;
  }
  djResponseOverlayVisible_ = false;
  display_.resetDjResponseOverlayCache();
  renderNow();
  return true;
}

bool DJConnectApp::chargerConnected() const {
  return power_.chargerConnected(battery_);
}

bool DJConnectApp::shouldReturnToSleepAfterTimerWake() const {
  return power_.shouldReturnToSleepAfterTimerWake(battery_);
}

void DJConnectApp::evaluateBatteryTransition() {
  if (!battery_.available || battery_.percent < 0) {
    return;
  }

  if (lastBatteryPercent_ > 20 && battery_.percent <= 20) {
    sound_.playBatteryWarning();
  }

  const bool chargingNow = chargerConnected();
  if (Logic::shouldPlayChargingCompleteCue(chargingNow, battery_.percent, chargingCompleteSoundPlayed_)) {
    chargingCompleteSoundPlayed_ = true;
    sound_.playChargingComplete();
  } else if (!chargingNow || battery_.percent <= 90) {
    chargingCompleteSoundPlayed_ = false;
  }
  lastBatteryPercent_ = battery_.percent;
}

bool DJConnectApp::updateLowBatteryGuard() {
  if (!battery_.available || battery_.percent < 0) {
    return false;
  }

  const bool charging = chargerConnected();
  const bool critical = battery_.percent < 10;
  const bool low = battery_.percent < 20;
  const bool chargedEnough = battery_.percent >= 21;
  if (chargingBatteryGuardActive_ && chargedEnough) {
    display_.showBootMessage(I18n::text("battery_ok_restart"), battery_);
    responsiveDelay(800);
    ESP.restart();
  }

  if (!low && !lowBatteryGuardActive_ && !criticalBatteryGuardActive_ && !chargingBatteryGuardActive_) {
    return false;
  }

  if (!low) {
    lowBatteryGuardActive_ = false;
    criticalBatteryGuardActive_ = false;
    chargingBatteryGuardActive_ = false;
    lowBatteryGuardStartedAt_ = 0;
    renderNow();
    return false;
  }

  if (!lowBatteryGuardActive_ || criticalBatteryGuardActive_ != critical || chargingBatteryGuardActive_ != charging) {
    lowBatteryGuardActive_ = true;
    criticalBatteryGuardActive_ = critical;
    chargingBatteryGuardActive_ = charging && !critical;
    lowBatteryGuardStartedAt_ = millis();
    notice_.clear();
    djResponseOverlayVisible_ = false;
    display_.resetDjResponseOverlayCache();
    if (voiceState_ != VoiceState::Listening && voiceState_ != VoiceState::SendingCommand) {
      voiceState_ = VoiceState::Idle;
    }
    input_.clearPendingButtonActions();
    renderLowBatteryGuard();
  }

  if (millis() - lastBatteryPollAt_ > Config::BatteryPollIntervalMs) {
    lastBatteryPollAt_ = millis();
    batteryMonitor_.refresh();
    evaluateBatteryTransition();
    renderLowBatteryGuard();
  }

  if (chargingBatteryGuardActive_) {
    ledRing_.showChargingAnimation();
    visualState_.ledOn = ledRing_.isOn();
    return true;
  }

  const uint32_t sleepAfterMs = criticalBatteryGuardActive_ ? 30000UL : 120000UL;
  if (!deepSleepStarted_ && millis() - lowBatteryGuardStartedAt_ >= sleepAfterMs) {
    lowBatteryTimerWake_ = true;
    enterDeepSleep();
  }
  return true;
}

void DJConnectApp::renderLowBatteryGuard() {
  const uint8_t brightness = criticalBatteryGuardActive_ ? 25 : 50;
  const String message = chargingBatteryGuardActive_ ? I18n::text("charging") : I18n::text("boot_please_charge");
  display_.forceBacklightPercent(brightness);
  display_.renderLowBatteryScreen(battery_, message);

  if (criticalBatteryGuardActive_) {
    ledRing_.setPowerPercent(0);
  } else if (chargingBatteryGuardActive_) {
    ledRing_.showChargingAnimation();
  } else {
    ledRing_.showSolid(CRGB::Red, 100);
  }

  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
}

bool DJConnectApp::connectionHealthy() {
  const bool homeAssistantOk = homeAssistantPaired_;
  const bool playbackOkOrIdle = playbackConnectionState() != PlaybackConnectionState::Error;
  return homeAssistantOk && playbackOkOrIdle;
}

void DJConnectApp::wakeDisplayWithSplash() {
  if (lowBatteryGuardActive_ || criticalBatteryGuardActive_ || chargingBatteryGuardActive_) {
    display_.wakeForUserActivity();
    renderLowBatteryGuard();
    return;
  }

  if (haPairingScreenActive_) {
    display_.wakeForUserActivity();
    haDevice_.displayPairingCode();
    return;
  }

  display_.showSplashScreen(battery_);
  responsiveDelay(Config::WakeSplashDurationMs);
  renderNow();
}

AboutStatus DJConnectApp::aboutStatus() {
  AboutStatus status;
  status.wifiConnected = WiFi.status() == WL_CONNECTED;
  status.ipAddress = status.wifiConnected ? WiFi.localIP().toString() : "";
  status.webAddress = status.wifiConnected ? "http://" + WiFi.localIP().toString() : "";
  status.haPaired = homeAssistantPaired_;
  status.spotifyConnected = playbackConnectionState() == PlaybackConnectionState::Ok;
  return status;
}

void DJConnectApp::updateVisualPower() {
  if (haPairingScreenActive_) {
    display_.forceBacklightPercent(100);
    const uint8_t currentBacklightPercent = display_.backlightPercent();
    ledRing_.setPowerPercent(currentBacklightPercent);
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = currentBacklightPercent;
    visualState_.ledOn = ledRing_.isOn();
    return;
  }

  display_.updateIdleBrightness();
  const uint8_t currentBacklightPercent = display_.backlightPercent();
  ledRing_.setPowerPercent(currentBacklightPercent);
  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = currentBacklightPercent;
  visualState_.ledOn = ledRing_.isOn();
}

void DJConnectApp::configureWatchdog() {
  power_.configureWatchdog();
}

void DJConnectApp::serviceWatchdog() {
  power_.serviceWatchdog();
}

void DJConnectApp::responsiveDelay(uint32_t durationMs) {
  if (durationMs == 0) {
    serviceWatchdog();
    yield();
    return;
  }

  const uint32_t startedAt = millis();
  while (millis() - startedAt < durationMs) {
    const uint32_t elapsedMs = millis() - startedAt;
    const uint32_t remainingMs = durationMs > elapsedMs ? durationMs - elapsedMs : 0;
    serviceWatchdog();
    delay(min<uint32_t>(remainingMs, 20));
  }
}

void DJConnectApp::logHeapIfDue() {
  const uint32_t now = millis();
  if (lastHeapLogAt_ != 0 && now - lastHeapLogAt_ < Config::HeapLogIntervalMs) {
    return;
  }
  lastHeapLogAt_ = now;

  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minFreeHeap = ESP.getMinFreeHeap();
  const uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (heapTrendBaselineMinFree_ == 0 || heapTrendBaselineLargestBlock_ == 0) {
    heapTrendBaselineMinFree_ = minFreeHeap;
    heapTrendBaselineLargestBlock_ = largestBlock;
    heapTrendPreviousMinFree_ = minFreeHeap;
    heapTrendPreviousLargestBlock_ = largestBlock;
  }

  const int32_t minFreeDelta = static_cast<int32_t>(minFreeHeap) - static_cast<int32_t>(heapTrendPreviousMinFree_);
  const int32_t largestBlockDelta = static_cast<int32_t>(largestBlock) - static_cast<int32_t>(heapTrendPreviousLargestBlock_);
  const int32_t minFreeSinceBoot = static_cast<int32_t>(minFreeHeap) - static_cast<int32_t>(heapTrendBaselineMinFree_);
  const int32_t largestBlockSinceBoot = static_cast<int32_t>(largestBlock) - static_cast<int32_t>(heapTrendBaselineLargestBlock_);

  AppLog.print("Memory: free_heap=");
  AppLog.print(freeHeap);
  AppLog.print(" min_free_heap=");
  AppLog.print(minFreeHeap);
  AppLog.print(" min_delta=");
  AppLog.print(minFreeDelta);
  AppLog.print(" min_since_baseline=");
  AppLog.print(minFreeSinceBoot);
  AppLog.print(" largest_block=");
  AppLog.print(largestBlock);
  AppLog.print(" largest_delta=");
  AppLog.print(largestBlockDelta);
  AppLog.print(" largest_since_baseline=");
  AppLog.println(largestBlockSinceBoot);

  if (minFreeDelta < 0 || largestBlockDelta < 0) {
    AppLog.print("Memory trend warning: ");
    if (minFreeDelta < 0) {
      AppLog.print("min_free_heap down ");
      AppLog.print(-minFreeDelta);
      AppLog.print("B ");
    }
    if (largestBlockDelta < 0) {
      AppLog.print("largest_block down ");
      AppLog.print(-largestBlockDelta);
      AppLog.print("B");
    }
    AppLog.println();
  }

  heapTrendPreviousMinFree_ = minFreeHeap;
  heapTrendPreviousLargestBlock_ = largestBlock;
}

void DJConnectApp::enterDeepSleep(const char *reason) {
  deepSleepStarted_ = true;
  AppLog.print("Entering deep sleep");
  if (reason != nullptr && strlen(reason) > 0) {
    AppLog.print(": ");
    AppLog.print(reason);
  }
  AppLog.print(" uptime_ms=");
  AppLog.print(millis());
  AppLog.print(" idle_ms=");
  AppLog.print(display_.idleMs());
  AppLog.print(" timeout_ms=");
  AppLog.println(deviceSleepTimeoutMs_);
  sound_.playTurnOff();
  responsiveDelay(220);
  ledRing_.playTurnOffRainbow();

  if (activeScreen_ != UiScreen::NowPlaying) {
    activeScreen_ = UiScreen::NowPlaying;
    menuStackSize_ = 0;
  }

  ledRing_.setPowerPercent(0);
  responsiveDelay(50);

  esp_sleep_enable_ext1_wakeup(power_.buttonWakeMask(), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(power_.sleepTimerWakeUs(lowBatteryTimerWake_));
  WiFi.disconnect(true, false);
  responsiveDelay(100);
  esp_deep_sleep_start();
}

void DJConnectApp::enterDeepSleepWithoutDisplay() {
  deepSleepStarted_ = true;
  sound_.playTurnOff();
  responsiveDelay(220);
  esp_sleep_enable_ext1_wakeup(power_.buttonWakeMask(), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(power_.sleepTimerWakeUs(false));
  WiFi.disconnect(true, false);
  responsiveDelay(50);
  esp_deep_sleep_start();
}

void DJConnectApp::recordLoopMetrics(uint32_t loopStartedAt) {
  const uint32_t now = millis();
  const uint32_t busyMs = now - loopStartedAt;

  diagnostics_.uptimeMs = now;
  diagnostics_.loopCount++;
  diagnostics_.lastLoopDurationMs = busyMs;
  if (busyMs > diagnostics_.maxLoopDurationMs) {
    diagnostics_.maxLoopDurationMs = busyMs;
  }
  if (busyMs >= Config::SlowLoopWarningMs &&
      (lastSlowLoopLogAt_ == 0 || now - lastSlowLoopLogAt_ >= 10000UL)) {
    lastSlowLoopLogAt_ = now;
    AppLog.print("Responsiveness: slow loop ");
    AppLog.print(busyMs);
    AppLog.println(" ms");
  }

  if (loopMetricsWindowStartedAt_ == 0) {
    loopMetricsWindowStartedAt_ = now;
  }
  loopMetricsBusyMs_ += busyMs;

  const uint32_t windowMs = now - loopMetricsWindowStartedAt_;
  if (windowMs >= 1000) {
    diagnostics_.cpuUsagePercent = constrain((loopMetricsBusyMs_ * 100UL) / windowMs, 0UL, 100UL);
    loopMetricsWindowStartedAt_ = now;
    loopMetricsBusyMs_ = 0;
  }
}

void DJConnectApp::applyWebSettings(
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    bool wakeWordEnabled) {
  screenBrightnessPercent_ = constrain(brightnessPercent, 25, 100);
  screenOffTimeoutMs_ = constrain(offTimeoutMs, 30000UL, 240000UL);
  deviceSleepTimeoutMs_ = constrain(sleepTimeoutMs, 300000UL, 3600000UL);
  speakerVolumePercent_ = constrain(speakerVolumePercent, 25, 100);
  language_ = I18n::languageFromCode(languageCode);
  I18n::setLanguage(language_);
  languageCode_ = I18n::languageCode();
  languageSelection_ = language_ == Language::Dutch ? 1 : 0;
  themeCode_ = themeCode;
  themeCode_.toLowerCase();
  if (themeCode_ != "auto" && themeCode_ != "light") {
    themeCode_ = "dark";
  }
  logLevel_ = logLevel;
  logLevel_.toLowerCase();
  if (logLevel_ != "debug" && logLevel_ != "warning" && logLevel_ != "error") {
    logLevel_ = "info";
  }
  wakeWordEnabled_ = wakeWordEnabled;
  wakeWord_.setEnabled(wakeWordEnabled_);
  if (!wakeWordEnabled_) {
    wakeWord_.releaseResources();
  }
  for (size_t index = 0; index < ThemeOptionCount; index++) {
    if (themeValue(index) == themeCode_) {
      themeSelection_ = index;
      break;
    }
  }
  for (size_t index = 0; index < LogLevelOptionCount; index++) {
    if (logLevelValue(index) == logLevel_) {
      logLevelSelection_ = index;
      break;
    }
  }
  sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
  for (size_t index = 0; index < SpeakerVolumeOptionCount; index++) {
    if (speakerVolumeValuePercent(index) == speakerVolumePercent_) {
      speakerVolumeSelection_ = index;
      break;
    }
  }
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  if (haPairingScreenActive_) {
    display_.forceBacklightPercent(100);
  }
  applyTheme();
  if (haPairingScreenActive_) {
    display_.forceBacklightPercent(100);
  }
  AppLog.setLevel(logLevel_);
  sound_.setVolumePercent(speakerVolumePercent_);
  saveDisplaySettings();
  showNotice(I18n::text("web_settings_saved"), 1800);
  renderNow();
  sendHomeAssistantStatusIfDue(true);
}

void DJConnectApp::applyProvisionedLanguage(const String &languageCode) {
  const String normalized = DJConnectDevice::normalizedLanguageCode(languageCode);
  if (normalized.isEmpty()) {
    return;
  }
  language_ = I18n::languageFromCode(normalized);
  I18n::setLanguage(language_);
  languageCode_ = I18n::languageCode();
  languageSelection_ = language_ == Language::Dutch ? 1 : 0;
  AppLog.print("UI language applied: ");
  AppLog.println(languageCode_);
  renderNow();
}

bool DJConnectApp::checkBootstrapFirmwareUpdate() {
  const char *currentVersion = Logic::otaComparableFirmwareVersion(Config::AppVersionNumber, Config::AppVersion);
  if (strcmp(currentVersion, "0.0.0") == 0) {
    AppLog.println("Bootstrap OTA skipped: dev firmware");
    return false;
  }

  display_.forceBacklightPercent(100);
  display_.showBootMessage(I18n::text("boot_checking_firmware"), battery_);
  ledRing_.showFirmwareUpdateAnimation();

  WiFiClientSecure secureClient;
  secureClient.setCACert(GitHubApiCa);
  secureClient.setHandshakeTimeout(Config::TlsHandshakeTimeoutMs);
  secureClient.setTimeout(Config::OtaIoTimeoutMs);

  HTTPClient releaseHttp;
  NetworkActivity releaseActivity("bootstrap_release_check", Config::OtaIoTimeoutMs);
  NetworkActivity::configureHttp(releaseHttp, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
  if (!releaseHttp.begin(secureClient, Config::BootstrapFirmwareReleaseApiUrl)) {
    AppLog.println("Bootstrap OTA release API begin failed");
    releaseActivity.finishError("begin failed");
    return false;
  }
  releaseHttp.addHeader("User-Agent", "DJConnect");
  const int releaseCode = releaseHttp.GET();
  if (releaseCode != HTTP_CODE_OK) {
    AppLog.print("Bootstrap OTA release API HTTP ");
    AppLog.println(releaseCode);
    releaseHttp.end();
    releaseActivity.finish(releaseCode);
    return false;
  }

  JsonDocument releaseDoc;
  DeserializationError jsonError = deserializeJson(releaseDoc, releaseHttp.getStream());
  releaseHttp.end();
  if (jsonError) {
    AppLog.print("Bootstrap OTA release JSON failed: ");
    AppLog.println(jsonError.c_str());
    releaseActivity.finishError("json failed");
    return false;
  }
  releaseActivity.finish(releaseCode);

  String manifestUrl;
  for (JsonVariantConst asset : releaseDoc["assets"].as<JsonArrayConst>()) {
    const String name = asset["name"] | "";
    if (name == Config::BootstrapFirmwareManifestAsset) {
      manifestUrl = asset["browser_download_url"] | "";
      break;
    }
  }
  if (manifestUrl.isEmpty()) {
    AppLog.println("Bootstrap OTA manifest asset missing");
    return false;
  }

  HTTPClient manifestHttp;
  NetworkActivity manifestActivity("bootstrap_manifest", Config::OtaIoTimeoutMs);
  NetworkActivity::configureHttp(manifestHttp, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
  manifestHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  manifestHttp.setRedirectLimit(5);
  if (!manifestHttp.begin(secureClient, manifestUrl)) {
    AppLog.println("Bootstrap OTA manifest begin failed");
    manifestActivity.finishError("begin failed");
    return false;
  }
  manifestHttp.addHeader("User-Agent", "DJConnect");
  const int manifestCode = manifestHttp.GET();
  if (manifestCode != HTTP_CODE_OK) {
    AppLog.print("Bootstrap OTA manifest HTTP ");
    AppLog.println(manifestCode);
    manifestHttp.end();
    manifestActivity.finish(manifestCode);
    return false;
  }

  JsonDocument manifestDoc;
  jsonError = deserializeJson(manifestDoc, manifestHttp.getStream());
  manifestHttp.end();
  if (jsonError) {
    AppLog.print("Bootstrap OTA manifest JSON failed: ");
    AppLog.println(jsonError.c_str());
    manifestActivity.finishError("json failed");
    return false;
  }
  manifestActivity.finish(manifestCode);

  DJConnectOTARequest request;
  request.version = manifestDoc["version"] | "";
  request.device = manifestDoc["device"] | "";
  request.url = manifestDoc["url"] | "";
  request.sha256 = manifestDoc["sha256"] | "";
  if (request.version.isEmpty() || request.device.isEmpty() || request.url.isEmpty() || request.sha256.isEmpty()) {
    AppLog.println("Bootstrap OTA manifest incomplete");
    return false;
  }

  AppLog.print("Bootstrap OTA latest=");
  AppLog.print(request.version);
  AppLog.print(" current=");
  AppLog.println(currentVersion);
  if (Logic::compareSemver(request.version.c_str(), currentVersion) <= 0) {
    AppLog.println("Bootstrap OTA skipped: firmware current");
    return false;
  }

  String message;
  AppLog.println("Bootstrap OTA starting pre-pairing update");
  prepareForOta();
  if (haOta_.performUpdate(request, &battery_, &display_, &ledRing_, &sound_, message)) {
    responsiveDelay(500);
    ESP.restart();
    return true;
  }
  AppLog.print("Bootstrap OTA failed: ");
  AppLog.println(message);
  display_.showBootMessage(I18n::text("boot_booting"), battery_);
  return false;
}

void DJConnectApp::requestWebWifiSettings(const String &ssid, const String &password) {
  pendingWifiSsid_ = ssid;
  pendingWifiSsid_.trim();
  pendingWifiPassword_ = password;
  pendingWifiPasswordProvided_ = !password.isEmpty();
  pendingWifiSettingsRequestedAt_ = millis();
  pendingWifiSettings_ = !pendingWifiSsid_.isEmpty();
  showNotice(I18n::text("testing_wifi"), 2500);
  renderNow();
}

void DJConnectApp::processPendingWifiSettings() {
  if (!pendingWifiSettings_) {
    return;
  }
  if (millis() - pendingWifiSettingsRequestedAt_ < 1500) {
    return;
  }

  pendingWifiSettings_ = false;
  const String oldSsid = wifiSsid_;
  const String oldPassword = wifiPassword_;

  String targetPassword = pendingWifiPasswordProvided_ ? pendingWifiPassword_ : oldPassword;

  AppLog.print("Testing web WiFi credentials, SSID chars=");
  AppLog.println(pendingWifiSsid_.length());
  display_.wakeForUserActivity();
  display_.showBootMessage(I18n::text("boot_testing_wifi"), battery_);
  ledRing_.showWifiTestingAnimation();

  WiFi.disconnect(false, false);
  responsiveDelay(250);
  WiFi.mode(WIFI_STA);
  WiFi.begin(pendingWifiSsid_.c_str(), targetPassword.c_str());

  const uint32_t startedAt = millis();
  uint32_t lastProgressDotAt = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    const uint32_t now = millis();
    ledRing_.showWifiTestingAnimation();
    responsiveDelay(50);
    if (lastProgressDotAt == 0 || now - lastProgressDotAt >= 250) {
      lastProgressDotAt = now;
      AppLog.print(".");
    }
  }
  AppLog.println();

  if (WiFi.status() == WL_CONNECTED) {
    AppLog.print("Web WiFi test OK, IP: ");
    AppLog.println(WiFi.localIP());
    provisioning_.saveWifiCredentials(pendingWifiSsid_, targetPassword);
    display_.showBootMessage(I18n::text("wifi_ok_restart"), battery_);
    responsiveDelay(900);
    ESP.restart();
    return;
  }

  AppLog.println("Web WiFi test failed, restoring previous connection");
  playback_.error = I18n::text("wifi_test_failed");
  showNotice(I18n::text("wifi_test_failed"), 5000);
  WiFi.disconnect(false, false);
  responsiveDelay(250);
  if (!oldSsid.isEmpty()) {
    WiFi.begin(oldSsid.c_str(), oldPassword.c_str());
  }
  renderNow();
}

void DJConnectApp::setupHomeAssistantLayer() {
  if (WiFi.status() != WL_CONNECTED || !webPortal_.isRunning()) {
    return;
  }

  const bool apiWasRunning = haApiServer_.isRunning();
  haDiscovery_.begin(haDevice_);
  haApiServer_.begin(
      webPortal_.server(),
      haDevice_,
      haPairing_,
      haDiscovery_,
      haOta_,
      spotify_,
      display_,
      ledRing_,
      sound_,
      battery_,
      diagnostics_,
      this,
      djResponseCallback,
      languageProvisionedCallback,
      deviceCommandCallback,
      directPairCallback,
      otaPrepareCallback,
      debugScreenCallback);

  if (!apiWasRunning) {
    AppLog.line(String("Home Assistant: paired=") + (haDevice_.isPaired() ? "true" : "false") +
                (haDevice_.isPaired() ? ", URL configured" : ", showing pairing code"));
    if (haDevice_.isPaired()) {
      AppLog.line(String("Home Assistant local URL: ") +
                  (haDevice_.getHaLocalUrl().isEmpty() ? String("(empty)") : haDevice_.getHaLocalUrl()));
    }
  }
  if (!haDevice_.isPaired()) {
    haDevice_.displayPairingCode();
  }
}

void DJConnectApp::sendHomeAssistantStatusIfDue(bool force) {
  if (!haDevice_.isPaired() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!force &&
      (webVoiceTextPending_ ||
       webVoiceTextOnlyActive_ ||
       djResponseOverlayVisible_ ||
       voiceState_ == VoiceState::SendingCommand ||
       voiceState_ == VoiceState::WaitingForResult)) {
    lastHaStatusAt_ = millis();
    return;
  }
  const uint32_t now = millis();
  if (!force && now - lastHaStatusAt_ < Config::HaStatusIntervalMs) {
    return;
  }
  lastHaStatusAt_ = now;
  const bool playbackProxyUsable =
      Logic::playbackProxyUsableForHomeAssistantStatus(haDevice_.isPlaybackConfigured(), spotify_.needsCredentialRefresh());
  if (haDevice_.isPlaybackConfigured() && !playbackProxyUsable) {
    AppLog.println("Playback proxy marked stale; requesting HA status refresh");
  }
  DeviceSettingsStatus settings;
  settings.screenBrightnessPercent = screenBrightnessPercent_;
  settings.screenOffTimeoutMs = screenOffTimeoutMs_;
  settings.turnOffAfterMs = deviceSleepTimeoutMs_;
  settings.speakerVolumePercent = speakerVolumePercent_;
  settings.language = languageCode_;
  settings.theme = themeCode_;
  settings.logLevel = logLevel_;
  settings.wakeWordEnabled = wakeWordEnabled_;
  const String soundOutput = playback_.deviceName.isEmpty() ? "" : playback_.deviceName;
  const DJConnectPairing::StatusResult result =
      haPairing_.sendStatusToHA(battery_, playbackProxyUsable, settings, visualState_, soundOutput);
  if (result == DJConnectPairing::StatusResult::Ok) {
    const bool wasPendingValidation = haPairingPendingValidation_;
    homeAssistantPaired_ = true;
    haPairingPendingValidation_ = false;
    if (wasPendingValidation || playbackRefreshAfterPairing_) {
      playbackRefreshAfterPairing_ = false;
      lastPlaybackPollAt_ = millis();
      playbackPollPausedUntil_ = millis() + Config::PlaybackBootGraceMs;
      AppLog.println("Home Assistant: pairing status confirmed, enabling playback proxy");
    }
  } else if (result == DJConnectPairing::StatusResult::PairingInvalid) {
    if (haPairingPendingValidation_) {
      AppLog.println("Home Assistant: pending pairing rejected by status endpoint");
      haDevice_.clearHomeAssistantPairing();
      homeAssistantPaired_ = false;
      haPairingPendingValidation_ = false;
      haPairingScreenActive_ = true;
      lastHaStatusAt_ = 0;
      haDevice_.displayPairingCode();
      return;
    }
    markHomeAssistantPairingInvalid(I18n::text("ha_pairing_invalid"));
  } else if (result == DJConnectPairing::StatusResult::VersionMismatch) {
    playback_.error = "Update DJConnect firmware/integration";
    showNotice(playback_.error, 5000);
    renderNow();
  }
}

void DJConnectApp::markHomeAssistantPairingInvalid(const String &message) {
  if (homeAssistantPaired_) {
    AppLog.println("Home Assistant: pairing invalid or stale");
  }
  homeAssistantPaired_ = false;
  haPairingPendingValidation_ = false;
  playbackRefreshAfterPairing_ = false;
  showNotice(message, 5000);
  renderNow();
}

bool DJConnectApp::handleHomeAssistantPairingMode(uint32_t loopStartedAt) {
  if (haDevice_.isPaired()) {
    if (haPairingScreenActive_) {
      homeAssistantPaired_ = true;
      haPairingPendingValidation_ = true;
      playbackRefreshAfterPairing_ = true;
      haPairingScreenActive_ = false;
      haPairingStartedAt_ = 0;
      lastHaStatusAt_ = Logic::forceImmediatePollTimestamp();
      if (bleProvisioning_.isStarted()) {
        bleProvisioning_.end();
      }
      haDevice_.displayPaired();
      responsiveDelay(700);
      if (!helpShown_) {
        helpShown_ = true;
        provisioning_.markHelpShown();
      }
      AppLog.println("Home Assistant pairing complete, restarting for clean runtime heap");
      display_.showBootMessage(I18n::text("restarting"), battery_);
      responsiveDelay(500);
      ESP.restart();
      recordLoopMetrics(loopStartedAt);
      return true;
    }
    return false;
  }

  haPairingScreenActive_ = true;
  homeAssistantPaired_ = false;
  haDevice_.ensurePairingCode();
  if (!bleProvisioning_.isStarted()) {
    bleProvisioning_.begin(haDevice_.getDeviceId());
    bleProvisioning_.setStatus("pairing", String("Pair code ") + haDevice_.getPairCode());
  }
  if (haPairingStartedAt_ == 0) {
    haPairingStartedAt_ = millis();
  }
  display_.forceBacklightPercent(100);
  ledRing_.showHomeAssistantPairingBreath();
  const InputEvents events = input_.poll();
  if (events.topButtonPress || events.topButtonHeld || events.topButtonLongClick) {
    topHoldMenuHintVisible_ = false;
    input_.clearPendingButtonActions();
  }
  if (events.encoderClick) {
    AppLog.println("Home Assistant pairing: turn off selected");
    display_.showBootMessage(I18n::text("turning_off"), battery_);
    responsiveDelay(300);
    enterDeepSleep();
    return true;
  }
  webPortal_.handle();
  processPendingWifiSettings();
  haApiServer_.loop();

  const uint32_t now = millis();
  if (now - lastBatteryPollAt_ >= Config::BatteryPollIntervalMs) {
    lastBatteryPollAt_ = now;
    batteryMonitor_.refresh();
    evaluateBatteryTransition();
  }
  if (lastHaPairingScreenAt_ == 0 || now - lastHaPairingScreenAt_ >= 5000) {
    lastHaPairingScreenAt_ = now;
    haDevice_.displayPairingCode();
  }

  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
  if (!deepSleepStarted_ && now - haPairingStartedAt_ >= Config::PairingModeTimeoutMs) {
    display_.showBootMessage(I18n::text("boot_pair_timeout_sleeping"), battery_);
    responsiveDelay(600);
    enterDeepSleep("pairing timeout");
  }
  recordLoopMetrics(loopStartedAt);
  responsiveDelay(10);
  return true;
}

void DJConnectApp::applyWebSettingsCallback(
    void *context,
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    bool wakeWordEnabled) {
  static_cast<DJConnectApp *>(context)->applyWebSettings(
      brightnessPercent,
      offTimeoutMs,
      sleepTimeoutMs,
      speakerVolumePercent,
      languageCode,
      themeCode,
      logLevel,
      wakeWordEnabled);
}

void DJConnectApp::applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password) {
  static_cast<DJConnectApp *>(context)->requestWebWifiSettings(ssid, password);
}

bool DJConnectApp::sendWebVoiceTextCallback(void *context, const String &text, String &message, String &audioUrl) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  DJConnectApp *app = static_cast<DJConnectApp *>(context);
  AppLog.print("Web voice: queued text chars=");
  AppLog.println(text.length());
  if (app->webVoiceTextPending_ || app->voiceRecording_ || app->voiceState_ != VoiceState::Idle) {
    message = I18n::text("voice_processing");
    audioUrl = "";
    return false;
  }
  app->pendingWebVoiceText_ = text;
  app->webVoiceTextPending_ = true;
  app->pendingWebVoiceTextProcessAfter_ = millis() + 750;
  message = "DJ announcement test queued";
  audioUrl = "";
  return true;
}

void DJConnectApp::wakeWordDetectedCallback(void *context) {
  if (context == nullptr) {
    return;
  }
  DJConnectApp *app = static_cast<DJConnectApp *>(context);
  if (app->voiceRecording_ ||
      app->voiceState_ != VoiceState::Idle ||
      app->djResponseOverlayVisible_ ||
      !app->homeAssistantPaired_) {
    return;
  }
  AppLog.println("Wake word: starting push-to-talk flow");
  app->display_.wakeForUserActivity();
  app->nextVoiceStartFromWakeWord_ = true;
  app->handleVoiceButton();
}

void DJConnectApp::voiceActivityCallback(void *context) {
  auto *app = static_cast<DJConnectApp *>(context);
  if (app == nullptr) {
    return;
  }
  app->ledRing_.showDjResponseAnimation();
  const InputEvents events = app->input_.poll();
  if (events.encoderPress || events.encoderClick) {
    app->input_.clearPendingButtonActions();
    app->cancelVoiceFlow("encoder");
  }
}

void DJConnectApp::softResetCueCallback(void *context) {
  auto *app = static_cast<DJConnectApp *>(context);
  if (app == nullptr) {
    return;
  }

  app->sound_.playSoftReset();
  app->ledRing_.showSolid(CRGB::White, 100);
  delay(180);
  app->ledRing_.setPowerPercent(0);
  delay(80);
  app->ledRing_.showSolid(CRGB::White, 100);
  delay(180);
  app->ledRing_.setPowerPercent(0);
  delay(180);
}

void DJConnectApp::refreshFromWebCallback(void *context) {
  static_cast<DJConnectApp *>(context)->refreshPlaybackAndBattery();
}

void DJConnectApp::resetPairingFromWebCallback(void *context) {
  static_cast<DJConnectApp *>(context)->resetHomeAssistantPairing();
}

void DJConnectApp::hardResetFromWebCallback(void *context) {
  static_cast<DJConnectApp *>(context)->hardResetToProvisioning();
}

bool DJConnectApp::djResponseCallback(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType) {
  DJConnectApp *app = static_cast<DJConnectApp *>(context);
  if (app->webVoiceTextOnlyActive_ &&
      app->webVoiceTextOnlyConsumeNext_ &&
      static_cast<int32_t>(millis() - app->webVoiceTextOnlyUntil_) < 0) {
    spoken = false;
    app->webVoiceTextOnlyActive_ = false;
    app->webVoiceTextOnlyConsumeNext_ = false;
    app->showDjResponseOverlay(I18n::text("voice_dj_response"), text, 6000);
    app->lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "skipped";
    audioType = app->lastDjAudioType_;
    if (!audioUrl.isEmpty()) {
      AppLog.println("Web voice: DJ response audio skipped");
    }
    AppLog.print("DJ response displayed chars=");
    AppLog.println(text.length());
    return !text.isEmpty();
  }
  app->webVoiceTextOnlyActive_ = false;
  app->webVoiceTextOnlyConsumeNext_ = false;
  const bool displayed = app->handleDjResponseText(text, audioUrl, spoken);
  audioType = audioUrl.isEmpty() ? "none" : app->lastDjAudioType_;
  return displayed;
}

void DJConnectApp::otaPrepareCallback(void *context) {
  if (context == nullptr) {
    return;
  }
  static_cast<DJConnectApp *>(context)->prepareForOta();
}

void DJConnectApp::languageProvisionedCallback(void *context, const String &languageCode) {
  if (context != nullptr) {
    static_cast<DJConnectApp *>(context)->applyProvisionedLanguage(languageCode);
  }
}

bool DJConnectApp::deviceCommandCallback(void *context, const DeviceCommand &command, String &message) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  return static_cast<DJConnectApp *>(context)->handleDeviceCommand(command, message);
}

void DJConnectApp::directPairCallback(void *context) {
  if (context == nullptr) {
    return;
  }
  static_cast<DJConnectApp *>(context)->noteDirectPairingReceived();
}

bool DJConnectApp::debugScreenCallback(void *context, const String &screenName, String &message) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  return static_cast<DJConnectApp *>(context)->openDebugScreen(screenName, message);
}

bool DJConnectApp::openDebugScreen(const String &screenName, String &message) {
  String normalized = screenName;
  normalized.trim();
  normalized.toLowerCase();
  normalized.replace("_", "-");
  normalized.replace(" ", "-");

  UiScreen target = UiScreen::NowPlaying;
  if (normalized == "now-playing" || normalized == "now" || normalized == "playback") {
    target = UiScreen::NowPlaying;
  } else if (normalized == "current-song" || normalized == "album-art") {
    target = UiScreen::AlbumArt;
  } else if (normalized == "queue" || normalized == "up-next") {
    target = UiScreen::Queue;
  } else if (normalized == "playlists") {
    target = UiScreen::Playlists;
  } else if (normalized == "outputs" || normalized == "sound-outputs") {
    target = UiScreen::SoundOutputs;
  } else if (normalized == "logs") {
    target = UiScreen::Logs;
  } else if (normalized == "games") {
    target = UiScreen::Games;
  } else if (normalized == "help") {
    target = UiScreen::Help;
  } else if (normalized == "paddle-rally" || normalized == "pong") {
    resetPong();
    target = UiScreen::Pong;
  } else if (normalized == "meteor-run" || normalized == "asteroids") {
    resetAsteroids();
    target = UiScreen::Asteroids;
  } else if (normalized == "sky-dash" || normalized == "flyer" || normalized == "fly") {
    resetFlyer();
    target = UiScreen::Flyer;
  } else if (normalized == "maze-chase" || normalized == "pacman") {
    resetMazeChase();
    target = UiScreen::MazeChase;
  } else if (normalized == "menu" || normalized == "root-menu") {
    target = UiScreen::RootMenu;
  } else if (normalized == "about") {
    target = UiScreen::About;
  } else if (normalized == "settings") {
    target = UiScreen::Settings;
  } else if (normalized == "dim-timeout") {
    target = UiScreen::DimTimeout;
  } else if (normalized == "brightness") {
    target = UiScreen::Brightness;
  } else if (normalized == "language") {
    target = UiScreen::Language;
  } else if (normalized == "theme") {
    target = UiScreen::Theme;
  } else if (normalized == "log-level") {
    target = UiScreen::LogLevel;
  } else if (normalized == "speaker-volume") {
    target = UiScreen::SpeakerVolume;
  } else if (normalized == "shuffle") {
    target = UiScreen::ShuffleMode;
  } else if (normalized == "repeat") {
    target = UiScreen::RepeatMode;
  } else if (normalized == "sleep-timeout" || normalized == "turn-off-after") {
    target = UiScreen::SleepTimeout;
  } else if (normalized == "change-wifi") {
    target = UiScreen::ChangeWifiConfirm;
  } else if (normalized == "reset-pairing") {
    target = UiScreen::ResetPairingConfirm;
  } else if (normalized == "factory-reset") {
    target = UiScreen::HardResetConfirm;
  } else {
    message = "Unknown screen";
    return false;
  }

  menuStackSize_ = 0;
  activeScreen_ = target;
  notice_.clear();
  display_.wakeForUserActivity();
  renderNow();
  message = "Screen opened";
  AppLog.line("Debug screen opened: " + normalized);
  return true;
}

void DJConnectApp::noteDirectPairingReceived() {
  playback_.error = "";
  spotify_.reloadCredentials();
  if (haPairingScreenActive_) {
    AppLog.println("Home Assistant direct pairing received, leaving pairing mode");
    lastHaStatusAt_ = Logic::forceImmediatePollTimestamp();
    return;
  }
  homeAssistantPaired_ = true;
  haPairingPendingValidation_ = true;
  playbackRefreshAfterPairing_ = true;
  lastHaStatusAt_ = Logic::forceImmediatePollTimestamp();
  lastPlaybackPollAt_ = millis();
  showNotice(I18n::text("boot_paired"), 1500);
  renderNow();
}

bool DJConnectApp::handleDjResponseText(const String &text, const String &audioUrl, bool &spoken) {
  if (text.isEmpty()) {
    return false;
  }
  spoken = false;
  showDjResponseOverlay(I18n::text("voice_dj_response"), text, 6000);
  lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "unknown";
  AppLog.print("DJ response displayed chars=");
  AppLog.println(text.length());
  if (!audioUrl.isEmpty()) {
    String resolvedAudioUrl = audioUrl;
    const String localHaUrl = haDevice_.getHaLocalUrl();
    if (!localHaUrl.isEmpty()) {
      if (resolvedAudioUrl.startsWith("/")) {
        resolvedAudioUrl = localHaUrl + resolvedAudioUrl;
      } else {
        const int pathStart = resolvedAudioUrl.indexOf("/api/");
        if ((resolvedAudioUrl.indexOf(".ui.nabu.casa") >= 0 ||
             resolvedAudioUrl.indexOf("://homeassistant.local") >= 0) &&
            pathStart > 0) {
          resolvedAudioUrl = localHaUrl + resolvedAudioUrl.substring(pathStart);
        }
      }
    }
    input_.clearPendingButtonActions();
    const DjResponseAudioResult audioResult = djAudio_.play(resolvedAudioUrl);
    input_.clearPendingButtonActions();
    if (voiceCancelRequested_) {
      voiceCancelRequested_ = false;
      spoken = false;
      voiceState_ = VoiceState::Idle;
      renderNow();
      return true;
    }
    spoken = audioResult.spoken;
    lastDjAudioType_ = audioResult.audioType;
    if (djResponseOverlayVisible_) {
      djResponseOverlayUntil_ = millis() + 6000;
    }
  }
  if (!spoken && volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
  return true;
}

void DJConnectApp::showDjResponseOverlay(const String &title, const String &text, uint32_t ttlMs) {
  const uint32_t readingMs = min<uint32_t>(30000UL, max<uint32_t>(ttlMs, 6000UL + (text.length() * 55UL)));
  diagnostics_.lastDjText = text;
  djResponseOverlayTitle_ = title;
  display_.resetDjResponseOverlayCache();
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + readingMs;
  display_.wakeForUserActivity();
  showNotice(text, readingMs);
  renderNow();
}

void DJConnectApp::prepareForOta() {
  AppLog.line(String("OTA prepare: releasing runtime resources, free=") +
              String(heap_caps_get_free_size(MALLOC_CAP_8BIT)) +
              ", largest=" +
              String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  wakeWord_.releaseResources();
  sound_.requestStopStreaming();
  if (voiceRecorder_.isRecording()) {
    voiceRecorder_.abort();
  }
  voiceRecording_ = false;
  voiceState_ = VoiceState::Idle;
  webVoiceTextOnlyActive_ = false;
  webVoiceTextOnlyConsumeNext_ = false;
  delay(50);
  AppLog.line(String("OTA prepare: ready, free=") +
              String(heap_caps_get_free_size(MALLOC_CAP_8BIT)) +
              ", largest=" +
              String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

void DJConnectApp::renderMenuNow() {
  switch (activeScreen_) {
    case UiScreen::AlbumArt:
      display_.renderAlbumArtScreen(playback_, notice_, albumArt_.imagePath(), albumArt_.status());
      break;

    case UiScreen::RootMenu: {
      MenuItemView items[] = {
          {I18n::text("current_song")},
          {I18n::text("up_next")},
          {I18n::text("playlists")},
          {I18n::text("outputs")},
          {String(I18n::text("shuffle")) + " " + shuffleLabel(playback_.shuffle)},
          {String(I18n::text("repeat")) + " " + repeatLabel(playback_.repeatState)},
          {I18n::text("games")},
          {I18n::text("help")},
          {I18n::text("settings")},
          {I18n::text("about")},
          {I18n::text("logs")},
      };
      display_.renderMenuList(I18n::text("menu"), items, RootMenuItemCount, rootMenuSelection_, notice_);
      break;
    }

    case UiScreen::SoundOutputs: {
      MenuItemView items[8];
      items[0].label = I18n::text("none");
      size_t itemCount = DJConnectMenuModel::FixedSoundOutputCount;
      if (deviceList_.available && deviceList_.count > 0) {
        const size_t maxDevices = min(deviceList_.count, DJConnectMenuModel::MaxVisibleOutputs);
        for (size_t index = 0; index < maxDevices; index++) {
          items[index + DJConnectMenuModel::FixedSoundOutputCount].label = deviceList_.devices[index].active ? "* " : "  ";
          items[index + DJConnectMenuModel::FixedSoundOutputCount].label += deviceList_.devices[index].name;
        }
        itemCount += maxDevices;
      }
      display_.renderMenuList(I18n::text("outputs"), items, itemCount, soundOutputSelection_, notice_);
      break;
    }

    case UiScreen::Queue: {
      MenuItemView items[QueueState::MaxItems];
      size_t itemCount = queue_.count;
      if (!queue_.available || itemCount == 0) {
        items[0].label = queue_.error.isEmpty() ? I18n::text("queue_empty") : queue_.error;
        itemCount = 1;
      } else {
        itemCount = min(itemCount, QueueState::MaxItems);
        for (size_t index = 0; index < itemCount; index++) {
          items[index].label = queue_.items[index].title;
          if (!queue_.items[index].subtitle.isEmpty()) {
            items[index].label += " - " + queue_.items[index].subtitle;
          }
        }
      }
      if (queueSelection_ >= itemCount) {
        queueSelection_ = 0;
      }
      display_.renderMenuList(I18n::text("up_next"), items, itemCount, queueSelection_, notice_);
      break;
    }

    case UiScreen::Playlists: {
      MenuItemView items[8];
      size_t itemCount = playlists_.count;
      if (!playlists_.available || itemCount == 0) {
        items[0].label = playlists_.error.isEmpty() ? I18n::text("playlists") : playlists_.error;
        itemCount = 1;
      } else {
        itemCount = min(itemCount, static_cast<size_t>(8));
        for (size_t index = 0; index < itemCount; index++) {
          items[index].label = playlists_.items[index].name;
          if (!playlists_.items[index].owner.isEmpty()) {
            items[index].label += " - " + playlists_.items[index].owner;
          }
        }
      }
      display_.renderMenuList(I18n::text("playlists"), items, itemCount, playlistSelection_, notice_);
      break;
    }

    case UiScreen::About:
      display_.renderAboutScreen(notice_, aboutStatus(), aboutSelection_);
      break;

    case UiScreen::Logs: {
      String lines[9];
      const size_t available = AppLog.availableLines();
      const size_t maxScrollBack = available > 9 ? available - 9 : 0;
      logsScrollBack_ = min(logsScrollBack_, maxScrollBack);
      const size_t lineCount = AppLog.newestLines(lines, 9, logsScrollBack_);
      lastLogsRenderAt_ = millis();
      display_.renderLogsScreen(lines, lineCount, notice_);
      break;
    }

    case UiScreen::Pong:
      display_.renderPongScreen(pongPaddleY_, pongBallX_, pongBallY_, pongScore_, pongHighScore_, millis() < pongMissFlashUntil_, notice_);
      break;

    case UiScreen::Asteroids:
      display_.renderAsteroidsScreen(asteroidShipX_, asteroidShipY_, asteroidX_, asteroidY_, asteroidBulletY_, asteroidBulletActive_, asteroidScore_, asteroidHighScore_, millis() < asteroidFlashUntil_, notice_);
      break;

    case UiScreen::Flyer:
      display_.renderFlyerScreen(flyerPlaneY_, flyerObstacleX_, flyerObstacleY_, flyerShotX_, flyerShotActive_, flyerScore_, flyerHighScore_, millis() < flyerFlashUntil_, notice_);
      break;

    case UiScreen::MazeChase:
      display_.renderMazeChaseScreen(mazePlayerX_, mazePlayerLane_, mazeGhostX_, mazeGhostLane_, mazePelletX_, mazePelletLane_, mazeScore_, mazeHighScore_, millis() < mazePowerUntil_, millis() < mazeFlashUntil_, notice_);
      break;

    case UiScreen::Games: {
      MenuItemView items[GamesItemCount] = {
          {I18n::text("pong")},
          {I18n::text("asteroids")},
          {I18n::text("flyer")},
          {I18n::text("maze_chase")},
      };
      display_.renderMenuList(I18n::text("games"), items, GamesItemCount, gamesSelection_, notice_);
      break;
    }

    case UiScreen::Help: {
      MenuItemView items[HelpItemCount] = {
          {I18n::text("help_top_short")},
          {I18n::text("help_top_double")},
          {I18n::text("help_top_hold")},
          {I18n::text("help_top_10s")},
          {I18n::text("help_center_short")},
          {I18n::text("help_center_hold")},
          {I18n::text("help_encoder_turn")},
          {I18n::text("help_games")},
      };
      display_.renderMenuList(I18n::text("help"), items, HelpItemCount, helpSelection_, notice_);
      break;
    }

    case UiScreen::Settings: {
      MenuItemView items[] = {
          {String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%"},
          {String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_)},
          {String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_)},
          {String(I18n::text("language")) + " " + languageLabel(language_)},
          {String(I18n::text("theme")) + " " + themeLabel(themeCode_)},
          {String(I18n::text("log_level")) + " " + logLevelLabel(logLevel_)},
          {String(I18n::text("audio_feedback")) + " " + I18n::onOff(volumeFeedbackEnabled_)},
          {String(I18n::text("wake_word")) + " " + I18n::onOff(wakeWordEnabled_)},
          {String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%"},
          {String(I18n::text("stress_test")) + " " + I18n::onOff(stressTestActive_)},
          {I18n::text("turn_off_device")},
          {I18n::text("change_wifi")},
          {I18n::text("restart_device")},
          {I18n::text("reset_pairing")},
          {I18n::text("factory_reset")},
      };
      display_.renderMenuList(I18n::text("settings"), items, SettingsItemCount, settingsSelection_, notice_);
      break;
    }

    case UiScreen::DimTimeout: {
      MenuItemView items[DimTimeoutOptionCount];
      for (size_t index = 0; index < DimTimeoutOptionCount; index++) {
        const uint32_t valueMs = dimTimeoutValueMs(index);
        items[index].label = dimTimeoutLabel(valueMs);
        if (valueMs == screenOffTimeoutMs_) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("dim_timeout"),
          items,
          DimTimeoutOptionCount,
          dimTimeoutSelection_,
          notice_);
      break;
    }

    case UiScreen::Brightness: {
      MenuItemView items[BrightnessOptionCount];
      for (size_t index = 0; index < BrightnessOptionCount; index++) {
        const uint8_t valuePercent = brightnessValuePercent(index);
        items[index].label = String(valuePercent) + "%";
        if (valuePercent == screenBrightnessPercent_) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("brightness"),
          items,
          BrightnessOptionCount,
          brightnessSelection_,
          notice_);
      break;
    }

    case UiScreen::Language: {
      MenuItemView items[LanguageOptionCount] = {
          {languageLabel(Language::English)},
          {languageLabel(Language::Dutch)},
      };
      items[languageSelection_].label += " " + String(I18n::text("selected"));
      display_.renderMenuList(
          I18n::text("language"),
          items,
          LanguageOptionCount,
          languageSelection_,
          notice_);
      break;
    }

    case UiScreen::Theme: {
      MenuItemView items[ThemeOptionCount] = {
          {themeLabel("dark")},
          {themeLabel("light")},
          {themeLabel("auto")},
      };
      items[themeSelection_].label += " " + String(I18n::text("selected"));
      display_.renderMenuList(
          I18n::text("theme"),
          items,
          ThemeOptionCount,
          themeSelection_,
          notice_);
      break;
    }

    case UiScreen::LogLevel: {
      MenuItemView items[LogLevelOptionCount];
      for (size_t index = 0; index < LogLevelOptionCount; index++) {
        const String value = logLevelValue(index);
        items[index].label = logLevelLabel(value);
        if (value == logLevel_) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("log_level"),
          items,
          LogLevelOptionCount,
          logLevelSelection_,
          notice_);
      break;
    }

    case UiScreen::SpeakerVolume: {
      MenuItemView items[SpeakerVolumeOptionCount];
      for (size_t index = 0; index < SpeakerVolumeOptionCount; index++) {
        const uint8_t valuePercent = speakerVolumeValuePercent(index);
        items[index].label = String(valuePercent) + "%";
        if (valuePercent == speakerVolumePercent_) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("speaker_volume"),
          items,
          SpeakerVolumeOptionCount,
          speakerVolumeSelection_,
          notice_);
      break;
    }

    case UiScreen::ShuffleMode: {
      MenuItemView items[ShuffleOptionCount];
      for (size_t index = 0; index < ShuffleOptionCount; index++) {
        const bool enabled = shuffleValue(index);
        items[index].label = shuffleLabel(enabled);
        if (enabled == playback_.shuffle) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("shuffle"),
          items,
          ShuffleOptionCount,
          shuffleSelection_,
          notice_);
      break;
    }

    case UiScreen::RepeatMode: {
      MenuItemView items[RepeatOptionCount];
      for (size_t index = 0; index < RepeatOptionCount; index++) {
        const String repeat = repeatValue(index);
        items[index].label = repeatLabel(repeat);
        if (repeat == playback_.repeatState) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("repeat"),
          items,
          RepeatOptionCount,
          repeatSelection_,
          notice_);
      break;
    }

    case UiScreen::SleepTimeout: {
      MenuItemView items[SleepTimeoutOptionCount];
      for (size_t index = 0; index < SleepTimeoutOptionCount; index++) {
        const uint32_t valueMs = sleepTimeoutValueMs(index);
        items[index].label = minuteLabel(valueMs);
        if (valueMs == deviceSleepTimeoutMs_) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("deep_sleep_after"),
          items,
          SleepTimeoutOptionCount,
          sleepTimeoutSelection_,
          notice_);
      break;
    }

    case UiScreen::HardResetConfirm: {
      MenuItemView items[HardResetOptionCount] = {
          {I18n::text("confirm_no_go_back")},
          {I18n::text("confirm_yes_wipe_setup")},
      };
      display_.renderMenuList(I18n::text("factory_reset_title"), items, HardResetOptionCount, hardResetSelection_, notice_);
      break;
    }

    case UiScreen::ResetPairingConfirm: {
      MenuItemView items[HardResetOptionCount] = {
          {I18n::text("confirm_no")},
          {I18n::text("confirm_yes_reset_pairing")},
      };
      display_.renderMenuList(I18n::text("reset_pairing_title"), items, HardResetOptionCount, hardResetSelection_, notice_);
      break;
    }

    case UiScreen::ChangeWifiConfirm: {
      MenuItemView items[HardResetOptionCount] = {
          {I18n::text("confirm_no")},
          {I18n::text("confirm_yes_change_wifi")},
      };
      display_.renderMenuList(I18n::text("change_wifi_title"), items, HardResetOptionCount, hardResetSelection_, notice_);
      break;
    }

    case UiScreen::NowPlaying:
      display_.renderPlaybackScreen(
          playback_,
          battery_,
          notice_,
          displayedVolume(),
          homeAssistantPaired_,
          playbackConnectionState());
      break;
  }
}

void DJConnectApp::showNotice(const String &message, uint32_t ttlMs) {
  notice_.show(message, ttlMs);
}

int DJConnectApp::displayedVolume() const {
  return pendingVolume_ >= 0 ? pendingVolume_ : playback_.volume;
}

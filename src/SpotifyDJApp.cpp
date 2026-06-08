// Top-level application orchestration.
// This file wires together input, Spotify, display, battery, LED ring, and periodic refresh timing.
#include "SpotifyDJApp.h"

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
#include <cstring>
#include <time.h>

#include "AppLog.h"
#include "Config.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "assets/spotifydj_favicon_ico.h"
#include "assets/spotifydj_icon_192_png.h"

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

namespace {
static const char *const TopHoldMenuHint = "hold to open menu";
static const char GitHubApiCa[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDXzCCAuagAwIBAgIQNuBZ7YiN1Xrt1XC2cn+b2jAKBggqhkjOPQQDAzBfMQsw
CQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1T
ZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcN
MjEwMzIyMDAwMDAwWhcNMzYwMzIxMjM1OTU5WjBgMQswCQYDVQQGEwJHQjEYMBYG
A1UEChMPU2VjdGlnbyBMaW1pdGVkMTcwNQYDVQQDEy5TZWN0aWdvIFB1YmxpYyBT
ZXJ2ZXIgQXV0aGVudGljYXRpb24gQ0EgRFYgRTM2MFkwEwYHKoZIzj0CAQYIKoZI
zj0DAQcDQgAEaKGnbAUnBYljHDmn/yUhxe3TLxKYuyzc9VXoSaCEV5F73Fhfa/Si
/RMsmwTFW3R9s7J6JpYZFmu4do3vk/Vgl6OCAYEwggF9MB8GA1UdIwQYMBaAFNEi
2kxZ8UtfJjiqndbu6w3D+6lhMB0GA1UdDgQWBBQXmagEwW/kLXCoChA9A9PpGrgm
YzAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHSUEFjAU
BggrBgEFBQcDAQYIKwYBBQUHAwIwGwYDVR0gBBQwEjAGBgRVHSAAMAgGBmeBDAEC
ATBUBgNVHR8ETTBLMEmgR6BFhkNodHRwOi8vY3JsLnNlY3RpZ28uY29tL1NlY3Rp
Z29QdWJsaWNTZXJ2ZXJBdXRoZW50aWNhdGlvblJvb3RFNDYuY3JsMIGEBggrBgEF
BQcBAQR4MHYwTwYIKwYBBQUHMAKGQ2h0dHA6Ly9jcnQuc2VjdGlnby5jb20vU2Vj
dGlnb1B1YmxpY1NlcnZlckF1dGhlbnRpY2F0aW9uUm9vdEU0Ni5wN2MwIwYIKwYB
BQUHMAGGF2h0dHA6Ly9vY3NwLnNlY3RpZ28uY29tMAoGCCqGSM49BAMDA2cAMGQC
MFsKnBQDh64l+v+aUYWjDCJKQMxHUUGmcwAYDIjJ9pbRYItMCIx5xu0oUb6sIfTX
qQIwPddcsDE4KdeLu1hJdpHgdLvsHAK3vygyLGujMU9xBJCDackRT93VHEE0gppg
NqdV
-----END CERTIFICATE-----
)EOF";

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

using namespace SpotifyDJMenu;

void SpotifyDJApp::begin() {
  pinMode(Config::DisplayBacklightPin, OUTPUT);
  digitalWrite(Config::DisplayBacklightPin, LOW);

  Serial.begin(115200);
  AppLog.begin();
  AppLog.println(Config::BuildMarker);
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
  haPairing_.setSpotifyProvisionedCallback(spotifyProvisionedCallback, this);
  softResetMonitor_.begin(battery_, softResetCueCallback, this);
  albumArt_.begin();
  ledRing_.begin();
  sound_.begin();
  djAudio_.begin(sound_, &ledRing_);
  voiceRecorder_.begin();
  wakeWord_.begin();
  wakeWord_.setCallback(wakeWordDetectedCallback, this);
  voiceClient_.begin(haDevice_);
  homeAssistantPaired_ = false;
  loadProvisioning();
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
    lastPlaybackPollAt_ = Logic::forceImmediatePollTimestamp();
  }

  lastBatteryPollAt_ = millis();
  loopMetricsWindowStartedAt_ = millis();
  if (wifiConnectFailed_) {
    return;
  }
  renderNow();
}

void SpotifyDJApp::loop() {
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
    renderNow();
  }
  if (voiceRecording_) {
    if (!voiceRecorder_.update()) {
      const String error = voiceRecorder_.error();
      voiceRecorder_.abort();
      voiceRecording_ = false;
      voiceState_ = VoiceState::Error;
      voiceClient_.sendStatus(false, "error", error);
      showNotice(error, 3000);
      renderNow();
    } else if (Logic::shouldAutoStopVoiceRecording(voiceRecorder_.elapsedMs(), Config::VoiceMaxRecordMs)) {
      stopVoiceRecordingAndSendText();
    }
  }
  if (!voiceRecording_ && voiceState_ == VoiceState::Idle && homeAssistantPaired_) {
    wakeWord_.loop(voiceRecorder_);
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
  }

  updateVisualPower();
  if (!deepSleepStarted_ && display_.idleMs() >= deviceSleepTimeoutMs_) {
    enterDeepSleep();
  }
  pollBatteryIfDue();
  pollPlaybackIfDue();
  sendHomeAssistantStatusIfDue();

  recordLoopMetrics(loopStartedAt);
  logHeapIfDue();
  responsiveDelay(5);
}

void SpotifyDJApp::loadProvisioning() {
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
  speakerVolumePercent_ = settings.speakerVolumePercent;
  volumeFeedbackEnabled_ = settings.volumeFeedbackEnabled;
  setupModeRequested_ = settings.setupModeRequested;

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

bool SpotifyDJApp::shouldStartProvisioningPortal() const {
  return setupModeRequested_ || wifiSsid_.isEmpty();
}

void SpotifyDJApp::handleWifiConnectFailureLoop(uint32_t loopStartedAt) {
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

void SpotifyDJApp::renderWifiConnectFailureMenu() {
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

void SpotifyDJApp::applyWifiConnectFailureSelection() {
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

bool SpotifyDJApp::connectWiFi(uint32_t timeoutMs, bool bootScreen) {
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

void SpotifyDJApp::startWebPortalIfNeeded() {
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

bool SpotifyDJApp::syncClock() {
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

void SpotifyDJApp::runCaptivePortal() {
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
    server.send_P(200, "image/x-icon", reinterpret_cast<const char *>(SPOTIFYDJ_FAVICON_ICO), SPOTIFYDJ_FAVICON_ICO_LEN);
  });

  server.on("/icon-192.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(SPOTIFYDJ_ICON_192_PNG), SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server.on("/apple-touch-icon.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(SPOTIFYDJ_ICON_192_PNG), SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server.on("/apple-touch-icon-precomposed.png", HTTP_GET, [&]() {
    server.send_P(200, "image/png", reinterpret_cast<const char *>(SPOTIFYDJ_ICON_192_PNG), SPOTIFYDJ_ICON_192_PNG_LEN);
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

bool SpotifyDJApp::handleBleProvisioningPayload(const String &payload, String &message) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    message = "BLE setup JSON failed.";
    AppLog.println("BLE provisioning JSON failed");
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

String SpotifyDJApp::captivePortalPage(
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
  page.reserve(4600);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<meta name='theme-color' content='#080b0c'>");
  page += F("<meta name='mobile-web-app-capable' content='yes'>");
  page += F("<meta name='apple-mobile-web-app-capable' content='yes'>");
  page += F("<meta name='apple-mobile-web-app-title' content='SpotifyDJ Setup'>");
  page += F("<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>");
  page += F("<meta name='application-name' content='SpotifyDJ Setup'>");
  page += F("<link rel='shortcut icon' href='/favicon.ico?v=2' sizes='any'>");
  page += F("<link rel='icon' href='/favicon.ico?v=2' sizes='any'>");
  page += F("<link rel='icon' type='image/png' sizes='192x192' href='/icon-192.png?v=2'>");
  page += F("<link rel='apple-touch-icon' sizes='180x180' href='/apple-touch-icon.png'>");
  page += F("<link rel='apple-touch-icon' sizes='192x192' href='/apple-touch-icon.png?v=2'>");
  page += F("<link rel='apple-touch-icon-precomposed' sizes='180x180' href='/apple-touch-icon-precomposed.png'>");
  page += F("<link rel='apple-touch-icon-precomposed' sizes='192x192' href='/apple-touch-icon-precomposed.png?v=2'>");
  page += F("<title>SpotifyDJ Setup</title><style>");
  page += F("*{box-sizing:border-box}body{margin:0;background:#080b0c;color:#f3f7f5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}");
  page += F("main{width:100%;max-width:520px;margin:0 auto;padding:18px}.brand{display:flex;align-items:center;gap:10px;margin:8px 0 4px}.brand img{width:42px;height:42px;border-radius:8px}h1{font-size:26px;margin:0}p{color:#9aa6a2;line-height:1.4}");
  page += F(".box{background:#111718;border:1px solid #233033;border-radius:8px;padding:14px;margin-top:14px}");
  page += F(".timeout{border:1px solid #4c3d17;background:#1a1608;color:#ffe28a;border-radius:8px;padding:10px 12px;margin:12px 0}");
  page += F(".msg{border-radius:8px;padding:10px 12px;margin:12px 0;background:");
  page += error ? F("#3a1714;color:#ffd1c9") : F("#173721;color:#baf7ca");
  page += F("}label{display:grid;gap:6px;margin:12px 0;color:#a8b3af;font-size:13px}");
  page += F("input,button{display:block;width:100%;max-width:100%;min-height:44px;border-radius:8px;border:1px solid #233033;background:#0c1112;color:#f3f7f5;padding:9px 10px;font-size:16px}");
  page += F("button{background:#173721;border-color:#25593a;color:#baf7ca;font-weight:700;margin-top:8px}");
  page += F("</style></head><body><main><div class='brand'><img src='/icon-192.png' alt=''><h1>SpotifyDJ Setup</h1></div>");
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

bool SpotifyDJApp::testAndSaveProvisioning(
    const String &ssid,
    const String &password,
    String &message) {
  display_.showBootMessage(I18n::text("boot_testing_wifi"), battery_);
  ledRing_.showSolid(CRGB::Yellow, 100);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    responsiveDelay(250);
    AppLog.print(".");
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

  provisioning_.saveSetupProvisioning(ssid, password, "", "", "");

  wifiSsid_ = ssid;
  wifiPassword_ = password;
  setupModeRequested_ = false;
  message = I18n::text("setup_success_restart");
  display_.showBootMessage(I18n::text("boot_setup_ok"), battery_);
  return true;
}

void SpotifyDJApp::handleInputEvents(const InputEvents &events) {
  if (suppressInputUntilRelease_) {
    input_.clearPendingButtonActions();
    if (!events.buttonHeld) {
      suppressInputUntilRelease_ = false;
    }
    return;
  }

  if (events.touched) {
    if (display_.backlightPercent() == 0) {
      display_.wakeForUserActivity();
      input_.clearPendingButtonActions();
      suppressInputUntilRelease_ = events.buttonHeld;
      return;
    }
    display_.wakeForUserActivity();
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

  if (events.encoderPress && volumeFeedbackEnabled_) {
    sound_.playButtonPress();
  }

  if (homeAssistantPaired_ && events.encoderRelease && voiceRecording_) {
    input_.clearPendingButtonActions();
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

void SpotifyDJApp::handlePlaybackInputEvents(const InputEvents &events) {
  if (events.encoderSteps != 0) {
    handleEncoderTurn(events.encoderSteps);
  }

  if (homeAssistantPaired_ && events.encoderRelease && !voiceRecording_ && voiceState_ == VoiceState::Idle) {
    AppLog.println("Voice: encoder released without active PTT");
  }

  if (events.encoderClick) {
    if (playback_.hasPlayback) {
      pauseOrResume();
    } else {
      startLikedProxyPlaylist();
    }
  }

  if (events.topButtonClick) {
    goToNextTrack();
  }

  if (events.topButtonDoubleClick) {
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

void SpotifyDJApp::handleMenuInputEvents(const InputEvents &events) {
  if (activeScreen_ == UiScreen::Pong && events.encoderSteps != 0) {
    pongPaddleY_ = constrain(pongPaddleY_ + (events.encoderSteps * 5), 42, 126);
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

void SpotifyDJApp::openRootMenu() {
  activeScreen_ = UiScreen::RootMenu;
  menuStackSize_ = 0;
  if (volumeFeedbackEnabled_) {
    sound_.playMenuOpen();
  }
  showNotice(I18n::text("menu"), 900);
  renderNow();
}

void SpotifyDJApp::openScreen(UiScreen screen) {
  if (menuStackSize_ < MenuStackCapacity) {
    menuStack_[menuStackSize_++] = activeScreen_;
  }
  activeScreen_ = screen;
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
}

void SpotifyDJApp::goBackOneScreen() {
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

void SpotifyDJApp::moveMenuSelection(int encoderSteps) {
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

void SpotifyDJApp::selectCurrentMenuItem() {
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
        playModeSelection_ = 0;
        const String currentMode = currentPlayModeValue(playback_);
        for (size_t index = 0; index < PlayModeOptionCount; index++) {
          if (playModeValue(index) == currentMode) {
            playModeSelection_ = index;
            break;
          }
        }
        openScreen(UiScreen::PlayMode);
      } else if (rootMenuSelection_ == 5) {
        openScreen(UiScreen::Settings);
      } else if (rootMenuSelection_ == 6) {
        openScreen(UiScreen::About);
      } else if (rootMenuSelection_ == 7) {
        logsScrollBack_ = 0;
        openScreen(UiScreen::Logs);
      } else if (rootMenuSelection_ == 8) {
        resetPong();
        openScreen(UiScreen::Pong);
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
        openScreen(UiScreen::SpeakerVolume);
      } else if (settingsSelection_ == 8) {
        toggleStressTest();
      } else if (settingsSelection_ == 9) {
        display_.showBootMessage(I18n::text("turning_off"), battery_);
        responsiveDelay(250);
        enterDeepSleep();
      } else if (settingsSelection_ == 10) {
        sound_.playHardReset();
        display_.showBootMessage(I18n::text("restarting"), battery_);
        responsiveDelay(320);
        ESP.restart();
      } else if (settingsSelection_ == 11) {
        hardResetSelection_ = 0;
        openScreen(UiScreen::ResetPairingConfirm);
      } else if (settingsSelection_ == 12) {
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

    case UiScreen::PlayMode:
      applyPlayModeSelection();
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

    case UiScreen::HardResetConfirm:
      if (hardResetSelection_ == 0) {
        goBackOneScreen();
      } else {
        hardResetToProvisioning();
      }
      break;

    case UiScreen::About:
    case UiScreen::NowPlaying:
    case UiScreen::Queue:
    case UiScreen::Logs:
      break;
  }
}

void SpotifyDJApp::applyDimTimeoutSelection() {
  screenOffTimeoutMs_ = dimTimeoutValueMs(dimTimeoutSelection_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  AppLog.print("Settings: screen dim timeout ");
  AppLog.println(dimTimeoutLabel(screenOffTimeoutMs_));
  showNotice(String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_), 2000);
  renderNow();
}

void SpotifyDJApp::applyBrightnessSelection() {
  screenBrightnessPercent_ = brightnessValuePercent(brightnessSelection_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  AppLog.print("Settings: screen brightness ");
  AppLog.print(screenBrightnessPercent_);
  AppLog.println("%");
  showNotice(String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%", 2000);
  renderNow();
}

void SpotifyDJApp::applySpeakerVolumeSelection() {
  speakerVolumePercent_ = speakerVolumeValuePercent(speakerVolumeSelection_);
  sound_.setVolumePercent(speakerVolumePercent_);
  saveDisplaySettings();
  AppLog.print("Settings: speaker volume ");
  AppLog.print(speakerVolumePercent_);
  AppLog.println("%");
  showNotice(String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%", 2000);
  renderNow();
}

void SpotifyDJApp::applyLogLevelSelection() {
  logLevel_ = logLevelValue(logLevelSelection_);
  saveDisplaySettings();
  AppLog.setLevel(logLevel_);
  showNotice(String(I18n::text("log_level")) + " " + logLevelLabel(logLevel_), 2000);
  renderNow();
}

void SpotifyDJApp::applyPlayModeSelection() {
  const String mode = playModeValue(playModeSelection_);
  AppLog.print("Playback: setting play mode ");
  AppLog.println(mode);
  showNotice(I18n::text("spotify_play_mode"), 1600);
  renderNow();
  if (spotify_.setPlayMode(mode)) {
    AppLog.print("Playback: play mode set to ");
    AppLog.println(mode);
    showNotice(playModeLabel(mode), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Playback: play mode failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::applySleepTimeoutSelection() {
  // The selected timeout controls full ESP32-S3 deep sleep, not just the display backlight.
  deviceSleepTimeoutMs_ = sleepTimeoutValueMs(sleepTimeoutSelection_);
  saveDisplaySettings();
  AppLog.print("Settings: turn off after ");
  AppLog.print(deviceSleepTimeoutMs_ / 60000UL);
  AppLog.println(" min");
  showNotice(String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_), 2000);
  renderNow();
}

void SpotifyDJApp::saveDisplaySettings() {
  languageCode_ = I18n::languageCode();
  provisioning_.saveDisplaySettings(
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_,
      screenBrightnessPercent_,
      languageCode_,
      themeCode_,
      logLevel_,
      speakerVolumePercent_,
      volumeFeedbackEnabled_);
}

void SpotifyDJApp::hardResetToProvisioning() {
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

void SpotifyDJApp::resetHomeAssistantPairing() {
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

bool SpotifyDJApp::isMenuActive() const {
  return isMenuScreen(activeScreen_);
}

size_t SpotifyDJApp::menuItemCount(UiScreen screen) const {
  return itemCount(screen, playlists_, deviceList_);
}

size_t SpotifyDJApp::selectedIndexForScreen(UiScreen screen) const {
  switch (screen) {
    case UiScreen::RootMenu:
      return rootMenuSelection_;
    case UiScreen::Settings:
      return settingsSelection_;
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
    case UiScreen::PlayMode:
      return playModeSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
    case UiScreen::ResetPairingConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
      return aboutSelection_;
    case UiScreen::AlbumArt:
    case UiScreen::Queue:
    case UiScreen::Logs:
    case UiScreen::NowPlaying:
      return 0;
  }
  return 0;
}

size_t &SpotifyDJApp::selectedIndexRefForScreen(UiScreen screen) {
  switch (screen) {
    case UiScreen::RootMenu:
      return rootMenuSelection_;
    case UiScreen::Settings:
      return settingsSelection_;
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
    case UiScreen::PlayMode:
      return playModeSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
    case UiScreen::ResetPairingConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
      return aboutSelection_;
    case UiScreen::AlbumArt:
    case UiScreen::Queue:
    case UiScreen::Logs:
    case UiScreen::Pong:
    case UiScreen::NowPlaying:
      return rootMenuSelection_;
  }
  return rootMenuSelection_;
}

void SpotifyDJApp::applyTheme() {
  // The ESP has no browser preference, so auto maps to the normal dark TFT palette.
  display_.setLightTheme(themeCode_ == "light");
}

void SpotifyDJApp::handleEncoderTurn(int encoderSteps) {
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

void SpotifyDJApp::flushPendingVolume() {
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

void SpotifyDJApp::processVolumeResult() {
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

void SpotifyDJApp::processStressTest() {
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

void SpotifyDJApp::toggleStressTest() {
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

void SpotifyDJApp::stopStressTest(const String &reason) {
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

void SpotifyDJApp::runStressTestStep() {
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

void SpotifyDJApp::resetPong() {
  pongPaddleY_ = 86;
  pongBallX_ = 160;
  pongBallY_ = 86;
  pongVelocityX_ = 3;
  pongVelocityY_ = 2;
  pongScore_ = 0;
  pongMissFlashUntil_ = 0;
  lastPongFrameAt_ = 0;
}

void SpotifyDJApp::updatePong() {
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

bool SpotifyDJApp::handleDeviceCommand(const DeviceCommand &command, String &message) {
  if (command.type == DeviceCommandType::Status) {
    AppLog.println("Device command: status");
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

  if (!playbackProxyReady()) {
    AppLog.println("Device command ignored: playback not connected");
    message = I18n::text("spotify_not_connected");
    return false;
  }

  switch (command.type) {
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
      break;

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
  message = "Unsupported command";
  return false;
}

void SpotifyDJApp::pauseOrResume() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  const uint32_t now = millis();
  if (now - lastPauseToggleAt_ < 900) {
    return;
  }
  lastPauseToggleAt_ = now;

  if (playback_.isPlaying) {
    if (spotify_.pausePlayback()) {
      playback_.isPlaying = false;
      showNotice(I18n::text("paused"));
      lastPlaybackPollAt_ = 0;
    } else {
      showNotice(playback_.error, 3500);
    }
  } else {
    if (spotify_.resumePlayback()) {
      playback_.isPlaying = true;
      playback_.progressSyncedAt = millis();
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      showNotice(I18n::text("playing"));
      lastPlaybackPollAt_ = 0;
    } else {
      showNotice(playback_.error, 3500);
    }
  }
  renderNow();
}

void SpotifyDJApp::startLikedProxyPlaylist() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  AppLog.println("Playback: starting Liked Proxy playlist");
  showNotice(I18n::text("starting_liked_proxy"), 1600);
  renderNow();
  if (spotify_.startLikedProxyPlaylist()) {
    AppLog.println("Playback: Liked Proxy playlist command accepted");
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
    AppLog.print("Playback: Liked Proxy playlist failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::handleVoiceButton() {
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

#if defined(SPOTIFYDJ_DEBUG_TEXT_COMMAND)
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
  voiceState_ = VoiceState::Connecting;
  AppLog.println("Voice: starting WAV recording");
  showNotice(I18n::text("voice_connecting"), 3000);
  renderNow();
  if (!voiceRecorder_.start()) {
    const String error = voiceRecorder_.error();
    AppLog.print("Voice: mic start failed: ");
    AppLog.println(error);
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showNotice(error, 3000);
    renderNow();
    return;
  }
  voiceRecording_ = true;
  voiceState_ = VoiceState::Listening;
  AppLog.println("Voice: listening");
  if (volumeFeedbackEnabled_) {
    sound_.playPttStart();
  }
  ledRing_.playPulse(CRGB::Yellow);
  voiceClient_.sendStatus(true, "recording");
  diagnostics_.lastDjText = I18n::text("voice_listening");
  display_.resetDjResponseOverlayCache();
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + Config::VoiceMaxRecordMs + 1000;
  showNotice(I18n::text("voice_listening"), Config::VoiceMaxRecordMs + 1000);
  renderNow();
}

void SpotifyDJApp::stopVoiceRecordingAndSendText() {
  if (!voiceRecording_) {
    return;
  }
  voiceRecording_ = false;
  voiceState_ = VoiceState::WaitingForResult;
  AppLog.println("Voice: recording stopped, uploading WAV");
  if (volumeFeedbackEnabled_) {
    sound_.playPttStop();
  }
  ledRing_.playPulse(CRGB::Blue);
  diagnostics_.lastDjText = I18n::text("voice_processing");
  display_.resetDjResponseOverlayCache();
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + 3000;
  showNotice(I18n::text("voice_processing"), 3000);
  renderNow();
  if (!voiceRecorder_.stop()) {
    const String error = voiceRecorder_.error();
    AppLog.print("Voice: mic stop failed: ");
    AppLog.println(error);
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showNotice(error, 3500);
    renderNow();
    return;
  }

  String message;
  voiceState_ = VoiceState::SendingCommand;
  AppLog.println("Voice: uploading WAV to HA integration");
  voiceClient_.sendStatus(false, "sending_command");
  showNotice("SpotifyDJ...", 2500);
  renderNow();
  String audioUrl;
  if (voiceClient_.uploadWav(voiceRecorder_.wavPath(), message, &audioUrl)) {
    voiceState_ = VoiceState::Done;
    AppLog.println("Voice: command accepted");
    ledRing_.playPulse(CRGB::Green);
    voiceClient_.sendStatus(false, "idle");
    bool spoken = false;
    if (message != "Voice command sent") {
      handleDjResponseText(message, audioUrl, spoken);
    } else if (!audioUrl.isEmpty()) {
      spoken = djAudio_.play(audioUrl).spoken;
      showNotice(spoken ? I18n::text("voice_response_played") : I18n::text("voice_response_audio_failed"), 3500);
    } else {
      showNotice(message, 3500);
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
    showNotice(message, 3500);
  }
  voiceRecorder_.abort();
  if (voiceState_ == VoiceState::Done) {
    voiceState_ = VoiceState::Idle;
  }
  renderNow();
}

void SpotifyDJApp::goToNextTrack() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  if (spotify_.nextTrack()) {
    // Optimistic UI while Spotify switches tracks; the next poll replaces this with real metadata.
    playback_.trackName = I18n::text("loading_next_track");
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice(I18n::text("next_track"));

    if (!playback_.isPlaying && spotify_.resumePlayback()) {
      playback_.isPlaying = true;
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      showNotice(I18n::text("playing"));
    }

    lastPlaybackPollAt_ = 0;
  } else {
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::goToPreviousTrack() {
  if (!playbackProxyReady()) {
    showNotice(I18n::text("ha_pairing_invalid"), 3000);
    renderNow();
    return;
  }

  if (spotify_.previousTrack()) {
    // Optimistic UI while Spotify switches tracks; the next poll replaces this with real metadata.
    playback_.trackName = I18n::text("loading_previous_track");
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice(I18n::text("previous_track"));
    lastPlaybackPollAt_ = 0;
  } else {
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::startSelectedPlaylist() {
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

bool SpotifyDJApp::transferToOutputByNameOrId(const String &output) {
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

bool SpotifyDJApp::startPlaylistByNameOrUri(const String &playlist) {
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

void SpotifyDJApp::refreshPlaybackAndBattery() {
  showNotice(I18n::text("refreshing"));
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  if (playbackProxyReady()) {
    spotify_.refreshPlayback();
  } else {
    playback_.error = I18n::text("ha_pairing_invalid");
    showNotice(playback_.error, 2500);
  }
  if (activeScreen_ == UiScreen::AlbumArt) {
    albumArt_.requestCurrentSongArt(playback_);
  }
  lastBatteryPollAt_ = millis();
  lastPlaybackPollAt_ = millis();
  renderNow();
}

void SpotifyDJApp::pollBatteryIfDue() {
  if (millis() - lastBatteryPollAt_ <= Config::BatteryPollIntervalMs) {
    return;
  }

  lastBatteryPollAt_ = millis();
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  renderNow();
}

void SpotifyDJApp::pollPlaybackIfDue() {
  if (millis() - lastPlaybackPollAt_ <= Config::PlaybackPollIntervalMs) {
    return;
  }

  lastPlaybackPollAt_ = millis();
  if (!playbackProxyReady()) {
    return;
  }
  spotify_.refreshPlayback();
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
  if (activeScreen_ == UiScreen::AlbumArt) {
    albumArt_.requestCurrentSongArt(playback_);
  }
  renderNow();
}

void SpotifyDJApp::openAlbumArtScreen() {
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
  albumArt_.requestCurrentSongArt(playback_);
  renderNow();
}

void SpotifyDJApp::openQueueScreen() {
  activeScreen_ = UiScreen::Queue;
  menuStackSize_ = 0;
  showNotice(I18n::text("loading_queue"), 1200);
  renderNow();
  if (playbackProxyReady()) {
    spotify_.refreshQueue(queue_);
  } else {
    queue_.available = false;
    queue_.error = I18n::text("ha_pairing_invalid");
  }
  renderNow();
}

void SpotifyDJApp::openSoundOutputsScreen() {
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

bool SpotifyDJApp::playbackProxyReady() const {
  return homeAssistantPaired_ && spotify_.isAuthorized();
}

PlaybackConnectionState SpotifyDJApp::playbackConnectionState() const {
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

void SpotifyDJApp::transferToSelectedOutput() {
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

  if (soundOutputSelection_ == 1) {
    if (transferToOutputByNameOrId("iPhone")) {
      return;
    }
    showNotice(playback_.error, 3500);
    renderNow();
    return;
  }

  const size_t deviceIndex = soundOutputSelection_ - SpotifyDJMenuModel::FixedSoundOutputCount;
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

void SpotifyDJApp::renderNow() {
  if (lowBatteryGuardActive_ || criticalBatteryGuardActive_) {
    renderLowBatteryGuard();
    if (djResponseOverlayVisible_) {
      display_.renderDjResponseOverlay(diagnostics_.lastDjText);
    }
    return;
  }

  if (djResponseOverlayVisible_) {
    display_.renderDjResponseOverlay(diagnostics_.lastDjText);
    visualState_.screenOn = display_.isOn();
    visualState_.screenBrightnessLevel = display_.backlightPercent();
    visualState_.ledOn = ledRing_.isOn();
    return;
  }

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
  if (connectionHealthy()) {
    if (playback_.hasPlayback && playback_.supportsVolume && displayedVolume() >= 0) {
      ledRing_.showVolume(displayedVolume());
    } else {
      ledRing_.setPowerPercent(0);
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

bool SpotifyDJApp::dismissDjResponseOverlay() {
  if (!djResponseOverlayVisible_) {
    return false;
  }
  djResponseOverlayVisible_ = false;
  display_.resetDjResponseOverlayCache();
  renderNow();
  return true;
}

bool SpotifyDJApp::chargerConnected() const {
  return power_.chargerConnected(battery_);
}

bool SpotifyDJApp::shouldReturnToSleepAfterTimerWake() const {
  return power_.shouldReturnToSleepAfterTimerWake(battery_);
}

void SpotifyDJApp::evaluateBatteryTransition() {
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

bool SpotifyDJApp::updateLowBatteryGuard() {
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

void SpotifyDJApp::renderLowBatteryGuard() {
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

bool SpotifyDJApp::connectionHealthy() {
  const bool homeAssistantOk = homeAssistantPaired_;
  const bool playbackOkOrIdle = playbackConnectionState() != PlaybackConnectionState::Error;
  return homeAssistantOk && playbackOkOrIdle;
}

AboutStatus SpotifyDJApp::aboutStatus() {
  AboutStatus status;
  status.wifiConnected = WiFi.status() == WL_CONNECTED;
  status.ipAddress = status.wifiConnected ? WiFi.localIP().toString() : "";
  status.webAddress = status.wifiConnected ? "http://" + WiFi.localIP().toString() : "";
  status.haPaired = homeAssistantPaired_;
  status.spotifyConnected = playbackConnectionState() == PlaybackConnectionState::Ok;
  return status;
}

void SpotifyDJApp::updateVisualPower() {
  display_.updateIdleBrightness();
  const uint8_t currentBacklightPercent = display_.backlightPercent();
  ledRing_.setPowerPercent(currentBacklightPercent);
  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = currentBacklightPercent;
  visualState_.ledOn = ledRing_.isOn();
}

void SpotifyDJApp::configureWatchdog() {
  power_.configureWatchdog();
}

void SpotifyDJApp::serviceWatchdog() {
  power_.serviceWatchdog();
}

void SpotifyDJApp::responsiveDelay(uint32_t durationMs) {
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

void SpotifyDJApp::logHeapIfDue() {
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

void SpotifyDJApp::enterDeepSleep() {
  deepSleepStarted_ = true;
  AppLog.println("Entering deep sleep");
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

void SpotifyDJApp::enterDeepSleepWithoutDisplay() {
  deepSleepStarted_ = true;
  sound_.playTurnOff();
  responsiveDelay(220);
  esp_sleep_enable_ext1_wakeup(power_.buttonWakeMask(), ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(power_.sleepTimerWakeUs(false));
  WiFi.disconnect(true, false);
  responsiveDelay(50);
  esp_deep_sleep_start();
}

void SpotifyDJApp::recordLoopMetrics(uint32_t loopStartedAt) {
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

void SpotifyDJApp::applyWebSettings(
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel) {
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
  applyTheme();
  AppLog.setLevel(logLevel_);
  sound_.setVolumePercent(speakerVolumePercent_);
  saveDisplaySettings();
  showNotice(I18n::text("web_settings_saved"), 1800);
  renderNow();
  display_.showBootMessage(I18n::text("restarting"), battery_);
  responsiveDelay(450);
  ESP.restart();
}

void SpotifyDJApp::applyProvisionedLanguage(const String &languageCode) {
  const String normalized = SpotifyDJDevice::normalizedLanguageCode(languageCode);
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

void SpotifyDJApp::applyProvisionedSpotifyCredentials() {
  spotify_.reloadCredentials();
  AppLog.println("Playback proxy refreshed after HA provisioning");
  lastPlaybackPollAt_ = millis();
}

bool SpotifyDJApp::checkBootstrapFirmwareUpdate() {
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
  releaseHttp.addHeader("User-Agent", "SpotifyDJ");
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
  manifestHttp.addHeader("User-Agent", "SpotifyDJ");
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

  SpotifyDJOTARequest request;
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

void SpotifyDJApp::requestWebWifiSettings(const String &ssid, const String &password) {
  pendingWifiSsid_ = ssid;
  pendingWifiSsid_.trim();
  pendingWifiPassword_ = password;
  pendingWifiPasswordProvided_ = !password.isEmpty();
  pendingWifiSettingsRequestedAt_ = millis();
  pendingWifiSettings_ = !pendingWifiSsid_.isEmpty();
  showNotice(I18n::text("testing_wifi"), 2500);
  renderNow();
}

void SpotifyDJApp::processPendingWifiSettings() {
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
  ledRing_.showSolid(CRGB::Yellow, 100);

  WiFi.disconnect(false, false);
  responsiveDelay(250);
  WiFi.mode(WIFI_STA);
  WiFi.begin(pendingWifiSsid_.c_str(), targetPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    responsiveDelay(250);
    AppLog.print(".");
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

void SpotifyDJApp::setupHomeAssistantLayer() {
  if (WiFi.status() != WL_CONNECTED || !webPortal_.isRunning()) {
    return;
  }

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
      directPairCallback);

  AppLog.print("Home Assistant paired: ");
  AppLog.println(haDevice_.isPaired() ? "true" : "false");
  if (haDevice_.isPaired()) {
    AppLog.println("Home Assistant URL configured");
  } else {
    haDevice_.displayPairingCode();
  }
}

void SpotifyDJApp::sendHomeAssistantStatusIfDue(bool force) {
  if (!haDevice_.isPaired() || WiFi.status() != WL_CONNECTED) {
    return;
  }
  const uint32_t now = millis();
  if (!force && now - lastHaStatusAt_ < Config::HaStatusIntervalMs) {
    return;
  }
  lastHaStatusAt_ = now;
  const bool playbackProxyUsable =
      Logic::spotifyConfiguredForHomeAssistantStatus(haDevice_.isSpotifyConfigured(), spotify_.needsCredentialRefresh());
  if (haDevice_.isSpotifyConfigured() && !playbackProxyUsable) {
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
  const SpotifyDJPairing::StatusResult result =
      haPairing_.sendStatusToHA(battery_, playbackProxyUsable, settings, visualState_);
  if (result == SpotifyDJPairing::StatusResult::Ok) {
    homeAssistantPaired_ = true;
    haPairingPendingValidation_ = false;
  } else if (result == SpotifyDJPairing::StatusResult::PairingInvalid) {
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
  }
}

void SpotifyDJApp::markHomeAssistantPairingInvalid(const String &message) {
  if (homeAssistantPaired_) {
    AppLog.println("Home Assistant: pairing invalid or stale");
  }
  homeAssistantPaired_ = false;
  haPairingPendingValidation_ = false;
  showNotice(message, 5000);
  renderNow();
}

bool SpotifyDJApp::handleHomeAssistantPairingMode(uint32_t loopStartedAt) {
  if (haDevice_.isPaired()) {
    if (haPairingScreenActive_) {
      homeAssistantPaired_ = true;
      haPairingPendingValidation_ = true;
      haPairingScreenActive_ = false;
      haPairingStartedAt_ = 0;
      lastHaStatusAt_ = Logic::forceImmediatePollTimestamp();
      if (bleProvisioning_.isStarted()) {
        bleProvisioning_.end();
      }
      haDevice_.displayPaired();
      responsiveDelay(700);
      display_.showBootMessage(I18n::text("boot_connecting_playback"), battery_);
      lastPlaybackPollAt_ = Logic::forceImmediatePollTimestamp();
      renderNow();
      recordLoopMetrics(loopStartedAt);
      return false;
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
    enterDeepSleep();
  }
  recordLoopMetrics(loopStartedAt);
  responsiveDelay(10);
  return true;
}

void SpotifyDJApp::applyWebSettingsCallback(
    void *context,
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs,
    uint8_t speakerVolumePercent,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel) {
  static_cast<SpotifyDJApp *>(context)->applyWebSettings(
      brightnessPercent,
      offTimeoutMs,
      sleepTimeoutMs,
      speakerVolumePercent,
      languageCode,
      themeCode,
      logLevel);
}

void SpotifyDJApp::applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password) {
  static_cast<SpotifyDJApp *>(context)->requestWebWifiSettings(ssid, password);
}

bool SpotifyDJApp::sendWebVoiceTextCallback(void *context, const String &text, String &message, String &audioUrl) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  SpotifyDJApp *app = static_cast<SpotifyDJApp *>(context);
  AppLog.print("Web voice: sending text chars=");
  AppLog.println(text.length());
  app->voiceState_ = VoiceState::SendingCommand;
  app->voiceRecording_ = false;
  app->notice_.visibleUntil = 0;
  app->voiceClient_.sendStatus(false, "sending_command");
  const bool ok = app->voiceClient_.sendRecognizedText(text, message, &audioUrl);
  if (ok) {
    bool spoken = false;
    if (message != "Voice command sent") {
      app->handleDjResponseText(message, audioUrl, spoken);
    } else if (!audioUrl.isEmpty()) {
      spoken = app->djAudio_.play(audioUrl).spoken;
    }
  } else if (app->voiceClient_.pairingInvalidated()) {
    app->markHomeAssistantPairingInvalid(message);
  }
  app->voiceState_ = VoiceState::Idle;
  app->voiceRecording_ = false;
  app->notice_.visibleUntil = 0;
  app->djResponseOverlayVisible_ = false;
  app->display_.resetDjResponseOverlayCache();
  app->renderNow();
  app->voiceClient_.sendStatus(false, ok ? "idle" : "error", ok ? "" : message);
  return ok;
}

void SpotifyDJApp::wakeWordDetectedCallback(void *context) {
  if (context == nullptr) {
    return;
  }
  SpotifyDJApp *app = static_cast<SpotifyDJApp *>(context);
  if (app->voiceRecording_ || app->voiceState_ != VoiceState::Idle || !app->homeAssistantPaired_) {
    return;
  }
  AppLog.println("Wake word: starting push-to-talk flow");
  app->display_.wakeForUserActivity();
  app->handleVoiceButton();
}

void SpotifyDJApp::softResetCueCallback(void *context) {
  auto *app = static_cast<SpotifyDJApp *>(context);
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

void SpotifyDJApp::refreshFromWebCallback(void *context) {
  static_cast<SpotifyDJApp *>(context)->refreshPlaybackAndBattery();
}

void SpotifyDJApp::resetPairingFromWebCallback(void *context) {
  static_cast<SpotifyDJApp *>(context)->resetHomeAssistantPairing();
}

void SpotifyDJApp::hardResetFromWebCallback(void *context) {
  static_cast<SpotifyDJApp *>(context)->hardResetToProvisioning();
}

bool SpotifyDJApp::djResponseCallback(void *context, const String &text, const String &audioUrl, bool &spoken, String &audioType) {
  SpotifyDJApp *app = static_cast<SpotifyDJApp *>(context);
  const bool displayed = app->handleDjResponseText(text, audioUrl, spoken);
  audioType = audioUrl.isEmpty() ? "none" : app->lastDjAudioType_;
  return displayed;
}

void SpotifyDJApp::languageProvisionedCallback(void *context, const String &languageCode) {
  if (context != nullptr) {
    static_cast<SpotifyDJApp *>(context)->applyProvisionedLanguage(languageCode);
  }
}

void SpotifyDJApp::spotifyProvisionedCallback(void *context) {
  if (context != nullptr) {
    static_cast<SpotifyDJApp *>(context)->applyProvisionedSpotifyCredentials();
  }
}

bool SpotifyDJApp::deviceCommandCallback(void *context, const DeviceCommand &command, String &message) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  return static_cast<SpotifyDJApp *>(context)->handleDeviceCommand(command, message);
}

void SpotifyDJApp::directPairCallback(void *context) {
  if (context == nullptr) {
    return;
  }
  static_cast<SpotifyDJApp *>(context)->noteDirectPairingReceived();
}

void SpotifyDJApp::noteDirectPairingReceived() {
  homeAssistantPaired_ = true;
  haPairingPendingValidation_ = true;
  playback_.error = "";
  spotify_.reloadCredentials();
  if (haPairingScreenActive_) {
    haPairingScreenActive_ = false;
    haPairingStartedAt_ = 0;
  }
  lastHaStatusAt_ = Logic::forceImmediatePollTimestamp();
  lastPlaybackPollAt_ = Logic::forceImmediatePollTimestamp();
  showNotice(I18n::text("boot_paired"), 1500);
  renderNow();
}

bool SpotifyDJApp::handleDjResponseText(const String &text, const String &audioUrl, bool &spoken) {
  if (text.isEmpty()) {
    return false;
  }
  spoken = false;
  diagnostics_.lastDjText = text;
  lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "unknown";
  AppLog.print("DJ response displayed chars=");
  AppLog.println(text.length());
  display_.resetDjResponseOverlayCache();
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + 6000;
  display_.wakeForUserActivity();
  renderNow();
  if (!audioUrl.isEmpty()) {
    const DjResponseAudioResult audioResult = djAudio_.play(audioUrl);
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

void SpotifyDJApp::renderMenuNow() {
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
          {String(I18n::text("spotify_play_mode")) + " " + playModeLabel(currentPlayModeValue(playback_))},
          {I18n::text("settings")},
          {I18n::text("about")},
          {I18n::text("logs")},
          {I18n::text("pong")},
      };
      display_.renderMenuList(I18n::text("menu"), items, RootMenuItemCount, rootMenuSelection_, notice_);
      break;
    }

    case UiScreen::SoundOutputs: {
      MenuItemView items[8];
      items[0].label = I18n::text("none");
      items[1].label = "iPhone";
      size_t itemCount = SpotifyDJMenuModel::FixedSoundOutputCount;
      if (deviceList_.available && deviceList_.count > 0) {
        const size_t maxDevices = min(deviceList_.count, SpotifyDJMenuModel::MaxVisibleOutputs);
        for (size_t index = 0; index < maxDevices; index++) {
          items[index + SpotifyDJMenuModel::FixedSoundOutputCount].label = deviceList_.devices[index].active ? "* " : "  ";
          items[index + SpotifyDJMenuModel::FixedSoundOutputCount].label += deviceList_.devices[index].name;
        }
        itemCount += maxDevices;
      }
      display_.renderMenuList(I18n::text("outputs"), items, itemCount, soundOutputSelection_, notice_);
      break;
    }

    case UiScreen::Queue: {
      MenuItemView items[5];
      size_t itemCount = queue_.count;
      if (!queue_.available || itemCount == 0) {
        items[0].label = queue_.error.isEmpty() ? I18n::text("queue_empty") : queue_.error;
        itemCount = 1;
      } else {
        for (size_t index = 0; index < itemCount; index++) {
          items[index].label = queue_.items[index].title;
          if (!queue_.items[index].subtitle.isEmpty()) {
            items[index].label += " - " + queue_.items[index].subtitle;
          }
        }
      }
      display_.renderMenuList(I18n::text("up_next"), items, itemCount, 0, notice_);
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
      display_.renderPongScreen(pongPaddleY_, pongBallX_, pongBallY_, pongScore_, millis() < pongMissFlashUntil_, notice_);
      break;

    case UiScreen::Settings: {
      MenuItemView items[] = {
          {String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%"},
          {String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_)},
          {String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_)},
          {String(I18n::text("language")) + " " + languageLabel(language_)},
          {String(I18n::text("theme")) + " " + themeLabel(themeCode_)},
          {String(I18n::text("log_level")) + " " + logLevelLabel(logLevel_)},
          {String(I18n::text("audio_feedback")) + " " + I18n::onOff(volumeFeedbackEnabled_)},
          {String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%"},
          {String(I18n::text("stress_test")) + " " + I18n::onOff(stressTestActive_)},
          {I18n::text("turn_off_device")},
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

    case UiScreen::PlayMode: {
      MenuItemView items[PlayModeOptionCount];
      const String currentMode = currentPlayModeValue(playback_);
      for (size_t index = 0; index < PlayModeOptionCount; index++) {
        const String mode = playModeValue(index);
        items[index].label = playModeLabel(mode);
        if (mode == currentMode) {
          items[index].label += " " + String(I18n::text("selected"));
        }
      }
      display_.renderMenuList(
          I18n::text("spotify_play_mode"),
          items,
          PlayModeOptionCount,
          playModeSelection_,
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

void SpotifyDJApp::showNotice(const String &message, uint32_t ttlMs) {
  notice_.show(message, ttlMs);
}

int SpotifyDJApp::displayedVolume() const {
  return pendingVolume_ >= 0 ? pendingVolume_ : playback_.volume;
}

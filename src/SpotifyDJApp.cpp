// Top-level application orchestration.
// This file wires together input, Spotify, display, battery, LED ring, and periodic refresh timing.
#include "SpotifyDJApp.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <cstring>
#include <time.h>

#include "AppLog.h"
#include "Config.h"
#include "LogicHelpers.h"
#include "assets/spotifydj_favicon_ico.h"
#include "assets/spotifydj_icon_192_png.h"

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

using namespace SpotifyDJMenu;

void SpotifyDJApp::begin() {
  pinMode(Config::DisplayBacklightPin, OUTPUT);
  digitalWrite(Config::DisplayBacklightPin, LOW);

  Serial.begin(115200);
  AppLog.begin();
  AppLog.print("SpotifyDJ ");
  AppLog.print(Config::AppVersion);
  AppLog.print(" / ");
  AppLog.print(Config::AppVersionNumber);
  AppLog.println(" booting");
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
  softResetMonitor_.begin(battery_);
  albumArt_.begin();
  ledRing_.begin();
  sound_.begin();
  djAudio_.begin(sound_);
  voiceRecorder_.begin();
  wakeWord_.begin();
  wakeWord_.setCallback(wakeWordDetectedCallback, this);
  assistClient_.begin(haDevice_);
  voiceClient_.begin(haDevice_);
  homeAssistantPaired_ = haDevice_.isPaired();
  loadProvisioning();
  sound_.playStartup();
  spotify_.begin();

  if (shouldStartProvisioningPortal()) {
    runCaptivePortal();
    return;
  }

  if (updateLowBatteryGuard()) {
    return;
  }

  ledRing_.playBootBounce();
  display_.showBootMessage(I18n::text("boot_booting"), battery_);
  connectWiFi(Config::WifiConnectTimeoutMs, true);

  if (WiFi.status() == WL_CONNECTED) {
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
    display_.showBootMessage(I18n::text("boot_authorizing_spotify"), battery_);
    if (spotify_.authorize()) {
      showNotice(I18n::text("spotify_connected"));
      lastPlaybackPollAt_ = millis();
      spotify_.refreshPlayback();
      refreshMqttControlLists();
    }
    sendHomeAssistantStatusIfDue(true);
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
  homeAssistantPaired_ = haDevice_.isPaired();

  if (handleHomeAssistantPairingMode(loopStartedAt)) {
    return;
  }

  // Main loop order keeps input responsive, then drains async work, then performs slower polling.
  const InputEvents events = input_.poll();
  handleInputEvents(events);
  if (djResponseOverlayVisible_ && static_cast<int32_t>(millis() - djResponseOverlayUntil_) >= 0) {
    djResponseOverlayVisible_ = false;
    renderNow();
  }
  if (voiceRecording_) {
    uint8_t audioChunk[Config::VoicePcmChunkBytes];
    size_t bytesRead = 0;
    if (!voiceRecorder_.readPcmChunk(audioChunk, sizeof(audioChunk), bytesRead)) {
      const String error = voiceRecorder_.error();
      voiceRecorder_.abort();
      assistClient_.close();
      voiceRecording_ = false;
      voiceState_ = VoiceState::Error;
      voiceClient_.sendStatus(false, "error", error);
      showNotice(error, 3000);
      renderNow();
    } else if (bytesRead > 0 && !assistClient_.sendAudio(audioChunk, bytesRead)) {
      const String error = "Assist audio failed";
      voiceRecorder_.abort();
      assistClient_.close();
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
  syncHomeAssistantMqttSettings();
  mqttPublisher_.setDeviceFlags(homeAssistantPaired_, haDevice_.isSpotifyConfigured());
  mqttPublisher_.loop();
  processMqttCommands();
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
  mqttSettings_ = settings.mqtt;
  screenOffTimeoutMs_ = settings.screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = settings.deviceSleepTimeoutMs;
  screenBrightnessPercent_ = settings.screenBrightnessPercent;
  language_ = settings.language;
  I18n::setLanguage(language_);
  languageCode_ = I18n::languageCode();
  themeCode_ = settings.themeCode;
  speakerVolumePercent_ = settings.speakerVolumePercent;
  volumeFeedbackEnabled_ = settings.volumeFeedbackEnabled;
  setupModeRequested_ = settings.setupModeRequested;

  const MqttSettings haMqttSettings = haDevice_.getMqttSettings();
  if (!haMqttSettings.host.isEmpty()) {
    mqttSettings_ = haMqttSettings;
  }

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
    while (nextSelection < 0) {
      nextSelection += WifiFailureOptionCount;
    }
    wifiFailureSelection_ = static_cast<size_t>(nextSelection) % WifiFailureOptionCount;
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
  notice.show("Center = select", 1000);
  MenuItemView items[WifiFailureOptionCount] = {
      {"Retry connect"},
      {"Hard reset"},
      {"Reset device"},
      {"Turn off"},
  };
  display_.renderMenuList("WiFi failed", items, WifiFailureOptionCount, wifiFailureSelection_, notice);
}

void SpotifyDJApp::applyWifiConnectFailureSelection() {
  if (volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }

  if (wifiFailureSelection_ == 0) {
    AppLog.println("WiFi recovery: retry connect selected");
    wifiConnectFailed_ = false;
    wifiConnectFailedAt_ = 0;
    showNotice("Retry WiFi", 1500);
    if (connectWiFi(Config::WifiConnectTimeoutMs, true) && WiFi.status() == WL_CONNECTED) {
      startWebPortalIfNeeded();
      display_.showBootMessage(I18n::text("boot_authorizing_spotify"), battery_);
      if (spotify_.authorize()) {
        showNotice(I18n::text("spotify_connected"));
        lastPlaybackPollAt_ = millis();
        spotify_.refreshPlayback();
        refreshMqttControlLists();
      }
      renderNow();
    } else {
      renderWifiConnectFailureMenu();
    }
    return;
  }

  if (wifiFailureSelection_ == 1) {
    AppLog.println("WiFi recovery: factory reset selected");
    hardResetToProvisioning();
    return;
  }

  if (wifiFailureSelection_ == 2) {
    AppLog.println("WiFi recovery: restart selected");
    display_.showBootMessage(I18n::text("restarting"), battery_);
    responsiveDelay(300);
    ESP.restart();
    return;
  }

  AppLog.println("WiFi recovery: turn off selected");
  display_.showBootMessage(I18n::text("turning_off"), battery_);
  responsiveDelay(300);
  enterDeepSleep();
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
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    responsiveDelay(250);
    AppLog.print(".");
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
      mqttPublisher_,
      mqttSettings_,
      screenBrightnessPercent_,
      speakerVolumePercent_,
      homeAssistantPaired_,
      languageCode_,
      themeCode_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_,
      this,
      applyWebSettingsCallback,
      applyWebMqttSettingsCallback,
      applyWebWifiSettingsCallback,
      sendWebVoiceTextCallback,
      repairSpotifyCredentialsFromWebCallback,
      refreshFromWebCallback,
      resetPairingFromWebCallback,
      hardResetFromWebCallback);

  setupHomeAssistantLayer();

  mqttPublisher_.begin(
      haDevice_.getDeviceId(),
      mqttSettings_,
      playback_,
      battery_,
      deviceList_,
      playlists_,
      diagnostics_,
      visualState_,
      screenBrightnessPercent_,
      speakerVolumePercent_,
      languageCode_,
      themeCode_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_);
}

bool SpotifyDJApp::syncClock() {
#if SPOTIFY_ALLOW_INSECURE_TLS
  return true;
#else
  // TLS certificate validation needs a sane wall clock on ESP32.
  display_.showBootMessage(I18n::text("boot_syncing_clock"), battery_);
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

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
  AppLog.print("Clock: ");
  AppLog.println(ctime(&now));
  return true;
#endif
}

void SpotifyDJApp::runCaptivePortal() {
  display_.wakeForUserActivity();
  display_.forceBacklightPercent(100);
  display_.showBootMessage(String(I18n::text("boot_setup_device")) + "\n" + I18n::text("boot_connect_setup_wifi") + " " + String(Config::ProvisioningApSsid), battery_);
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
  String formClientId;
  String formRefreshToken;
  String formSpotifyMarket = "NL";
  MqttSettings formMqtt;
  formMqtt.port = 1883;

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid, formClientId, formRefreshToken, formSpotifyMarket, formMqtt));
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
    String clientId = server.arg("clientId");
    String refreshToken = server.arg("refreshToken");
    String spotifyMarket = server.arg("spotifyMarket");
    ssid.trim();
    clientId.trim();
    refreshToken.trim();
    spotifyMarket.trim();
    formSsid = ssid;
    formClientId = clientId;
    formRefreshToken = refreshToken;
    formSpotifyMarket = spotifyMarket.isEmpty() ? "NL" : spotifyMarket;
    MqttSettings submittedMqtt;
    submittedMqtt.host = server.arg("mqttHost");
    submittedMqtt.host.trim();
    submittedMqtt.port = server.arg("mqttPort").toInt() > 0 ? server.arg("mqttPort").toInt() : 1883;
    submittedMqtt.username = server.arg("mqttUser");
    submittedMqtt.password = server.arg("mqttPass");
    submittedMqtt.enabled = !submittedMqtt.host.isEmpty();
    formMqtt = submittedMqtt;

    if (ssid.isEmpty()) {
      portalMessage = "SSID is required.";
      portalError = true;
      server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid, formClientId, formRefreshToken, formSpotifyMarket, formMqtt));
      return;
    }
    if ((!clientId.isEmpty() && refreshToken.isEmpty()) || (clientId.isEmpty() && !refreshToken.isEmpty())) {
      portalMessage = "Spotify client ID and refresh token must both be filled, or both left empty.";
      portalError = true;
      server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid, formClientId, formRefreshToken, formSpotifyMarket, formMqtt));
      return;
    }

    String message;
    if (testAndSaveProvisioning(ssid, password, clientId, refreshToken, spotifyMarket, submittedMqtt, message)) {
      server.send(200, "text/html", captivePortalPage(message, false, formSsid, formClientId, formRefreshToken, formSpotifyMarket, formMqtt));
      responsiveDelay(1500);
      ESP.restart();
      return;
    }

    portalMessage = message;
    portalError = true;
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError, formSsid, formClientId, formRefreshToken, formSpotifyMarket, formMqtt));
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
      display_.showBootMessage(String(I18n::text("boot_device_setup")) + "\n" + I18n::text("boot_connect_setup_wifi") + " \"" + String(Config::ProvisioningApSsid) + "\"", battery_);
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
  return testAndSaveProvisioning(ssid, password, "", "", "NL", MqttSettings(), message);
}

String SpotifyDJApp::captivePortalPage(
    const String &message,
    bool error,
    const String &ssid,
    const String &clientId,
    const String &refreshToken,
    const String &spotifyMarket,
    const MqttSettings &mqttSettings) const {
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
  page += F("<label>Spotify client ID<input name='clientId' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' value=\"");
  page += escaped(clientId);
  page += F("\"></label>");
  (void)refreshToken;
  page += F("<label>Spotify refresh token<input name='refreshToken' type='password' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false'></label>");
  page += F("<label>Spotify market<input name='spotifyMarket' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' value=\"");
  page += escaped(spotifyMarket.isEmpty() ? "NL" : spotifyMarket);
  page += F("\"></label>");
  page += F("<label>MQTT host<input name='mqttHost' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' placeholder='HA IP or homeassistant.local' value=\"");
  page += escaped(mqttSettings.host);
  page += F("\"></label>");
  page += F("<label>MQTT port<input name='mqttPort' inputmode='numeric' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' value=\"");
  page += String(mqttSettings.port == 0 ? 1883 : mqttSettings.port);
  page += F("\"></label>");
  page += language_ == Language::Dutch
              ? F("<label>MQTT gebruikersnaam<input name='mqttUser' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' value=\"")
              : F("<label>MQTT username<input name='mqttUser' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' value=\"");
  page += escaped(mqttSettings.username);
  page += F("\"></label>");
  page += language_ == Language::Dutch
              ? F("<label>MQTT wachtwoord<input name='mqttPass' type='password' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false'></label>")
              : F("<label>MQTT password<input name='mqttPass' type='password' autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false'></label>");
  page += language_ == Language::Dutch
              ? F("<button type='submit'>Opslaan &amp; verbinden</button></form>")
              : F("<button type='submit'>Save &amp; Connect</button></form>");
  page += F("</main></body></html>");
  return page;
}

bool SpotifyDJApp::testAndSaveProvisioning(
    const String &ssid,
    const String &password,
    const String &clientId,
    const String &refreshToken,
    const String &spotifyMarket,
    const MqttSettings &mqttSettings,
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

  const bool hasSpotifyCredentials = !clientId.isEmpty() && !refreshToken.isEmpty();
  if (hasSpotifyCredentials) {
    display_.showBootMessage(I18n::text("boot_testing_spotify"), battery_);
    spotify_.useCredentialsForProvisioning(clientId, refreshToken);
    if (!spotify_.authorize()) {
      message = playback_.error.isEmpty() ? I18n::text("spotify_authorization_failed_sentence") : playback_.error;
      WiFi.disconnect(false);
      return false;
    }
  }

  provisioning_.saveSetupProvisioning(ssid, password, clientId, refreshToken, spotifyMarket, mqttSettings);

  wifiSsid_ = ssid;
  wifiPassword_ = password;
  if (!mqttSettings.host.isEmpty()) {
    mqttSettings_ = mqttSettings;
    haDevice_.saveMqttSettings(mqttSettings);
  }
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

  if (activeScreen_ == UiScreen::AlbumArt && events.encoderLongClick) {
    AppLog.println("voice: encoder long press from current song");
    handleVoiceButton();
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
    AppLog.println("voice: encoder released without active PTT");
  }

  if (events.encoderClick) {
    if (playback_.hasPlayback) {
      pauseOrResume();
    } else {
      startLikedProxyPlaylist();
    }
  }

  if (events.encoderDoubleClick) {
    if (playback_.hasPlayback) {
      openAlbumArtScreen();
    } else {
      showNotice(I18n::text("no_current_song"), 1800);
      renderNow();
    }
  }

  if (events.topButtonClick) {
    goToNextTrack();
  }

  if (events.topButtonDoubleClick) {
    goToPreviousTrack();
  }

  if (events.encoderLongClick) {
    AppLog.println("voice: encoder long press");
    handleVoiceButton();
  }

  if (events.topButtonLongClick) {
    openRootMenu();
  }

  // Holding the top button for the longer reset threshold is still handled by SoftResetMonitor.
}

void SpotifyDJApp::handleMenuInputEvents(const InputEvents &events) {
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
    if (activeScreen_ == UiScreen::AlbumArt) {
      goBackOneScreen();
      return;
    }
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
        openScreen(UiScreen::Queue);
        showNotice(I18n::text("loading_queue"), 1200);
        spotify_.refreshQueue(queue_);
        renderNow();
      } else if (rootMenuSelection_ == 1) {
        openScreen(UiScreen::Playlists);
        playlistSelection_ = 0;
        showNotice(I18n::text("loading_playlists"), 1200);
        spotify_.refreshPlaylists(playlists_);
        renderNow();
      } else if (rootMenuSelection_ == 2) {
        openScreen(UiScreen::SoundOutputs);
        soundOutputSelection_ = 0;
        showNotice(I18n::text("loading_outputs"), 1200);
        spotify_.refreshDevices(deviceList_);
        renderNow();
      } else if (rootMenuSelection_ == 3) {
        openScreen(UiScreen::Settings);
      } else if (rootMenuSelection_ == 4) {
        openScreen(UiScreen::About);
      } else if (rootMenuSelection_ == 5) {
        openScreen(UiScreen::Logs);
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
        volumeFeedbackEnabled_ = !volumeFeedbackEnabled_;
        saveDisplaySettings();
        AppLog.print("Settings: audio feedback ");
        AppLog.println(volumeFeedbackEnabled_ ? "enabled" : "disabled");
        showNotice(String(I18n::text("audio_feedback")) + " " + I18n::onOff(volumeFeedbackEnabled_), 1600);
        renderNow();
      } else if (settingsSelection_ == 6) {
        openScreen(UiScreen::SpeakerVolume);
      } else if (settingsSelection_ == 7) {
        playModeSelection_ = 0;
        const String currentMode = currentPlayModeValue(playback_);
        for (size_t index = 0; index < PlayModeOptionCount; index++) {
          if (playModeValue(index) == currentMode) {
            playModeSelection_ = index;
            break;
          }
        }
        openScreen(UiScreen::PlayMode);
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

void SpotifyDJApp::applyPlayModeSelection() {
  const String mode = playModeValue(playModeSelection_);
  AppLog.print("Spotify: setting play mode ");
  AppLog.println(mode);
  showNotice(I18n::text("spotify_play_mode"), 1600);
  renderNow();
  if (spotify_.setPlayMode(mode)) {
    AppLog.print("Spotify: play mode set to ");
    AppLog.println(mode);
    showNotice(playModeLabel(mode), 2200);
    lastPlaybackPollAt_ = 0;
  } else {
    AppLog.print("Spotify: play mode failed: ");
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
    AppLog.println("[SpotifyDJ] local LittleFS caches cleared");
  }

  WiFi.disconnect(true, true);
  responsiveDelay(600);
  ESP.restart();
}

void SpotifyDJApp::resetHomeAssistantPairing() {
  AppLog.println("Home Assistant: clearing pairing and restarting to pairing mode");
  display_.showBootMessage(I18n::text("boot_reset_pairing"), battery_);
  haDevice_.clearHomeAssistantPairing();
  responsiveDelay(350);
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

void SpotifyDJApp::processMqttCommands() {
  MqttCommand command;
  while (mqttPublisher_.pollCommand(command)) {
    handleMqttCommand(command);
  }
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

void SpotifyDJApp::handleMqttCommand(const MqttCommand &command) {
  if (command.type == MqttCommandType::Status) {
    AppLog.println("MQTT command: publish status");
    mqttPublisher_.requestStatusPublish();
    return;
  }
  if (command.type == MqttCommandType::Ota) {
    AppLog.println("MQTT command ignored: OTA command requires HA device OTA endpoint payload");
    return;
  }
  if (command.type == MqttCommandType::DjResponse) {
    AppLog.println("MQTT command: DJ response");
    bool spoken = false;
    const bool displayed = handleDjResponseText(command.value, "", spoken);
    mqttPublisher_.publishDjResponseEvent(spoken, displayed);
    return;
  }
  if (command.type == MqttCommandType::ScreenBrightness) {
    screenBrightnessPercent_ = constrain(command.numericValue, 25, 100);
    for (size_t index = 0; index < BrightnessOptionCount; index++) {
      if (brightnessValuePercent(index) == screenBrightnessPercent_) {
        brightnessSelection_ = index;
      }
    }
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("MQTT command: screen brightness ");
    AppLog.println(screenBrightnessPercent_);
    showNotice(String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%", 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }
  if (command.type == MqttCommandType::ScreenDimTimeout) {
    screenOffTimeoutMs_ = constrain(static_cast<uint32_t>(command.numericValue) * 1000UL, 30000UL, 240000UL);
    for (size_t index = 0; index < DimTimeoutOptionCount; index++) {
      if (dimTimeoutValueMs(index) == screenOffTimeoutMs_) {
        dimTimeoutSelection_ = index;
      }
    }
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("MQTT command: screen dim timeout ");
    AppLog.println(screenOffTimeoutMs_);
    showNotice(String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_), 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }
  if (command.type == MqttCommandType::DeepSleepTimeout) {
    deviceSleepTimeoutMs_ = constrain(static_cast<uint32_t>(command.numericValue) * 60000UL, 300000UL, 3600000UL);
    sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
    saveDisplaySettings();
    AppLog.print("MQTT command: turn off after ");
    AppLog.println(deviceSleepTimeoutMs_);
    showNotice(String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_), 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }
  if (command.type == MqttCommandType::SpeakerVolume) {
    speakerVolumePercent_ = constrain(command.numericValue, 25, 100);
    for (size_t index = 0; index < SpeakerVolumeOptionCount; index++) {
      if (speakerVolumeValuePercent(index) == speakerVolumePercent_) {
        speakerVolumeSelection_ = index;
      }
    }
    sound_.setVolumePercent(speakerVolumePercent_);
    saveDisplaySettings();
    AppLog.print("MQTT command: speaker volume ");
    AppLog.println(speakerVolumePercent_);
    showNotice(String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%", 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }
  if (command.type == MqttCommandType::Language) {
    language_ = I18n::languageFromCode(command.value);
    I18n::setLanguage(language_);
    languageCode_ = I18n::languageCode();
    languageSelection_ = language_ == Language::Dutch ? 1 : 0;
    saveDisplaySettings();
    AppLog.print("MQTT command: language ");
    AppLog.println(languageCode_);
    showNotice(String(I18n::text("language")) + " " + languageLabel(language_), 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }
  if (command.type == MqttCommandType::Theme) {
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
    AppLog.print("MQTT command: theme ");
    AppLog.println(themeCode_);
    showNotice(String(I18n::text("theme")) + " " + themeLabel(themeCode_), 1800);
    mqttPublisher_.requestPublish();
    renderNow();
    return;
  }

  if (!spotify_.isAuthorized()) {
    AppLog.println("MQTT command ignored: Spotify not connected");
    return;
  }

  switch (command.type) {
    case MqttCommandType::Next:
      AppLog.println("MQTT command: next song");
      goToNextTrack();
      break;

    case MqttCommandType::Previous:
      AppLog.println("MQTT command: previous song");
      goToPreviousTrack();
      break;

    case MqttCommandType::Status:
    case MqttCommandType::Ota:
    case MqttCommandType::DjResponse:
    case MqttCommandType::ScreenBrightness:
    case MqttCommandType::ScreenDimTimeout:
    case MqttCommandType::DeepSleepTimeout:
    case MqttCommandType::SpeakerVolume:
    case MqttCommandType::Language:
    case MqttCommandType::Theme:
      break;

    case MqttCommandType::Volume:
      if (!playback_.hasPlayback || !playback_.supportsVolume) {
        AppLog.println("MQTT command ignored: volume unavailable");
        return;
      }
      AppLog.print("MQTT command: volume ");
      AppLog.println(command.numericValue);
      if (spotify_.queueVolume(constrain(command.numericValue, 0, Config::MaxSpotifyVolumePercent))) {
        playback_.volume = constrain(command.numericValue, 0, Config::MaxSpotifyVolumePercent);
        playback_.error = "";
        mqttPublisher_.requestPublish();
        renderNow();
      }
      break;

    case MqttCommandType::TransferOutput:
      AppLog.print("MQTT command: transfer output ");
      AppLog.println(command.value);
      if (!transferToOutputByNameOrId(command.value)) {
        AppLog.print("MQTT command failed: ");
        AppLog.println(playback_.error);
      }
      break;

    case MqttCommandType::StartPlaylist:
      AppLog.print("MQTT command: start playlist ");
      AppLog.println(command.value);
      if (!startPlaylistByNameOrUri(command.value)) {
        AppLog.print("MQTT command failed: ");
        AppLog.println(playback_.error);
      }
      break;

    case MqttCommandType::None:
      break;
  }
}

void SpotifyDJApp::pauseOrResume() {
  const uint32_t now = millis();
  if (now - lastPauseToggleAt_ < 900) {
    return;
  }
  lastPauseToggleAt_ = now;

  // Refresh first so a stale paused/playing bit does not invert the user's command.
  showNotice(I18n::text("checking_playback"), 1200);
  renderNow();
  spotify_.refreshPlayback();

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
  AppLog.println("Spotify: starting Liked Proxy playlist");
  showNotice(I18n::text("starting_liked_proxy"), 1600);
  renderNow();
  if (spotify_.startLikedProxyPlaylist()) {
    AppLog.println("Spotify: Liked Proxy playlist command accepted");
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
    AppLog.print("Spotify: Liked Proxy playlist failed: ");
    AppLog.println(playback_.error);
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::handleVoiceButton() {
  if (!homeAssistantPaired_) {
    AppLog.println("[SpotifyDJ] voice: HA not paired, using pause/resume");
    pauseOrResume();
    return;
  }
  if (voiceRecording_) {
    AppLog.println("[SpotifyDJ] voice: stop requested while recording");
    stopVoiceRecordingAndSendText();
    return;
  }

#if defined(SPOTIFYDJ_DEBUG_TEXT_COMMAND)
  AppLog.println("[SpotifyDJ] voice: debug text command");
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
  AppLog.println("[SpotifyDJ] voice: connecting Assist websocket");
  showNotice(I18n::text("voice_connecting"), 3000);
  renderNow();
  if (!assistClient_.start(message)) {
    AppLog.print("[SpotifyDJ] voice: Assist start failed: ");
    AppLog.println(message);
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", message);
    if (message == "Assist auth_invalid") {
      markHomeAssistantPairingInvalid(I18n::text("ha_pairing_invalid"));
      return;
    }
    showNotice(message, 3500);
    renderNow();
    return;
  }
  if (!voiceRecorder_.startRaw()) {
    const String error = voiceRecorder_.error();
    AppLog.print("[SpotifyDJ] voice: mic start failed: ");
    AppLog.println(error);
    assistClient_.close();
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showNotice(error, 3000);
    renderNow();
    return;
  }
  voiceRecording_ = true;
  voiceState_ = VoiceState::Listening;
  AppLog.println("[SpotifyDJ] voice: listening");
  if (volumeFeedbackEnabled_) {
    sound_.playPttStart();
  }
  ledRing_.playPulse(CRGB::Yellow);
  voiceClient_.sendStatus(true, "recording");
  mqttPublisher_.publishEvent("button", "middle", "push_to_talk_start");
  diagnostics_.lastDjText = I18n::text("voice_listening");
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
  AppLog.println("[SpotifyDJ] voice: recording stopped, waiting for STT result");
  if (volumeFeedbackEnabled_) {
    sound_.playPttStop();
  }
  ledRing_.playPulse(CRGB::Blue);
  diagnostics_.lastDjText = I18n::text("voice_processing");
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + 3000;
  showNotice(I18n::text("voice_processing"), 3000);
  renderNow();
  if (!voiceRecorder_.stopRaw()) {
    const String error = voiceRecorder_.error();
    AppLog.print("[SpotifyDJ] voice: mic stop failed: ");
    AppLog.println(error);
    assistClient_.close();
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", error);
    showNotice(error, 3500);
    renderNow();
    return;
  }

  String recognizedText;
  String message;
  if (!assistClient_.finish(recognizedText, message)) {
    AppLog.print("[SpotifyDJ] voice: STT failed: ");
    AppLog.println(message);
    voiceState_ = VoiceState::Error;
    voiceClient_.sendStatus(false, "error", message);
    showNotice(message, 3500);
    renderNow();
    return;
  }

  voiceState_ = VoiceState::SendingCommand;
  AppLog.print("[SpotifyDJ] voice: recognized text chars=");
  AppLog.println(recognizedText.length());
  AppLog.println("[SpotifyDJ] voice: sending text command to HA integration");
  voiceClient_.sendStatus(false, "sending_command");
  showNotice("SpotifyDJ...", 2500);
  renderNow();
  String audioUrl;
  if (voiceClient_.sendRecognizedText(recognizedText, message, &audioUrl)) {
    voiceState_ = VoiceState::Done;
    AppLog.println("[SpotifyDJ] voice: command accepted");
    ledRing_.playPulse(CRGB::Green);
    voiceClient_.sendStatus(false, "idle");
    bool spoken = false;
    if (message != "Voice command sent") {
      handleDjResponseText(message, audioUrl, spoken);
    } else if (!audioUrl.isEmpty()) {
      spoken = djAudio_.play(audioUrl).spoken;
      showNotice(spoken ? I18n::text("voice_response_played") : I18n::text("voice_response_audio_failed"), 3500);
    } else {
      showNotice(recognizedText, 3500);
    }
  } else {
    voiceState_ = VoiceState::Error;
    AppLog.print("[SpotifyDJ] voice: command failed: ");
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
  if (!playlists_.available || playlists_.count == 0 || playlistSelection_ >= playlists_.count) {
    showNotice(playlists_.error.isEmpty() ? I18n::text("no_playlists") : playlists_.error.c_str(), 3000);
    renderNow();
    return;
  }

  const PlaylistItemState &playlist = playlists_.items[playlistSelection_];
  AppLog.print("Spotify: starting playlist ");
  AppLog.println(playlist.name);
  showNotice(I18n::text("starting_playlist"), 1600);
  renderNow();
  if (spotify_.startPlaylist(playlist.uri)) {
    AppLog.println("Spotify: playlist command accepted");
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
    AppLog.print("Spotify: playlist failed: ");
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
      mqttPublisher_.requestPublish();
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
      mqttPublisher_.requestPublish();
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
  spotify_.refreshPlayback();
  if (activeScreen_ == UiScreen::AlbumArt) {
    albumArt_.requestCurrentSongArt(playback_);
  }
  lastBatteryPollAt_ = millis();
  lastPlaybackPollAt_ = millis();
  renderNow();
}

void SpotifyDJApp::refreshMqttControlLists() {
  if (!mqttSettings_.enabled || mqttSettings_.host.isEmpty() || !spotify_.isAuthorized()) {
    return;
  }
  AppLog.println("MQTT controls: refreshing Spotify outputs and playlists");
  spotify_.refreshDevices(deviceList_);
  spotify_.refreshPlaylists(playlists_);
  mqttPublisher_.requestPublish();
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
  spotify_.refreshPlayback();
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
  spotify_.refreshQueue(queue_);
  renderNow();
}

void SpotifyDJApp::openSoundOutputsScreen() {
  activeScreen_ = UiScreen::SoundOutputs;
  menuStackSize_ = 0;
  soundOutputSelection_ = 0;
  showNotice(I18n::text("loading_outputs"), 1200);
  renderNow();
  spotify_.refreshDevices(deviceList_);
  renderNow();
}

void SpotifyDJApp::transferToSelectedOutput() {
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
        spotify_.isAuthorized(),
        mqttSettings_.enabled && mqttPublisher_.connected());
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
  if (djResponseOverlayVisible_) {
    display_.renderDjResponseOverlay(diagnostics_.lastDjText);
  }
  mqttPublisher_.requestPublish();
}

bool SpotifyDJApp::dismissDjResponseOverlay() {
  if (!djResponseOverlayVisible_) {
    return false;
  }
  djResponseOverlayVisible_ = false;
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
  mqttPublisher_.requestPublish();
}

bool SpotifyDJApp::connectionHealthy() {
  const bool wifiOk = WiFi.status() == WL_CONNECTED;
  const bool spotifyOk = spotify_.isAuthorized();
  const bool mqttRequired = mqttSettings_.enabled && !mqttSettings_.host.isEmpty();
  const bool mqttOk = !mqttRequired || mqttPublisher_.connected();
  return wifiOk && spotifyOk && mqttOk;
}

AboutStatus SpotifyDJApp::aboutStatus() {
  AboutStatus status;
  status.wifiConnected = WiFi.status() == WL_CONNECTED;
  status.ipAddress = status.wifiConnected ? WiFi.localIP().toString() : "";
  status.webAddress = status.wifiConnected ? "http://" + WiFi.localIP().toString() : "";
  status.haPaired = homeAssistantPaired_;
  status.mqttState = mqttPublisher_.connectionState();
  status.mqttConnected = mqttPublisher_.connected();
  status.spotifyConnected = spotify_.isAuthorized();
  return status;
}

void SpotifyDJApp::updateVisualPower() {
  const bool wasScreenOn = visualState_.screenOn;
  const uint8_t previousBrightness = visualState_.screenBrightnessLevel;
  const bool wasLedOn = visualState_.ledOn;
  display_.updateIdleBrightness();
  const uint8_t currentBacklightPercent = display_.backlightPercent();
  ledRing_.setPowerPercent(currentBacklightPercent);
  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = currentBacklightPercent;
  visualState_.ledOn = ledRing_.isOn();
  if (visualState_.screenOn != wasScreenOn ||
      visualState_.screenBrightnessLevel != previousBrightness ||
      visualState_.ledOn != wasLedOn) {
    mqttPublisher_.requestPublish();
  }
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

  if (activeScreen_ != UiScreen::NowPlaying) {
    activeScreen_ = UiScreen::NowPlaying;
    menuStackSize_ = 0;
  }

  mqttPublisher_.prepareForSleep();
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
    const String &themeCode) {
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
  for (size_t index = 0; index < ThemeOptionCount; index++) {
    if (themeValue(index) == themeCode_) {
      themeSelection_ = index;
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
  sound_.setVolumePercent(speakerVolumePercent_);
  saveDisplaySettings();
  showNotice(I18n::text("web_settings_saved"), 1800);
  renderNow();
  display_.showBootMessage(I18n::text("restarting"), battery_);
  responsiveDelay(450);
  ESP.restart();
}

void SpotifyDJApp::applyWebMqttSettings(const MqttSettings &settings) {
  mqttSettings_ = settings;
  mqttSettings_.host.trim();
  mqttSettings_.enabled = !mqttSettings_.host.isEmpty();

  provisioning_.saveMqttSettings(mqttSettings_);
  if (!mqttSettings_.host.isEmpty()) {
    haDevice_.saveMqttSettings(mqttSettings_);
  }

  mqttPublisher_.begin(
      haDevice_.getDeviceId(),
      mqttSettings_,
      playback_,
      battery_,
      deviceList_,
      playlists_,
      diagnostics_,
      visualState_,
      screenBrightnessPercent_,
      speakerVolumePercent_,
      languageCode_,
      themeCode_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_);
  mqttPublisher_.requestPublish();
}

bool SpotifyDJApp::repairSpotifyCredentialsFromWeb(
    const String &clientId,
    const String &refreshToken,
    const String &market,
    String &message) {
  String submittedClientId = clientId;
  String submittedRefreshToken = refreshToken;
  String submittedMarket = market;
  submittedClientId.trim();
  submittedRefreshToken.trim();
  submittedMarket.trim();

  const String storedClientId = haDevice_.getSpotifyClientId();
  if (!Logic::spotifyRepairCredentialsValid(storedClientId.c_str(), submittedClientId.c_str(), submittedRefreshToken.c_str())) {
    message = storedClientId.isEmpty()
                  ? I18n::text("spotify_client_and_refresh_required")
                  : I18n::text("refresh_token_missing");
    return false;
  }

  const String effectiveClientId = submittedClientId.isEmpty() ? storedClientId : submittedClientId;
  const String effectiveMarket = Logic::spotifyMarketOrDefault(submittedMarket.c_str());
  AppLog.print("Web Spotify repair: client_id=");
  AppLog.print(effectiveClientId.isEmpty() ? "missing" : "present");
  AppLog.print(" market=");
  AppLog.println(effectiveMarket);

  if (!haDevice_.saveSpotifyCredentials(effectiveClientId, submittedRefreshToken, effectiveMarket)) {
    message = "Spotify credentials could not be saved to NVS";
    return false;
  }
  provisioning_.saveSpotifyCredentials(effectiveClientId, submittedRefreshToken, effectiveMarket);
  spotify_.reloadCredentials();
  if (!spotify_.authorize()) {
    message = playback_.error.isEmpty() ? I18n::text("spotify_authorization_failed") : playback_.error;
    return false;
  }

  spotify_.refreshPlayback();
  refreshMqttControlLists();
  showNotice(I18n::text("spotify_connected"), 2500);
  renderNow();
  message = I18n::text("spotify_refresh_saved_ok");
  return true;
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
  AppLog.print("[SpotifyDJ] UI language applied: ");
  AppLog.println(languageCode_);
  mqttPublisher_.requestPublish();
  renderNow();
}

void SpotifyDJApp::applyProvisionedSpotifyCredentials() {
  spotify_.reloadCredentials();
  AppLog.println("[SpotifyDJ] Spotify credentials reloaded after HA provisioning");
  if (!spotify_.isAuthorized() && WiFi.status() == WL_CONNECTED) {
    spotify_.authorize();
  }
  mqttPublisher_.requestPublish();
}

void SpotifyDJApp::syncHomeAssistantMqttSettings() {
  const uint32_t now = millis();
  if (now - lastMqttProvisioningSyncAt_ < 10000UL) {
    return;
  }
  lastMqttProvisioningSyncAt_ = now;

  const MqttSettings settings = haDevice_.getMqttSettings();
  if (settings.host.isEmpty()) {
    return;
  }
  if (settings.host == mqttSettings_.host &&
      settings.port == mqttSettings_.port &&
      settings.username == mqttSettings_.username &&
      settings.password == mqttSettings_.password) {
    return;
  }

  mqttSettings_ = settings;
  mqttSettings_.enabled = true;
  AppLog.print("MQTT provisioned from HA host=");
  AppLog.print(mqttSettings_.host);
  AppLog.print(" port=");
  AppLog.println(mqttSettings_.port);
  mqttPublisher_.begin(
      haDevice_.getDeviceId(),
      mqttSettings_,
      playback_,
      battery_,
      deviceList_,
      playlists_,
      diagnostics_,
      visualState_,
      screenBrightnessPercent_,
      speakerVolumePercent_,
      languageCode_,
      themeCode_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_);
  mqttPublisher_.requestPublish();
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
      languageProvisionedCallback);

  AppLog.print("[SpotifyDJ] paired: ");
  AppLog.println(haDevice_.isPaired() ? "true" : "false");
  if (haDevice_.isPaired()) {
    AppLog.println("[SpotifyDJ] HA URL: configured");
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
  const SpotifyDJPairing::StatusResult result = haPairing_.sendStatusToHA(battery_, haDevice_.isSpotifyConfigured());
  if (result == SpotifyDJPairing::StatusResult::Ok) {
    homeAssistantPaired_ = true;
  } else if (result == SpotifyDJPairing::StatusResult::PairingInvalid) {
    markHomeAssistantPairingInvalid(I18n::text("ha_pairing_invalid"));
  }
}

void SpotifyDJApp::markHomeAssistantPairingInvalid(const String &message) {
  if (homeAssistantPaired_) {
    AppLog.println("Home Assistant: pairing invalid or stale");
  }
  homeAssistantPaired_ = false;
  showNotice(message, 5000);
  renderNow();
}

bool SpotifyDJApp::handleHomeAssistantPairingMode(uint32_t loopStartedAt) {
  if (haDevice_.isPaired()) {
    homeAssistantPaired_ = true;
    if (haPairingScreenActive_) {
      haPairingScreenActive_ = false;
      haPairingStartedAt_ = 0;
      haDevice_.displayPaired();
      responsiveDelay(700);
      display_.showBootMessage(I18n::text("boot_authorizing_spotify"), battery_);
      if (spotify_.authorize()) {
        showNotice(I18n::text("spotify_connected"));
        lastPlaybackPollAt_ = millis();
        spotify_.refreshPlayback();
        refreshMqttControlLists();
      }
      sendHomeAssistantStatusIfDue(true);
      renderNow();
    }
    return false;
  }

  haPairingScreenActive_ = true;
  homeAssistantPaired_ = false;
  if (haPairingStartedAt_ == 0) {
    haPairingStartedAt_ = millis();
  }
  display_.forceBacklightPercent(100);
  webPortal_.handle();
  processPendingWifiSettings();
  haApiServer_.loop();
  mqttPublisher_.loop();

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
    const String &themeCode) {
  static_cast<SpotifyDJApp *>(context)->applyWebSettings(
      brightnessPercent,
      offTimeoutMs,
      sleepTimeoutMs,
      speakerVolumePercent,
      languageCode,
      themeCode);
}

void SpotifyDJApp::applyWebMqttSettingsCallback(void *context, const MqttSettings &settings) {
  static_cast<SpotifyDJApp *>(context)->applyWebMqttSettings(settings);
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
  AppLog.print("[SpotifyDJ] web voice: sending text chars=");
  AppLog.println(text.length());
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
  app->voiceClient_.sendStatus(false, ok ? "idle" : "error", ok ? "" : message);
  return ok;
}

bool SpotifyDJApp::repairSpotifyCredentialsFromWebCallback(
    void *context,
    const String &clientId,
    const String &refreshToken,
    const String &market,
    String &message) {
  if (context == nullptr) {
    message = "App unavailable";
    return false;
  }
  return static_cast<SpotifyDJApp *>(context)->repairSpotifyCredentialsFromWeb(clientId, refreshToken, market, message);
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

bool SpotifyDJApp::handleDjResponseText(const String &text, const String &audioUrl, bool &spoken) {
  if (text.isEmpty()) {
    return false;
  }
  spoken = false;
  diagnostics_.lastDjText = text;
  lastDjAudioType_ = audioUrl.isEmpty() ? "none" : "unknown";
  AppLog.print("[SpotifyDJ] DJ response displayed chars=");
  AppLog.println(text.length());
  djResponseOverlayVisible_ = true;
  djResponseOverlayUntil_ = millis() + 6000;
  display_.wakeForUserActivity();
  renderNow();
  if (!audioUrl.isEmpty()) {
    const DjResponseAudioResult audioResult = djAudio_.play(audioUrl);
    spoken = audioResult.spoken;
    lastDjAudioType_ = audioResult.audioType;
  }
  if (!spoken && volumeFeedbackEnabled_) {
    sound_.playConfirm();
  }
  renderNow();
  mqttPublisher_.requestPublish();
  mqttPublisher_.publishDjResponseEvent(spoken, true);
  return true;
}

void SpotifyDJApp::renderMenuNow() {
  switch (activeScreen_) {
    case UiScreen::AlbumArt:
      display_.renderAlbumArtScreen(playback_, notice_, albumArt_.imagePath(), albumArt_.status());
      break;

    case UiScreen::RootMenu: {
      MenuItemView items[] = {
          {I18n::text("up_next")},
          {I18n::text("playlists")},
          {I18n::text("outputs")},
          {I18n::text("settings")},
          {I18n::text("about")},
          {I18n::text("logs")},
      };
      display_.renderMenuList(I18n::text("menu"), items, 6, rootMenuSelection_, notice_);
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
      const size_t lineCount = AppLog.newestLines(lines, 9);
      lastLogsRenderAt_ = millis();
      display_.renderLogsScreen(lines, lineCount, notice_);
      break;
    }

    case UiScreen::Settings: {
      MenuItemView items[] = {
          {String(I18n::text("brightness")) + " " + String(screenBrightnessPercent_) + "%"},
          {String(I18n::text("dim_timeout")) + " " + dimTimeoutLabel(screenOffTimeoutMs_)},
          {String(I18n::text("deep_sleep_after")) + " " + minuteLabel(deviceSleepTimeoutMs_)},
          {String(I18n::text("language")) + " " + languageLabel(language_)},
          {String(I18n::text("theme")) + " " + themeLabel(themeCode_)},
          {String(I18n::text("audio_feedback")) + " " + I18n::onOff(volumeFeedbackEnabled_)},
          {String(I18n::text("speaker_volume")) + " " + String(speakerVolumePercent_) + "%"},
          {String(I18n::text("spotify_play_mode")) + " " + playModeLabel(currentPlayModeValue(playback_))},
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
          spotify_.isAuthorized(),
          mqttSettings_.enabled && mqttPublisher_.connected());
      break;
  }
}

void SpotifyDJApp::showNotice(const String &message, uint32_t ttlMs) {
  notice_.show(message, ttlMs);
}

int SpotifyDJApp::displayedVolume() const {
  return pendingVolume_ >= 0 ? pendingVolume_ : playback_.volume;
}

// Top-level application orchestration.
// This file wires together input, Spotify, display, battery, LED ring, and periodic refresh timing.
#include "SpotifyDJApp.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>

#include "AppLog.h"
#include "Config.h"
#include "LogicHelpers.h"

#ifndef SPOTIFY_ALLOW_INSECURE_TLS
#define SPOTIFY_ALLOW_INSECURE_TLS 0
#endif

namespace {
static const char *const TopHoldRestartHint = "hold 10s to restart";

String dimTimeoutLabel(uint32_t valueMs) {
  if (valueMs == 30000UL) {
    return "30 seconds";
  }
  if (valueMs == 60000UL) {
    return "1 minute";
  }
  return String(valueMs / 60000UL) + " minutes";
}
}  // namespace

void SpotifyDJApp::begin() {
  Serial.begin(115200);
  AppLog.begin();
  delay(500);

  // Hardware/services are initialized before WiFi so boot messages and recovery controls are available early.
  display_.begin();
  input_.begin();
  batteryMonitor_.begin();
  batteryMonitor_.refresh();
  evaluateBatteryTransition();
  softResetMonitor_.begin(battery_);
  albumArt_.begin();
  ledRing_.begin();
  sound_.begin();
  sound_.playStartup();
  loadProvisioning();
  spotify_.begin();

  if (shouldStartProvisioningPortal()) {
    runCaptivePortal();
    return;
  }

  if (updateLowBatteryGuard()) {
    return;
  }

  ledRing_.playBootBounce();
  display_.showBootMessage("Booting...", battery_);
  connectWiFi(Config::WifiConnectTimeoutMs, true);

  if (WiFi.status() == WL_CONNECTED) {
    startWebPortalIfNeeded();
    display_.showBootMessage("Authorizing Spotify...", battery_);
    if (spotify_.authorize()) {
      showNotice("Spotify authorized");
      lastPlaybackPollAt_ = millis();
      spotify_.refreshPlayback();
    }
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

  if (updateLowBatteryGuard()) {
    recordLoopMetrics(loopStartedAt);
    delay(20);
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
    display_.showBootMessage("Not connected\nEncoder press: retry", battery_);
    ledRing_.setPowerPercent(0);
    recordLoopMetrics(loopStartedAt);
    delay(10);
    return;
  }

  startWebPortalIfNeeded();

  // Main loop order keeps input responsive, then drains async work, then performs slower polling.
  const InputEvents events = input_.poll();
  handleInputEvents(events);
  flushPendingVolume();
  processVolumeResult();
  webPortal_.handle();
  processPendingWifiSettings();
  mqttPublisher_.loop();
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
    const bool artistScrollAdvanced = display_.advanceArtistScrollIfNeeded(playback_, 140, 2);
    if (titleScrollAdvanced || artistScrollAdvanced) {
      renderNow();
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

  recordLoopMetrics(loopStartedAt);
  delay(5);
}

void SpotifyDJApp::loadProvisioning() {
  Preferences preferences;
  preferences.begin("provision", false);
  wifiSsid_ = preferences.getString("ssid", "");
  wifiPassword_ = preferences.getString("pass", "");
  mqttSettings_.host = preferences.getString("mqtt_host", "");
  mqttSettings_.port = preferences.getUInt("mqtt_port", 1883);
  mqttSettings_.username = preferences.getString("mqtt_user", "");
  mqttSettings_.password = preferences.getString("mqtt_pass", "");
  mqttSettings_.enabled = !mqttSettings_.host.isEmpty();
  screenOffTimeoutMs_ = constrain(preferences.getUInt("screen_off_ms", Config::DisplayOffAfterMs), 30000UL, 240000UL);
  deviceSleepTimeoutMs_ = constrain(preferences.getUInt("sleep_ms", Config::DeviceSleepAfterMs), 300000UL, 3600000UL);
  screenBrightnessPercent_ = constrain(preferences.getUInt("screen_bright", 100), 25UL, 100UL);
  setupModeRequested_ = preferences.getBool("setup", false);
  preferences.end();

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
  sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);

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
  if (events.encoderClick) {
    input_.clearPendingButtonActions();
    wifiConnectFailed_ = false;
    wifiConnectFailedAt_ = 0;
    showNotice("Retry WiFi", 1500);
    if (connectWiFi(Config::WifiConnectTimeoutMs, true) && WiFi.status() == WL_CONNECTED) {
      startWebPortalIfNeeded();
      display_.showBootMessage("Authorizing Spotify...", battery_);
      if (spotify_.authorize()) {
        showNotice("Spotify authorized");
        lastPlaybackPollAt_ = millis();
        spotify_.refreshPlayback();
      }
      renderNow();
    }
  }

  if (!deepSleepStarted_ && wifiConnectFailedAt_ != 0 && millis() - wifiConnectFailedAt_ >= Config::WifiFailureSleepAfterMs) {
    enterDeepSleep();
  }

  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
  recordLoopMetrics(loopStartedAt);
  delay(10);
}

bool SpotifyDJApp::connectWiFi(uint32_t timeoutMs, bool bootScreen) {
  if (bootScreen) {
    display_.forceBacklightPercent(100);
  } else {
    display_.wakeForUserActivity();
  }
  display_.showBootMessage("Connecting WiFi...", battery_);
  if (wifiSsid_.isEmpty()) {
    playback_.error = "WiFi credentials missing";
    showNotice(playback_.error, 5000);
    wifiConnectFailed_ = true;
    wifiConnectFailedAt_ = millis();
    if (bootScreen) {
      display_.forceBacklightPercent(100);
      display_.showBootMessage("WiFi missing\nUse setup AP", battery_);
    }
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(250);
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
    playback_.error = "Not connected";
    showNotice(playback_.error, 5000);
    wifiConnectFailed_ = true;
    wifiConnectFailedAt_ = millis();
    WiFi.disconnect(false, false);
    if (bootScreen) {
      display_.forceBacklightPercent(100);
      display_.showBootMessage("Not connected\nEncoder press: retry", battery_);
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
      mqttPublisher_,
      mqttSettings_,
      screenBrightnessPercent_,
      screenOffTimeoutMs_,
      deviceSleepTimeoutMs_,
      this,
      applyWebSettingsCallback,
      applyWebMqttSettingsCallback,
      applyWebWifiSettingsCallback,
      refreshFromWebCallback,
      hardResetFromWebCallback);

  mqttPublisher_.begin(
      mqttSettings_,
      playback_,
      battery_,
      diagnostics_,
      visualState_,
      screenBrightnessPercent_,
      screenOffTimeoutMs_);
}

bool SpotifyDJApp::syncClock() {
#if SPOTIFY_ALLOW_INSECURE_TLS
  return true;
#else
  // TLS certificate validation needs a sane wall clock on ESP32.
  display_.showBootMessage("Syncing clock...", battery_);
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

  const uint32_t startedAt = millis();
  time_t now = time(nullptr);
  while (now < 1704067200 && millis() - startedAt < 12000) {
    delay(250);
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
  display_.showBootMessage(String(Config::ProvisioningApSsid) + "\nWiFi to configure", battery_);
  ledRing_.showSetupRainbowBreath();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::ProvisioningApSsid);
  const IPAddress apIp = WiFi.softAPIP();

  DNSServer dnsServer;
  dnsServer.start(53, "*", apIp);

  WebServer server(80);
  String portalMessage = "Connect to this AP and submit WiFi + Spotify details.";
  bool portalError = false;

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError));
  });

  server.on("/submit", HTTP_POST, [&]() {
    String ssid = server.arg("ssid");
    const String password = server.arg("password");
    String clientId = server.arg("clientId");
    String refreshToken = server.arg("refreshToken");
    ssid.trim();
    clientId.trim();
    refreshToken.trim();
    MqttSettings submittedMqtt;
    submittedMqtt.host = server.arg("mqttHost");
    submittedMqtt.host.trim();
    submittedMqtt.port = server.arg("mqttPort").toInt() > 0 ? server.arg("mqttPort").toInt() : 1883;
    submittedMqtt.username = server.arg("mqttUser");
    submittedMqtt.password = server.arg("mqttPass");
    submittedMqtt.enabled = !submittedMqtt.host.isEmpty();

    if (ssid.isEmpty()) {
      portalMessage = "SSID is required.";
      portalError = true;
      server.send(200, "text/html", captivePortalPage(portalMessage, portalError));
      return;
    }
    if (clientId.isEmpty() || refreshToken.isEmpty()) {
      portalMessage = "Spotify client ID and refresh token are required.";
      portalError = true;
      server.send(200, "text/html", captivePortalPage(portalMessage, portalError));
      return;
    }

    String message;
    if (testAndSaveProvisioning(ssid, password, clientId, refreshToken, submittedMqtt, message)) {
      server.send(200, "text/html", captivePortalPage(message, false));
      delay(1500);
      ESP.restart();
      return;
    }

    portalMessage = message;
    portalError = true;
    server.send(200, "text/html", captivePortalPage(portalMessage, portalError));
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
    display_.forceBacklightPercent(100);
    ledRing_.showSetupRainbowBreath();
    const uint32_t now = millis();

    if (lastBatteryRefreshAt == 0 || now - lastBatteryRefreshAt >= Config::BatteryPollIntervalMs) {
      lastBatteryRefreshAt = now;
      batteryMonitor_.refresh();
      evaluateBatteryTransition();
      display_.showBootMessage(String(Config::ProvisioningApSsid) + "\nWiFi to configure", battery_);
    }

    if (now - setupStartedAt <= Config::SetupPromptBeepDurationMs &&
        (lastSetupPromptAt == 0 || now - lastSetupPromptAt >= Config::SetupPromptBeepIntervalMs)) {
      lastSetupPromptAt = now;
      sound_.playSetupPrompt();
    }

    if (!deepSleepStarted_ && now - setupStartedAt >= Config::ProvisioningPortalTimeoutMs) {
      display_.showBootMessage("Setup timeout\nSleeping...", battery_);
      delay(600);
      enterDeepSleep();
    }
    delay(5);
  }
}

String SpotifyDJApp::captivePortalPage(const String &message, bool error) const {
  String page;
  page.reserve(3900);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>SpotifyDJ Setup</title><style>");
  page += F("body{margin:0;background:#080b0c;color:#f3f7f5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}");
  page += F("main{max-width:520px;margin:0 auto;padding:18px}h1{font-size:26px;margin:8px 0 4px}p{color:#9aa6a2;line-height:1.4}");
  page += F(".box{background:#111718;border:1px solid #233033;border-radius:8px;padding:14px;margin-top:14px}");
  page += F(".msg{border-radius:8px;padding:10px 12px;margin:12px 0;background:");
  page += error ? F("#3a1714;color:#ffd1c9") : F("#173721;color:#baf7ca");
  page += F("}label{display:grid;gap:6px;margin:12px 0;color:#a8b3af;font-size:13px}");
  page += F("input,button{width:100%;min-height:44px;border-radius:8px;border:1px solid #233033;background:#0c1112;color:#f3f7f5;padding:9px 10px;font-size:16px}");
  page += F("button{background:#173721;border-color:#25593a;color:#baf7ca;font-weight:700;margin-top:8px}");
  page += F("</style></head><body><main><h1>SpotifyDJ Setup</h1>");
  page += F("<p>Provision WiFi, Spotify and MQTT from your phone. Credentials are saved locally after the device tests them.</p>");
  page += F("<div class='msg'>");
  page += message;
  page += F("</div><form class='box' method='post' action='/submit'>");
  page += F("<label>WiFi SSID<input name='ssid' autocomplete='off' required></label>");
  page += F("<label>WiFi password<input name='password' type='password' autocomplete='current-password'></label>");
  page += F("<label>Spotify client ID<input name='clientId' autocomplete='off' required></label>");
  page += F("<label>Spotify refresh token<input name='refreshToken' type='password' autocomplete='off' required></label>");
  page += F("<label>MQTT host<input name='mqttHost' autocomplete='off' value='homeassistant.local' placeholder='HA IP or homeassistant.local'></label>");
  page += F("<label>MQTT port<input name='mqttPort' inputmode='numeric' value='1883'></label>");
  page += F("<label>MQTT username<input name='mqttUser' autocomplete='off' value='mqtt'></label>");
  page += F("<label>MQTT password<input name='mqttPass' type='password' autocomplete='off'></label>");
  page += F("<button type='submit'>Test and save</button></form>");
  page += F("</main></body></html>");
  return page;
}

bool SpotifyDJApp::testAndSaveProvisioning(
    const String &ssid,
    const String &password,
    const String &clientId,
    const String &refreshToken,
    const MqttSettings &mqttSettings,
    String &message) {
  display_.showBootMessage("Testing WiFi...", battery_);
  ledRing_.showSolid(CRGB::Yellow, 100);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    delay(250);
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

  display_.showBootMessage("Testing Spotify...", battery_);
  spotify_.useCredentialsForProvisioning(clientId, refreshToken);
  if (!spotify_.authorize()) {
    message = playback_.error.isEmpty() ? "Spotify authorization failed." : playback_.error;
    WiFi.disconnect(false);
    return false;
  }

  Preferences provision;
  provision.begin("provision", false);
  provision.putString("ssid", ssid);
  provision.putString("pass", password);
  provision.putString("sp_client", clientId);
  provision.putString("sp_refresh", refreshToken);
  provision.putString("mqtt_host", mqttSettings.host);
  provision.putUInt("mqtt_port", mqttSettings.port);
  provision.putString("mqtt_user", mqttSettings.username);
  provision.putString("mqtt_pass", mqttSettings.password);
  provision.putBool("setup", false);
  provision.end();

  wifiSsid_ = ssid;
  wifiPassword_ = password;
  mqttSettings_ = mqttSettings;
  setupModeRequested_ = false;
  message = "Provisioning successful. Rebooting into normal mode...";
  display_.showBootMessage("Setup OK...", battery_);
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

  const bool shouldShowTopHoldHint = events.topButtonHeld && !isMenuActive();
  if (shouldShowTopHoldHint) {
    if (!topHoldRestartHintVisible_) {
      topHoldRestartHintVisible_ = true;
      notice_.show(TopHoldRestartHint, 250);
      renderNow();
    } else if (notice_.message == TopHoldRestartHint) {
      notice_.visibleUntil = millis() + 250;
    }
  } else if (topHoldRestartHintVisible_) {
    topHoldRestartHintVisible_ = false;
    if (notice_.message == TopHoldRestartHint) {
      notice_.visibleUntil = 0;
      renderNow();
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

  if (events.encoderClick) {
    pauseOrResume();
  }

  if (events.topButtonClick) {
    goToNextTrack();
  }

  if (events.topButtonDoubleClick) {
    goToPreviousTrack();
  }

  if (events.encoderLongClick) {
    openAlbumArtScreen();
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

  if (events.encoderClick) {
    selectCurrentMenuItem();
  }

  if (events.topButtonClick) {
    goBackOneScreen();
  }

  // Encoder long press is intentionally unused in menu screens.
  // Holding the top button for 10 seconds keeps its hardware reset role.
}

void SpotifyDJApp::openRootMenu() {
  activeScreen_ = UiScreen::RootMenu;
  menuStackSize_ = 0;
  showNotice("Menu", 900);
  renderNow();
}

void SpotifyDJApp::openScreen(UiScreen screen) {
  if (menuStackSize_ < MenuStackCapacity) {
    menuStack_[menuStackSize_++] = activeScreen_;
  }
  activeScreen_ = screen;
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
  renderNow();
}

void SpotifyDJApp::moveMenuSelection(int encoderSteps) {
  const size_t itemCount = menuItemCount(activeScreen_);
  if (itemCount == 0) {
    return;
  }

  size_t &selection = selectedIndexRefForScreen(activeScreen_);
  int nextSelection = static_cast<int>(selection) + encoderSteps;
  while (nextSelection < 0) {
    nextSelection += itemCount;
  }
  selection = static_cast<size_t>(nextSelection) % itemCount;

  if (activeScreen_ == UiScreen::Brightness) {
    screenBrightnessPercent_ = brightnessValuePercent(brightnessSelection_);
    display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  }

  renderNow();
}

void SpotifyDJApp::selectCurrentMenuItem() {
  switch (activeScreen_) {
    case UiScreen::RootMenu:
      if (rootMenuSelection_ == 0) {
        openScreen(UiScreen::Queue);
        showNotice("Loading queue", 1200);
        spotify_.refreshQueue(queue_);
        renderNow();
      } else if (rootMenuSelection_ == 1) {
        openScreen(UiScreen::SoundOutputs);
        soundOutputSelection_ = 0;
        showNotice("Loading outputs", 1200);
        spotify_.refreshDevices(deviceList_);
        for (size_t index = 0; index < deviceList_.count; index++) {
          if (deviceList_.devices[index].active) {
            soundOutputSelection_ = index;
            break;
          }
        }
        renderNow();
      } else if (rootMenuSelection_ == 2) {
        openScreen(UiScreen::Settings);
      } else if (rootMenuSelection_ == 3) {
        openScreen(UiScreen::About);
      } else if (rootMenuSelection_ == 4) {
        openScreen(UiScreen::Logs);
      }
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
        display_.showBootMessage("Turning off...", battery_);
        delay(250);
        enterDeepSleep();
      } else if (settingsSelection_ == 4) {
        sound_.playHardReset();
        display_.showBootMessage("Restarting...", battery_);
        delay(320);
        ESP.restart();
      } else if (settingsSelection_ == 5) {
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

    case UiScreen::SleepTimeout:
      applySleepTimeoutSelection();
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
  showNotice("Dim timeout " + dimTimeoutLabel(screenOffTimeoutMs_), 2000);
  renderNow();
}

void SpotifyDJApp::applyBrightnessSelection() {
  screenBrightnessPercent_ = brightnessValuePercent(brightnessSelection_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  showNotice("Brightness " + String(screenBrightnessPercent_) + "%", 2000);
  renderNow();
}

void SpotifyDJApp::applySleepTimeoutSelection() {
  // The selected timeout controls full ESP32-S3 deep sleep, not just the display backlight.
  deviceSleepTimeoutMs_ = sleepTimeoutValueMs(sleepTimeoutSelection_);
  saveDisplaySettings();
  showNotice("Deep sleep " + String(deviceSleepTimeoutMs_ / 60000UL) + " min", 2000);
  renderNow();
}

void SpotifyDJApp::saveDisplaySettings() {
  Preferences provision;
  provision.begin("provision", false);
  provision.putUInt("screen_off_ms", screenOffTimeoutMs_);
  provision.putUInt("sleep_ms", deviceSleepTimeoutMs_);
  provision.putUInt("screen_bright", screenBrightnessPercent_);
  provision.end();
}

void SpotifyDJApp::hardResetToProvisioning() {
  sound_.playHardReset();
  display_.wakeForUserActivity();
  display_.showBootMessage("Hard reset...", battery_);
  ledRing_.showSolid(CRGB::Yellow, 100);

  spotify_.clearStoredTokens();

  Preferences spotifyPrefs;
  spotifyPrefs.begin("spotify", false);
  spotifyPrefs.clear();
  spotifyPrefs.end();

  Preferences provision;
  provision.begin("provision", false);
  provision.clear();
  provision.putBool("setup", true);
  provision.end();

  WiFi.disconnect(true, true);
  delay(600);
  ESP.restart();
}

bool SpotifyDJApp::isMenuActive() const {
  return activeScreen_ != UiScreen::NowPlaying;
}

size_t SpotifyDJApp::menuItemCount(UiScreen screen) const {
  switch (screen) {
    case UiScreen::AlbumArt:
      return 0;
    case UiScreen::Queue:
      return 0;
    case UiScreen::SoundOutputs:
      return deviceList_.available && deviceList_.count > 0 ? deviceList_.count : 0;
    case UiScreen::Logs:
      return 0;
    case UiScreen::RootMenu:
      return 5;
    case UiScreen::Settings:
      return 6;
    case UiScreen::DimTimeout:
      return DimTimeoutOptionCount;
    case UiScreen::Brightness:
      return BrightnessOptionCount;
    case UiScreen::SleepTimeout:
      return SleepTimeoutOptionCount;
    case UiScreen::HardResetConfirm:
      return HardResetOptionCount;
    case UiScreen::About:
    case UiScreen::NowPlaying:
      return 0;
  }
  return 0;
}

size_t SpotifyDJApp::selectedIndexForScreen(UiScreen screen) const {
  switch (screen) {
    case UiScreen::RootMenu:
      return rootMenuSelection_;
    case UiScreen::Settings:
      return settingsSelection_;
    case UiScreen::DimTimeout:
      return dimTimeoutSelection_;
    case UiScreen::Brightness:
      return brightnessSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
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
    case UiScreen::Brightness:
      return brightnessSelection_;
    case UiScreen::SleepTimeout:
      return sleepTimeoutSelection_;
    case UiScreen::HardResetConfirm:
      return hardResetSelection_;
    case UiScreen::SoundOutputs:
      return soundOutputSelection_;
    case UiScreen::About:
    case UiScreen::AlbumArt:
    case UiScreen::Queue:
    case UiScreen::Logs:
    case UiScreen::NowPlaying:
      return rootMenuSelection_;
  }
  return rootMenuSelection_;
}

uint32_t SpotifyDJApp::dimTimeoutValueMs(size_t index) const {
  static const uint32_t values[DimTimeoutOptionCount] = {30000, 60000, 120000, 240000};
  return values[index < DimTimeoutOptionCount ? index : 1];
}

uint8_t SpotifyDJApp::brightnessValuePercent(size_t index) const {
  static const uint8_t values[BrightnessOptionCount] = {25, 50, 75, 100};
  return values[index < BrightnessOptionCount ? index : 3];
}

uint32_t SpotifyDJApp::sleepTimeoutValueMs(size_t index) const {
  return Logic::deepSleepTimeoutMsForIndex(index);
}

void SpotifyDJApp::handleEncoderTurn(int encoderSteps) {
  sound_.playVolumeTick(encoderSteps);
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
  showNotice("Volume " + String(pendingVolume_) + "%", 1200);
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
    showNotice("Volume " + String(volume) + "%", 2000);
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

void SpotifyDJApp::pauseOrResume() {
  const uint32_t now = millis();
  if (now - lastPauseToggleAt_ < 900) {
    return;
  }
  lastPauseToggleAt_ = now;

  // Refresh first so a stale paused/playing bit does not invert the user's command.
  showNotice("Checking playback", 1200);
  renderNow();
  spotify_.refreshPlayback();

  if (playback_.isPlaying) {
    if (spotify_.pausePlayback()) {
      playback_.isPlaying = false;
      showNotice("Paused");
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
      showNotice("Playing");
      lastPlaybackPollAt_ = 0;
    } else {
      showNotice(playback_.error, 3500);
    }
  }
  renderNow();
}

void SpotifyDJApp::goToNextTrack() {
  if (spotify_.nextTrack()) {
    // Optimistic UI while Spotify switches tracks; the next poll replaces this with real metadata.
    playback_.trackName = "Loading next track";
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice("Next track");

    if (!playback_.isPlaying && spotify_.resumePlayback()) {
      playback_.isPlaying = true;
      display_.restartTitleScroll();
      display_.restartArtistScroll();
      showNotice("Playing");
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
    playback_.trackName = "Loading previous track";
    playback_.artistName = "";
    playback_.progressMs = 0;
    playback_.durationMs = 0;
    playback_.progressSyncedAt = millis();
    showNotice("Previous track");
    lastPlaybackPollAt_ = 0;
  } else {
    showNotice(playback_.error, 3500);
  }
  renderNow();
}

void SpotifyDJApp::refreshPlaybackAndBattery() {
  showNotice("Refreshing");
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
  activeScreen_ = UiScreen::AlbumArt;
  menuStackSize_ = 0;
  showNotice("Current Song", 900);
  renderNow();
  albumArt_.requestCurrentSongArt(playback_);
  renderNow();
}

void SpotifyDJApp::openQueueScreen() {
  activeScreen_ = UiScreen::Queue;
  menuStackSize_ = 0;
  showNotice("Loading queue", 1200);
  renderNow();
  spotify_.refreshQueue(queue_);
  renderNow();
}

void SpotifyDJApp::openSoundOutputsScreen() {
  activeScreen_ = UiScreen::SoundOutputs;
  menuStackSize_ = 0;
  soundOutputSelection_ = 0;
  showNotice("Loading outputs", 1200);
  renderNow();
  spotify_.refreshDevices(deviceList_);
  for (size_t index = 0; index < deviceList_.count; index++) {
    if (deviceList_.devices[index].active) {
      soundOutputSelection_ = index;
      break;
    }
  }
  renderNow();
}

void SpotifyDJApp::transferToSelectedOutput() {
  if (!deviceList_.available || deviceList_.count == 0 || soundOutputSelection_ >= deviceList_.count) {
    showNotice("No outputs", 2000);
    renderNow();
    return;
  }

  const SpotifyDeviceState &device = deviceList_.devices[soundOutputSelection_];
  showNotice("Starting " + device.name, 1800);
  renderNow();
  if (spotify_.transferPlayback(device.id, true)) {
    showNotice("Playing on " + device.name, 2500);
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
        spotify_.isAuthorized(),
        mqttSettings_.enabled && mqttPublisher_.connected());
  }
  if (connectionHealthy()) {
    ledRing_.showVolume(displayedVolume());
  } else {
    ledRing_.showSolid(CRGB::Red, display_.backlightPercent());
  }
  ledRing_.setPowerPercent(display_.backlightPercent());
  visualState_.screenOn = display_.isOn();
  visualState_.screenBrightnessLevel = display_.backlightPercent();
  visualState_.ledOn = ledRing_.isOn();
  mqttPublisher_.requestPublish();
}

bool SpotifyDJApp::chargerConnected() const {
  return battery_.charging || battery_.full || battery_.currentMa > Config::BatteryChargeCurrentThresholdMa;
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
    display_.showBootMessage("Battery OK\nRestarting...", battery_);
    delay(800);
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
  const String message = chargingBatteryGuardActive_ ? "Charging..." : "Please charge device";
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
  status.ipAddress = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No WiFi";
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

void SpotifyDJApp::enterDeepSleep() {
  deepSleepStarted_ = true;
  AppLog.println("Entering deep sleep");

  if (activeScreen_ != UiScreen::NowPlaying) {
    activeScreen_ = UiScreen::NowPlaying;
    menuStackSize_ = 0;
  }

  mqttPublisher_.prepareForSleep();
  ledRing_.setPowerPercent(0);
  delay(50);

  // Wake only on the two buttons. Encoder phase lines can rest LOW, which would wake immediately.
  const uint64_t wakeMask = (1ULL << Config::BoardUserKeyPin) |
                            (1ULL << Config::EncoderButtonPin);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (lowBatteryTimerWake_) {
    esp_sleep_enable_timer_wakeup(Config::LowBatteryWakeCheckUs);
  }
  WiFi.disconnect(true, false);
  delay(100);
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

void SpotifyDJApp::applyWebSettings(uint8_t brightnessPercent, uint32_t offTimeoutMs, uint32_t sleepTimeoutMs) {
  screenBrightnessPercent_ = constrain(brightnessPercent, 25, 100);
  screenOffTimeoutMs_ = constrain(offTimeoutMs, 30000UL, 240000UL);
  deviceSleepTimeoutMs_ = constrain(sleepTimeoutMs, 300000UL, 3600000UL);
  sleepTimeoutSelection_ = Logic::deepSleepTimeoutIndexForMs(deviceSleepTimeoutMs_);
  display_.configurePowerSaving(screenBrightnessPercent_, screenOffTimeoutMs_);
  saveDisplaySettings();
  showNotice("Web settings saved", 1800);
  renderNow();
}

void SpotifyDJApp::applyWebMqttSettings(const MqttSettings &settings) {
  mqttSettings_ = settings;
  mqttSettings_.host.trim();
  mqttSettings_.enabled = !mqttSettings_.host.isEmpty();

  Preferences provision;
  provision.begin("provision", false);
  provision.putString("mqtt_host", mqttSettings_.host);
  provision.putUInt("mqtt_port", mqttSettings_.port);
  provision.putString("mqtt_user", mqttSettings_.username);
  provision.putString("mqtt_pass", mqttSettings_.password);
  provision.end();

  mqttPublisher_.begin(
      mqttSettings_,
      playback_,
      battery_,
      diagnostics_,
      visualState_,
      screenBrightnessPercent_,
      screenOffTimeoutMs_);
  mqttPublisher_.requestPublish();
}

void SpotifyDJApp::requestWebWifiSettings(const String &ssid, const String &password) {
  pendingWifiSsid_ = ssid;
  pendingWifiSsid_.trim();
  pendingWifiPassword_ = password;
  pendingWifiPasswordProvided_ = !password.isEmpty();
  pendingWifiSettingsRequestedAt_ = millis();
  pendingWifiSettings_ = !pendingWifiSsid_.isEmpty();
  showNotice("Testing WiFi", 2500);
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

  AppLog.print("Testing web WiFi credentials for SSID: ");
  AppLog.println(pendingWifiSsid_);
  display_.wakeForUserActivity();
  display_.showBootMessage("Testing WiFi...", battery_);
  ledRing_.showSolid(CRGB::Yellow, 100);

  WiFi.disconnect(false, false);
  delay(250);
  WiFi.mode(WIFI_STA);
  WiFi.begin(pendingWifiSsid_.c_str(), targetPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 25000) {
    delay(250);
    AppLog.print(".");
  }
  AppLog.println();

  if (WiFi.status() == WL_CONNECTED) {
    AppLog.print("Web WiFi test OK, IP: ");
    AppLog.println(WiFi.localIP());
    Preferences provision;
    provision.begin("provision", false);
    provision.putString("ssid", pendingWifiSsid_);
    provision.putString("pass", targetPassword);
    provision.end();
    display_.showBootMessage("WiFi OK. Restarting...", battery_);
    delay(900);
    ESP.restart();
    return;
  }

  AppLog.println("Web WiFi test failed, restoring previous connection");
  playback_.error = "WiFi test failed";
  showNotice("WiFi test failed", 5000);
  WiFi.disconnect(false, false);
  delay(250);
  if (!oldSsid.isEmpty()) {
    WiFi.begin(oldSsid.c_str(), oldPassword.c_str());
  }
  renderNow();
}

void SpotifyDJApp::applyWebSettingsCallback(
    void *context,
    uint8_t brightnessPercent,
    uint32_t offTimeoutMs,
    uint32_t sleepTimeoutMs) {
  static_cast<SpotifyDJApp *>(context)->applyWebSettings(brightnessPercent, offTimeoutMs, sleepTimeoutMs);
}

void SpotifyDJApp::applyWebMqttSettingsCallback(void *context, const MqttSettings &settings) {
  static_cast<SpotifyDJApp *>(context)->applyWebMqttSettings(settings);
}

void SpotifyDJApp::applyWebWifiSettingsCallback(void *context, const String &ssid, const String &password) {
  static_cast<SpotifyDJApp *>(context)->requestWebWifiSettings(ssid, password);
}

void SpotifyDJApp::refreshFromWebCallback(void *context) {
  static_cast<SpotifyDJApp *>(context)->refreshPlaybackAndBattery();
}

void SpotifyDJApp::hardResetFromWebCallback(void *context) {
  static_cast<SpotifyDJApp *>(context)->hardResetToProvisioning();
}

void SpotifyDJApp::renderMenuNow() {
  switch (activeScreen_) {
    case UiScreen::AlbumArt:
      display_.renderAlbumArtScreen(playback_, notice_, albumArt_.imagePath(), albumArt_.status());
      break;

    case UiScreen::RootMenu: {
      MenuItemView items[] = {
          {"Up next"},
          {"Sound outputs"},
          {"Settings"},
          {"About"},
          {"Logs"},
      };
      display_.renderMenuList("Menu", items, 5, rootMenuSelection_, notice_);
      break;
    }

    case UiScreen::SoundOutputs: {
      MenuItemView items[8];
      size_t itemCount = deviceList_.count;
      if (!deviceList_.available || itemCount == 0) {
        items[0].label = deviceList_.error.isEmpty() ? "No outputs" : deviceList_.error;
        itemCount = 1;
      } else {
        for (size_t index = 0; index < itemCount; index++) {
          items[index].label = deviceList_.devices[index].active ? "* " : "  ";
          items[index].label += deviceList_.devices[index].name;
        }
      }
      display_.renderMenuList("Sound outputs", items, itemCount, soundOutputSelection_, notice_);
      break;
    }

    case UiScreen::Queue: {
      MenuItemView items[5];
      size_t itemCount = queue_.count;
      if (!queue_.available || itemCount == 0) {
        items[0].label = queue_.error.isEmpty() ? "Queue empty" : queue_.error;
        itemCount = 1;
      } else {
        for (size_t index = 0; index < itemCount; index++) {
          items[index].label = queue_.items[index].title;
          if (!queue_.items[index].subtitle.isEmpty()) {
            items[index].label += " - " + queue_.items[index].subtitle;
          }
        }
      }
      display_.renderMenuList("Up next", items, itemCount, 0, notice_);
      break;
    }

    case UiScreen::About:
      display_.renderAboutScreen(notice_, aboutStatus());
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
          {String("Screen brightness ") + String(screenBrightnessPercent_) + "%"},
          {String("Screen dim timeout ") + dimTimeoutLabel(screenOffTimeoutMs_)},
          {String("Deep sleep after ") + String(deviceSleepTimeoutMs_ / 60000UL) + " min"},
          {"Turn off device"},
          {"Restart device"},
          {"Hard reset"},
      };
      display_.renderMenuList("Settings", items, 6, settingsSelection_, notice_);
      break;
    }

    case UiScreen::DimTimeout: {
      MenuItemView items[DimTimeoutOptionCount];
      for (size_t index = 0; index < DimTimeoutOptionCount; index++) {
        const uint32_t valueMs = dimTimeoutValueMs(index);
        items[index].label = dimTimeoutLabel(valueMs);
        if (valueMs == screenOffTimeoutMs_) {
          items[index].label += " selected";
        }
      }
      display_.renderMenuList(
          "Screen dim timeout",
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
          items[index].label += " selected";
        }
      }
      display_.renderMenuList(
          "Screen brightness",
          items,
          BrightnessOptionCount,
          brightnessSelection_,
          notice_);
      break;
    }

    case UiScreen::SleepTimeout: {
      MenuItemView items[SleepTimeoutOptionCount];
      for (size_t index = 0; index < SleepTimeoutOptionCount; index++) {
        const uint32_t valueMs = sleepTimeoutValueMs(index);
        items[index].label = String(valueMs / 60000UL) + " min";
        if (valueMs == deviceSleepTimeoutMs_) {
          items[index].label += " selected";
        }
      }
      display_.renderMenuList(
          "Deep sleep after",
          items,
          SleepTimeoutOptionCount,
          sleepTimeoutSelection_,
          notice_);
      break;
    }

    case UiScreen::HardResetConfirm: {
      MenuItemView items[HardResetOptionCount] = {
          {"No, go back"},
          {"Yes, wipe setup"},
      };
      display_.renderMenuList("Hard reset?", items, HardResetOptionCount, hardResetSelection_, notice_);
      break;
    }

    case UiScreen::NowPlaying:
      display_.renderPlaybackScreen(
          playback_,
          battery_,
          notice_,
          displayedVolume(),
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

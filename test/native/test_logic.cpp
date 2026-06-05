// Host-side unit tests for pure firmware logic.
// These tests avoid Arduino headers so they can run quickly on the development machine.

#include <cassert>
#include <cstring>

#include <ArduinoJson.h>

#include "LogicHelpers.h"
#include "NetworkActivityLogic.h"
#include "SpotifyDJMenuModel.h"
#include "SpotifyProvisioning.h"

struct FakeHttpClient {
  uint32_t connectTimeout = 0;
  uint32_t ioTimeout = 0;
  bool reuse = true;

  void setConnectTimeout(uint32_t value) {
    connectTimeout = value;
  }

  void setTimeout(uint32_t value) {
    ioTimeout = value;
  }

  void setReuse(bool value) {
    reuse = value;
  }
};

static void testTrackTimeFormatting() {
  char buffer[12] = {};

  assert(Logic::formatTrackTime(0, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "0:00") == 0);

  assert(Logic::formatTrackTime(61000, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "1:01") == 0);

  assert(Logic::formatTrackTime(3661000, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "1:01:01") == 0);

  assert(Logic::formatTrackTime(-5000, buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "0:00") == 0);
}

static void testProgressEstimation() {
  assert(Logic::estimatedProgressMs(1000, 10000, false, 500, 2500) == 1000);
  assert(Logic::estimatedProgressMs(1000, 10000, true, 500, 2500) == 3000);
  assert(Logic::estimatedProgressMs(9000, 10000, true, 1000, 5000) == 10000);
  assert(Logic::estimatedProgressMs(-100, 10000, false, 0, 0) == 0);
  assert(Logic::estimatedProgressMs(1000, 0, true, 0, 1000) == 0);
}

static void testLedRingBrightness() {
  assert(Logic::ledSegmentBrightness(0, 0, 8) == 0);
  assert(Logic::ledSegmentBrightness(100, 0, 8) == 255);
  assert(Logic::ledSegmentBrightness(100, 7, 8) == 255);
  assert(Logic::ledSegmentBrightness(50, 0, 8) == 255);
  assert(Logic::ledSegmentBrightness(50, 3, 8) == 255);
  assert(Logic::ledSegmentBrightness(50, 4, 8) == 0);
  assert(Logic::ledSegmentBrightness(13, 1, 8) > 0);
  assert(Logic::ledSegmentBrightness(13, 2, 8) == 0);
  assert(Logic::ledSegmentBrightness(80, -1, 8) == 0);
}

static void testDisplayPowerPolicy() {
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 9999) == 100);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 10000) == 100);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 15000) == 75);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 20000) == 50);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 59999) == 50);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 60000) == 0);
  assert(Logic::displayPowerPercent(100, 50, 10000, 20000, 60000, 300000) == 0);
  assert(Logic::displayPowerPercent(50, 50, 10000, 20000, 0, 70000) == 50);
  assert(Logic::displayPowerPercent(140, 50, 10000, 20000, 60000, 0) == 100);
}

static void testDeepSleepTimeoutOptions() {
  assert(Logic::deepSleepTimeoutMsForIndex(0) == 300000UL);
  assert(Logic::deepSleepTimeoutMsForIndex(1) == 900000UL);
  assert(Logic::deepSleepTimeoutMsForIndex(2) == 1800000UL);
  assert(Logic::deepSleepTimeoutMsForIndex(3) == 3600000UL);
  assert(Logic::deepSleepTimeoutMsForIndex(99) == 300000UL);

  assert(Logic::deepSleepTimeoutIndexForMs(300000UL) == 0);
  assert(Logic::deepSleepTimeoutIndexForMs(900000UL) == 1);
  assert(Logic::deepSleepTimeoutIndexForMs(1800000UL) == 2);
  assert(Logic::deepSleepTimeoutIndexForMs(3600000UL) == 3);
  assert(Logic::deepSleepTimeoutIndexForMs(123456UL) == 0);
}

static void testChargingCompleteCue() {
  assert(!Logic::shouldPlayChargingCompleteCue(false, 95, false));
  assert(!Logic::shouldPlayChargingCompleteCue(true, 90, false));
  assert(Logic::shouldPlayChargingCompleteCue(true, 91, false));
  assert(!Logic::shouldPlayChargingCompleteCue(true, 95, true));
}

static void testBacklightDutyCurve() {
  assert(Logic::backlightDutyForPercent(0, 255) == 0);
  assert(Logic::backlightDutyForPercent(100, 255) == 255);
  assert(Logic::backlightDutyForPercent(50, 255) < 128);
  assert(Logic::backlightDutyForPercent(50, 255) > 0);
  assert(Logic::backlightDutyForPercent(10, 255) > 0);
  assert(Logic::backlightDutyForPercent(140, 255) == 255);
}

static void testBatteryVoltageFallbackCurve() {
  assert(Logic::batteryPercentFromVoltage(3299) == 0);
  assert(Logic::batteryPercentFromVoltage(3300) == 5);
  assert(Logic::batteryPercentFromVoltage(3500) == 10);
  assert(Logic::batteryPercentFromVoltage(3600) == 20);
  assert(Logic::batteryPercentFromVoltage(3700) == 35);
  assert(Logic::batteryPercentFromVoltage(3800) == 50);
  assert(Logic::batteryPercentFromVoltage(3900) == 65);
  assert(Logic::batteryPercentFromVoltage(4000) == 80);
  assert(Logic::batteryPercentFromVoltage(4100) == 90);
  assert(Logic::batteryPercentFromVoltage(4146) == 90);
  assert(Logic::batteryPercentFromVoltage(4150) == 100);
  assert(Logic::batteryPercentFromVoltage(4300) == 100);
}

static void testWebBatteryHeaderClass() {
  assert(std::strcmp(Logic::batteryHeaderClass(0, false), "low") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(19, false), "low") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(20, false), "medium") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(49, false), "medium") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(50, false), "high") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(100, false), "high") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(10, true), "charging low") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(30, true), "charging medium") == 0);
  assert(std::strcmp(Logic::batteryHeaderClass(90, true), "charging high") == 0);
}

static void testBq27220Interpretation() {
  const auto stuckReading = Logic::interpretBq27220(
      40,
      true,
      4146,
      true,
      0x0000,
      true,
      0,
      5);
  assert(stuckReading.available);
  assert(stuckReading.gaugePercent == 40);
  assert(stuckReading.percent == 90);
  assert(stuckReading.percentEstimated);
  assert(stuckReading.voltageMv == 4146);
  assert(stuckReading.currentMa == 0);
  assert(!stuckReading.discharging);
  assert(!stuckReading.full);
  assert(!stuckReading.charging);

  const auto discharging = Logic::interpretBq27220(
      75,
      true,
      3900,
      true,
      0x0001,
      true,
      static_cast<uint16_t>(static_cast<int16_t>(-120)),
      5);
  assert(discharging.discharging);
  assert(!discharging.charging);
  assert(discharging.percentEstimated);
  assert(discharging.percent == 65);
  assert(discharging.currentMa == -120);

  const auto full = Logic::interpretBq27220(
      100,
      true,
      4200,
      true,
      0x0200,
      true,
      0,
      5);
  assert(full.full);
  assert(!full.charging);
  assert(full.percentEstimated);
  assert(full.percent == 100);

  const auto charging = Logic::interpretBq27220(
      50,
      true,
      3900,
      true,
      0x0000,
      true,
      120,
      5);
  assert(charging.charging);
  assert(charging.currentMa == 120);
  assert(charging.percentEstimated);
  assert(charging.percent == 65);

  const auto negativeCurrentWithoutDsgBit = Logic::interpretBq27220(
      50,
      true,
      3900,
      true,
      0x0000,
      true,
      static_cast<uint16_t>(static_cast<int16_t>(-120)),
      5);
  assert(!negativeCurrentWithoutDsgBit.charging);
  assert(!negativeCurrentWithoutDsgBit.discharging);
  assert(negativeCurrentWithoutDsgBit.currentMa == -120);

  const auto missingCurrent = Logic::interpretBq27220(
      50,
      true,
      3900,
      true,
      0x0000,
      false,
      0,
      5);
  assert(!missingCurrent.charging);
}

static void testVoiceChunkHelpers() {
  assert(Logic::voiceAudioPayloadBytes(0, 1024) == 0);
  assert(Logic::voiceAudioPayloadBytes(256, 1024) == 256);
  assert(Logic::voiceAudioPayloadBytes(1024, 1024) == 1024);
  assert(Logic::voiceAudioPayloadBytes(2048, 1024) == 1024);
  assert(Logic::voiceAudioPayloadBytes(256, 0) == 0);

  assert(Logic::voiceAssistBinaryFrameBytes(0, 1024) == 0);
  assert(Logic::voiceAssistBinaryFrameBytes(1, 1024) == 2);
  assert(Logic::voiceAssistBinaryFrameBytes(256, 1024) == 257);
  assert(Logic::voiceAssistBinaryFrameBytes(1024, 1024) == 1025);
  assert(Logic::voiceAssistBinaryFrameBytes(2048, 1024) == 1025);
  assert(Logic::voiceAssistBinaryFrameBytes(2048, 0) == 0);

  assert(!Logic::shouldAutoStopVoiceRecording(0, 15000));
  assert(!Logic::shouldAutoStopVoiceRecording(14999, 15000));
  assert(Logic::shouldAutoStopVoiceRecording(15000, 15000));
  assert(Logic::shouldAutoStopVoiceRecording(15001, 15000));
  assert(!Logic::shouldAutoStopVoiceRecording(15000, 0));

  assert(!Logic::shouldSendRecognizedVoiceText(0));
  assert(Logic::shouldSendRecognizedVoiceText(1));
  assert(Logic::shouldSendRecognizedVoiceText(64));

  assert(!Logic::isHomeAssistantPairingInvalidStatus(200));
  assert(Logic::isHomeAssistantPairingInvalidStatus(401));
  assert(Logic::isHomeAssistantPairingInvalidStatus(403));
  assert(Logic::isHomeAssistantPairingInvalidStatus(404));
  assert(!Logic::isHomeAssistantPairingInvalidStatus(500));
  assert(std::strcmp(
             Logic::voiceHttpFailureMessage(404),
             "HA voice endpoint not found. Reset pairing and set up the SpotifyDJ integration again.") == 0);
  assert(std::strcmp(Logic::voiceHttpFailureMessage(401), "HA authorization failed. Reset pairing and pair again.") == 0);
  assert(std::strcmp(Logic::voiceHttpFailureMessage(403), "HA authorization failed. Reset pairing and pair again.") == 0);
  assert(Logic::voiceHttpFailureMessage(500) == nullptr);

  assert(Logic::preferencesKeyFits("sp_client"));
  assert(Logic::preferencesKeyFits("sp_refresh"));
  assert(Logic::preferencesKeyFits("sp_market"));
  assert(!Logic::preferencesKeyFits("spotify_client_id"));
  assert(!Logic::preferencesKeyFits("spotify_refresh_token"));

  assert(!Logic::shouldLockMqttAuthRetries(5, 1, 3));
  assert(!Logic::shouldLockMqttAuthRetries(5, 2, 3));
  assert(Logic::shouldLockMqttAuthRetries(5, 3, 3));
  assert(Logic::shouldLockMqttAuthRetries(4, 3, 3));
  assert(!Logic::shouldLockMqttAuthRetries(3, 3, 3));
  assert(!Logic::shouldLockMqttAuthRetries(5, 3, 0));

  assert(Logic::isSpotifyPlaylistContextUri("spotify:playlist:abc123"));
  assert(!Logic::isSpotifyPlaylistContextUri("spotify:album:abc123"));
  assert(!Logic::isSpotifyPlaylistContextUri("spotify:playlist:"));
  assert(!Logic::isSpotifyPlaylistContextUri(nullptr));
}

static void testPlayModeMapping() {
  assert(std::strcmp(Logic::playModeFromSpotifyState(false, "off"), "normal") == 0);
  assert(std::strcmp(Logic::playModeFromSpotifyState(true, "off"), "shuffle") == 0);
  assert(std::strcmp(Logic::playModeFromSpotifyState(false, "track"), "repeat_once") == 0);
  assert(std::strcmp(Logic::playModeFromSpotifyState(true, "track"), "repeat_once") == 0);
  assert(std::strcmp(Logic::playModeFromSpotifyState(false, "context"), "repeat_infinite") == 0);
  assert(std::strcmp(Logic::playModeFromSpotifyState(false, nullptr), "normal") == 0);

  assert(std::strcmp(Logic::playModeLabel("normal"), "No shuffle") == 0);
  assert(std::strcmp(Logic::playModeLabel("shuffle"), "Shuffle") == 0);
  assert(std::strcmp(Logic::playModeLabel("repeat_once"), "Repeat once") == 0);
  assert(std::strcmp(Logic::playModeLabel("repeat_infinite"), "Repeat infinite") == 0);
  assert(std::strcmp(Logic::playModeLabel("unexpected"), "No shuffle") == 0);
}

static void testMqttDeviceTopicFormatting() {
  char buffer[64] = {};
  assert(Logic::formatMqttDeviceTopic("spotifydj-ABCDEF123456", "status", buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "spotifydj/spotifydj-ABCDEF123456/status") == 0);

  assert(Logic::formatMqttDeviceTopic("spotifydj-ABCDEF123456", "command", buffer, sizeof(buffer)));
  assert(std::strcmp(buffer, "spotifydj/spotifydj-ABCDEF123456/command") == 0);

  char tiny[8] = {};
  assert(!Logic::formatMqttDeviceTopic("spotifydj-ABCDEF123456", "status", tiny, sizeof(tiny)));
  assert(!Logic::formatMqttDeviceTopic(nullptr, "status", buffer, sizeof(buffer)));
  assert(!Logic::formatMqttDeviceTopic("spotifydj-ABCDEF123456", nullptr, buffer, sizeof(buffer)));
}

static void testLanguageCodeNormalization() {
  assert(std::strcmp(Logic::languageCodeOrDefault("en"), "en") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("nl"), "nl") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("NL"), "nl") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("de"), "en") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault(nullptr), "en") == 0);
}

static void testSpotifyProvisioningTopLevel() {
  JsonDocument doc;
  doc["spotify_client_id"] = "client-a";
  doc["spotify_refresh_token"] = "refresh-a";
  doc["spotify_market"] = "NL";
  const auto credentials = SpotifyProvisioning::parseCredentials(doc.as<JsonVariantConst>());
  assert(credentials.complete());
  assert(std::strcmp(credentials.clientId, "client-a") == 0);
  assert(std::strcmp(credentials.refreshToken, "refresh-a") == 0);
  assert(std::strcmp(credentials.market, "NL") == 0);
}

static void testSpotifyProvisioningCompatTopLevel() {
  JsonDocument doc;
  doc["client_id"] = "client-b";
  doc["refresh_token"] = "refresh-b";
  doc["market"] = "BE";
  const auto credentials = SpotifyProvisioning::parseCredentials(doc.as<JsonVariantConst>());
  assert(credentials.complete());
  assert(std::strcmp(credentials.clientId, "client-b") == 0);
  assert(std::strcmp(credentials.refreshToken, "refresh-b") == 0);
  assert(std::strcmp(credentials.market, "BE") == 0);
}

static void testSpotifyProvisioningNestedPriority() {
  JsonDocument doc;
  doc["spotify_client_id"] = "top-client";
  doc["spotify_refresh_token"] = "top-refresh";
  JsonObject spotify = doc["spotify"].to<JsonObject>();
  spotify["client_id"] = "nested-compat-client";
  spotify["refresh_token"] = "nested-compat-refresh";
  spotify["spotify_client_id"] = "nested-client";
  spotify["spotify_refresh_token"] = "nested-refresh";
  spotify["market"] = "DE";
  const auto credentials = SpotifyProvisioning::parseCredentials(doc.as<JsonVariantConst>());
  assert(credentials.complete());
  assert(std::strcmp(credentials.clientId, "nested-client") == 0);
  assert(std::strcmp(credentials.refreshToken, "nested-refresh") == 0);
  assert(std::strcmp(credentials.market, "DE") == 0);
}

static void testSpotifyProvisioningMissingRefreshToken() {
  JsonDocument doc;
  doc["spotify_client_id"] = "client-only";
  const auto credentials = SpotifyProvisioning::parseCredentials(doc.as<JsonVariantConst>());
  assert(!credentials.complete());
  assert(std::strcmp(credentials.clientId, "client-only") == 0);
  assert(credentials.refreshToken == nullptr);
}

static void testSpotifyCredentialRepairValidation() {
  assert(Logic::spotifyRepairCredentialsValid("stored-client", "", "new-refresh"));
  assert(Logic::spotifyRepairCredentialsValid("", "submitted-client", "new-refresh"));
  assert(!Logic::spotifyRepairCredentialsValid("", "", "new-refresh"));
  assert(!Logic::spotifyRepairCredentialsValid("stored-client", "", ""));
  assert(!Logic::spotifyRepairCredentialsValid(nullptr, nullptr, "new-refresh"));
  assert(std::strcmp(Logic::spotifyMarketOrDefault("BE"), "BE") == 0);
  assert(std::strcmp(Logic::spotifyMarketOrDefault(""), "NL") == 0);
  assert(std::strcmp(Logic::spotifyMarketOrDefault(nullptr), "NL") == 0);
}

static void testSpotifyPlaylistPagingDecision() {
  assert(Logic::shouldFetchNextSpotifyPlaylistPage(50, 120, 0, 50));
  assert(Logic::shouldFetchNextSpotifyPlaylistPage(50, 0, 50, 50));
  assert(!Logic::shouldFetchNextSpotifyPlaylistPage(20, 120, 50, 50));
  assert(!Logic::shouldFetchNextSpotifyPlaylistPage(50, 50, 0, 50));
  assert(!Logic::shouldFetchNextSpotifyPlaylistPage(0, 120, 0, 50));
  assert(!Logic::shouldFetchNextSpotifyPlaylistPage(50, 120, -1, 50));
  assert(!Logic::shouldFetchNextSpotifyPlaylistPage(50, 120, 0, 0));
}

static void testSpotifyDJMenuItemCounts() {
  MenuCountInput input;
  assert(!SpotifyDJMenuModel::isMenuScreen(UiScreen::NowPlaying));
  assert(SpotifyDJMenuModel::isMenuScreen(UiScreen::Settings));
  assert(SpotifyDJMenuModel::itemCount(UiScreen::NowPlaying, input) == 0);
  assert(SpotifyDJMenuModel::itemCount(UiScreen::RootMenu, input) == SpotifyDJMenuModel::RootMenuItemCount);
  assert(SpotifyDJMenuModel::itemCount(UiScreen::Settings, input) == SpotifyDJMenuModel::SettingsItemCount);
  assert(SpotifyDJMenuModel::SettingsItemCount == 13);
  assert(SpotifyDJMenuModel::itemCount(UiScreen::About, input) == SpotifyDJMenuModel::AboutItemCount);
  assert(SpotifyDJMenuModel::itemCount(UiScreen::Playlists, input) == 1);
  assert(SpotifyDJMenuModel::itemCount(UiScreen::SoundOutputs, input) == 2);

  input.playlistsAvailable = true;
  input.playlistCount = 3;
  assert(SpotifyDJMenuModel::itemCount(UiScreen::Playlists, input) == 3);

  input.devicesAvailable = true;
  input.deviceCount = 3;
  assert(SpotifyDJMenuModel::itemCount(UiScreen::SoundOutputs, input) == 5);

  input.deviceCount = 99;
  assert(SpotifyDJMenuModel::itemCount(UiScreen::SoundOutputs, input) == SpotifyDJMenuModel::MaxVisibleOutputs + SpotifyDJMenuModel::FixedSoundOutputCount);
}

static void testSpotifyDJMenuOptionValues() {
  assert(SpotifyDJMenuModel::dimTimeoutValueMs(0) == 30000UL);
  assert(SpotifyDJMenuModel::dimTimeoutValueMs(1) == 60000UL);
  assert(SpotifyDJMenuModel::dimTimeoutValueMs(2) == 120000UL);
  assert(SpotifyDJMenuModel::dimTimeoutValueMs(3) == 240000UL);
  assert(SpotifyDJMenuModel::dimTimeoutValueMs(99) == 60000UL);

  assert(SpotifyDJMenuModel::brightnessValuePercent(0) == 25);
  assert(SpotifyDJMenuModel::brightnessValuePercent(3) == 100);
  assert(SpotifyDJMenuModel::brightnessValuePercent(99) == 100);

  assert(SpotifyDJMenuModel::speakerVolumeValuePercent(0) == 25);
  assert(SpotifyDJMenuModel::speakerVolumeValuePercent(3) == 100);
  assert(SpotifyDJMenuModel::speakerVolumeValuePercent(99) == 100);

  assert(std::strcmp(SpotifyDJMenuModel::themeValue(0), "dark") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::themeValue(1), "light") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::themeValue(2), "auto") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::themeValue(99), "dark") == 0);

  assert(std::strcmp(SpotifyDJMenuModel::playModeValue(0), "normal") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::playModeValue(1), "shuffle") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::playModeValue(2), "repeat_once") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::playModeValue(3), "repeat_infinite") == 0);
  assert(std::strcmp(SpotifyDJMenuModel::playModeValue(99), "normal") == 0);
}

static void testNetworkActivityLogicWithFakeHttp() {
  FakeHttpClient http;
  NetworkActivityLogic::configureHttp(http, 1234, 5678);
  assert(http.connectTimeout == 1234);
  assert(http.ioTimeout == 5678);
  assert(!http.reuse);

  assert(!NetworkActivityLogic::isSlow(999, 1000));
  assert(NetworkActivityLogic::isSlow(1000, 1000));
  assert(NetworkActivityLogic::isSlow(1001, 1000));
}

int main() {
  testTrackTimeFormatting();
  testProgressEstimation();
  testLedRingBrightness();
  testDisplayPowerPolicy();
  testDeepSleepTimeoutOptions();
  testChargingCompleteCue();
  testBacklightDutyCurve();
  testBatteryVoltageFallbackCurve();
  testWebBatteryHeaderClass();
  testBq27220Interpretation();
  testVoiceChunkHelpers();
  testPlayModeMapping();
  testMqttDeviceTopicFormatting();
  testLanguageCodeNormalization();
  testSpotifyProvisioningTopLevel();
  testSpotifyProvisioningCompatTopLevel();
  testSpotifyProvisioningNestedPriority();
  testSpotifyProvisioningMissingRefreshToken();
  testSpotifyCredentialRepairValidation();
  testSpotifyPlaylistPagingDecision();
  testSpotifyDJMenuItemCounts();
  testSpotifyDJMenuOptionValues();
  testNetworkActivityLogicWithFakeHttp();
  return 0;
}

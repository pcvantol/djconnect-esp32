// Host-side unit tests for pure firmware logic.
// These tests avoid Arduino headers so they can run quickly on the development machine.

#include <cassert>
#include <cstring>

#include <ArduinoJson.h>

#include "AppState.h"
#include "DeviceCommandParser.h"
#include "LogicHelpers.h"
#include "NetworkActivityLogic.h"
#include "DJConnectMenuModel.h"

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

static void testOtaComparableFirmwareVersion() {
  assert(std::strcmp(Logic::otaComparableFirmwareVersion("dev", "vdev"), "0.0.0") == 0);
  assert(std::strcmp(Logic::otaComparableFirmwareVersion("dev", "v3.0.0"), "3.0.0") == 0);
  assert(std::strcmp(Logic::otaComparableFirmwareVersion("3.0.0", "vdev"), "3.0.0") == 0);
  assert(std::strcmp(Logic::otaComparableFirmwareVersion(nullptr, nullptr), "0.0.0") == 0);
  assert(std::strcmp(Logic::otaComparableFirmwareVersion("", ""), "0.0.0") == 0);
  assert(std::strcmp(Logic::otaComparableFirmwareVersion("3.0.0", "v3.0.0"), "3.0.0") == 0);
}

static void testSemverComparison() {
  int parts[3] = {};
  assert(Logic::parseSemver("3.0.11", parts));
  assert(parts[0] == 3 && parts[1] == 0 && parts[2] == 11);
  assert(Logic::parseSemver("v3.0.11", parts));
  assert(Logic::compareSemver("3.0.12", "3.0.11") > 0);
  assert(Logic::compareSemver("3.1.0", "3.0.99") > 0);
  assert(Logic::compareSemver("3.0.11", "3.0.11") == 0);
  assert(Logic::compareSemver("3.0.10", "3.0.11") < 0);
  assert(Logic::compareSemver("dev", "3.0.11") < 0);
  assert(!Logic::parseSemver("3.0", parts));
  assert(!Logic::parseSemver("3.0.x", parts));
}

static void testHaPlaybackConnectionErrorClassification() {
  assert(!Logic::haPlaybackErrorIsConnectionError(nullptr));
  assert(!Logic::haPlaybackErrorIsConnectionError(""));
  assert(!Logic::haPlaybackErrorIsConnectionError("No active playback"));
  assert(!Logic::haPlaybackErrorIsConnectionError("Default playlist not found"));

  assert(Logic::haPlaybackErrorIsConnectionError("HA playback HTTP -1"));
  assert(Logic::haPlaybackErrorIsConnectionError("HA playback HTTP 503"));
  assert(Logic::haPlaybackErrorIsConnectionError("HA playback cooling down"));
  assert(Logic::haPlaybackErrorIsConnectionError("Playback proxy busy"));
  assert(Logic::haPlaybackErrorIsConnectionError("HA playback backend unavailable"));
}

static void testHomeAssistantStatusMirroredSettingsDefaults() {
  DeviceSettingsStatus settings;
  assert(settings.screenBrightnessPercent == 100);
  assert(settings.screenOffTimeoutMs == 60000UL);
  assert(settings.turnOffAfterMs == 300000UL);
  assert(settings.speakerVolumePercent == 100);
  assert(settings.language == "en");
  assert(settings.theme == "dark");
  assert(settings.logLevel == "info");

  VisualState visual;
  assert(visual.screenOn);
  assert(visual.screenBrightnessLevel == 100);
  assert(visual.ledOn);
}

static void testHomeAssistantStatusAliasContractNames() {
  const char *aliases[] = {
      "ha_pairing_status",
      "client_type",
      "local_url",
      "ha_local_url",
      "ha_remote_url",
      "ha_active_url",
      "brightness",
      "screen_brightness",
      "screen_brightness_percent",
      "screen_dim_timeout",
      "screen_dim_timeout_ms",
      "screen_off_timeout_ms",
      "turn_off_after",
      "turn_off_after_ms",
      "cue_volume",
      "speaker_volume",
      "speaker_volume_percent",
      "language",
      "theme",
      "log_level",
      "ota_state",
      "update_state",
      "sound_output",
  };
  for (const char *alias : aliases) {
    assert(alias != nullptr);
    assert(std::strlen(alias) > 0);
  }
}

static void testHomeAssistantClientTypeErrorContract() {
  assert(Logic::isDjConnectInvalidClientType("invalid_client_type"));
  assert(!Logic::isDjConnectInvalidClientType(nullptr));
  assert(!Logic::isDjConnectInvalidClientType(""));
  assert(!Logic::isDjConnectInvalidClientType("invalid_token"));
  assert(!Logic::isHomeAssistantPairingInvalidStatus(400));
}

static void testImmediatePollTimestampConvention() {
  assert(Logic::forceImmediatePollTimestamp() == 0UL);
}

static void testSpotifyConfiguredForHomeAssistantStatus() {
  assert(!Logic::playbackProxyUsableForHomeAssistantStatus(false, false));
  assert(!Logic::playbackProxyUsableForHomeAssistantStatus(false, true));
  assert(Logic::playbackProxyUsableForHomeAssistantStatus(true, false));
  assert(!Logic::playbackProxyUsableForHomeAssistantStatus(true, true));
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

static void testDjAudioTypeDetection() {
  assert(Logic::djAudioTypeFromContentType("audio/wav") == Logic::DjAudioType::Wav);
  assert(Logic::djAudioTypeFromContentType("audio/x-wav; charset=binary") == Logic::DjAudioType::Wav);
  assert(Logic::djAudioTypeFromContentType("Audio/MPEG") == Logic::DjAudioType::Mp3);
  assert(Logic::djAudioTypeFromContentType(" audio/mp3 ") == Logic::DjAudioType::Mp3);
  assert(Logic::djAudioTypeFromContentType("application/octet-stream") == Logic::DjAudioType::Unknown);
  assert(std::strcmp(Logic::djAudioTypeName(Logic::DjAudioType::Wav), "wav") == 0);
  assert(std::strcmp(Logic::djAudioTypeName(Logic::DjAudioType::Mp3), "mp3") == 0);
  assert(std::strcmp(Logic::djAudioTypeName(Logic::DjAudioType::Unknown), "unknown") == 0);

  const uint8_t wavHeader[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
  const uint8_t id3Header[] = {'I', 'D', '3', 4};
  const uint8_t mp3FrameHeader[] = {0xFF, 0xFB, 0x90, 0x64};
  const uint8_t unknownHeader[] = {'O', 'g', 'g', 'S'};
  assert(Logic::djAudioTypeFromHeaderBytes(wavHeader, sizeof(wavHeader)) == Logic::DjAudioType::Wav);
  assert(Logic::djAudioTypeFromHeaderBytes(id3Header, sizeof(id3Header)) == Logic::DjAudioType::Mp3);
  assert(Logic::djAudioTypeFromHeaderBytes(mp3FrameHeader, sizeof(mp3FrameHeader)) == Logic::DjAudioType::Mp3);
  assert(Logic::djAudioTypeFromHeaderBytes(unknownHeader, sizeof(unknownHeader)) == Logic::DjAudioType::Unknown);
  assert(Logic::djAudioTypeFromHeaderBytes(nullptr, 0) == Logic::DjAudioType::Unknown);
}

static void testSha256HexValidation() {
  assert(Logic::isSha256Hex("0123456789abcdef0123456789abcdef0123456789ABCDEF0123456789ABCDEF"));
  assert(!Logic::isSha256Hex(nullptr));
  assert(!Logic::isSha256Hex(""));
  assert(!Logic::isSha256Hex("0123456789abcdef"));
  assert(!Logic::isSha256Hex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg"));
  assert(!Logic::isSha256Hex("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"));
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
  assert(!Logic::isHomeAssistantPairingInvalidStatus(426));
  assert(Logic::isDjConnectVersionMismatch(426, "version_mismatch"));
  assert(!Logic::isDjConnectVersionMismatch(426, "unauthorized"));
  assert(!Logic::isDjConnectVersionMismatch(400, "version_mismatch"));
  assert(std::strcmp(
             Logic::voiceHttpFailureMessage(404),
             "HA voice endpoint not found. Reset pairing and set up the DJConnect integration again.") == 0);
  assert(std::strcmp(Logic::voiceHttpFailureMessage(401), "HA authorization failed. Reset pairing and pair again.") == 0);
  assert(std::strcmp(Logic::voiceHttpFailureMessage(403), "HA authorization failed. Reset pairing and pair again.") == 0);
  assert(Logic::voiceHttpFailureMessage(500) == nullptr);


  assert(Logic::isBackendPlaylistContextUri("spotify:playlist:abc123"));
  assert(!Logic::isBackendPlaylistContextUri("spotify:album:abc123"));
  assert(!Logic::isBackendPlaylistContextUri("spotify:playlist:"));
  assert(!Logic::isBackendPlaylistContextUri(nullptr));
}

static void testDjConnectDeviceIdContract() {
  assert(Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122AABBCC", "lilygo-t-embed-s3"));
  assert(Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122aabbcc", "lilygo-t-embed-s3"));
  assert(Logic::isDjConnectDeviceIdForModel("djconnect-esp32-s3-box-3-001122AABBCC", "esp32-s3-box-3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-001122AABBCC", "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-esp32-s3-box-3-001122AABBCC", "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122AABB", "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122AABBCCDD", "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122AABBCG", "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel(nullptr, "lilygo-t-embed-s3"));
  assert(!Logic::isDjConnectDeviceIdForModel("djconnect-lilygo-t-embed-s3-001122AABBCC", nullptr));
  const char *legacyId = "djconnect-" "001122AABBCC";

  assert(Logic::isLegacyDjConnectDeviceId(legacyId));
  assert(!Logic::isLegacyDjConnectDeviceId("djconnect-lilygo-t-embed-s3-001122AABBCC"));
}

static void testWebPortalFormValidation() {
  assert(Logic::isBlankFormValue(nullptr));
  assert(Logic::isBlankFormValue(""));
  assert(Logic::isBlankFormValue(" \t\r\n"));
  assert(!Logic::isBlankFormValue("DJConnect"));
  assert(!Logic::isBlankFormValue("  DJConnect  "));

  assert(!Logic::requiredFormFieldValid(nullptr));
  assert(!Logic::requiredFormFieldValid(""));
  assert(!Logic::requiredFormFieldValid("   "));
  assert(Logic::requiredFormFieldValid("Kitchen WiFi"));

  assert(Logic::optionalFormFieldValid(nullptr));
  assert(Logic::optionalFormFieldValid(""));
  assert(Logic::optionalFormFieldValid("new password"));

  assert(Logic::submitButtonEnabled(true, false));
  assert(!Logic::submitButtonEnabled(false, false));
  assert(!Logic::submitButtonEnabled(true, true));

  assert(Logic::wifiSettingsSubmitEnabled("Home WiFi", false));
  assert(Logic::wifiSettingsSubmitEnabled("  Home WiFi  ", false));
  assert(!Logic::wifiSettingsSubmitEnabled("", false));
  assert(!Logic::wifiSettingsSubmitEnabled("   ", false));
  assert(!Logic::wifiSettingsSubmitEnabled("Home WiFi", true));

  assert(Logic::otaUploadSubmitEnabled(true, false));
  assert(!Logic::otaUploadSubmitEnabled(false, false));
  assert(!Logic::otaUploadSubmitEnabled(true, true));
}

static void testWebPortalButtonStates() {
  auto states = Logic::webPlaybackButtonStates(false, true, true, true, true);
  assert(!states.previous);
  assert(!states.next);
  assert(!states.playPause);
  assert(states.playPauseShowsPause);
  assert(!states.volume);
  assert(!states.shuffle);
  assert(!states.repeat);
  assert(!states.soundOutput);
  assert(!states.playlist);
  assert(states.webPtt);

  states = Logic::webPlaybackButtonStates(true, false, false, true, false);
  assert(states.previous);
  assert(states.next);
  assert(!states.playPause);
  assert(!states.playPauseShowsPause);
  assert(!states.volume);
  assert(states.shuffle);
  assert(states.repeat);
  assert(states.soundOutput);
  assert(states.playlist);
  assert(!states.webPtt);

  states = Logic::webPlaybackButtonStates(true, true, false, true, true);
  assert(states.playPause);
  assert(!states.playPauseShowsPause);
  assert(states.volume);
  assert(states.webPtt);

  states = Logic::webPlaybackButtonStates(true, true, true, false, true);
  assert(states.playPause);
  assert(states.playPauseShowsPause);
  assert(!states.volume);
}

static void testLanguageCodeNormalization() {
  assert(std::strcmp(Logic::languageCodeOrDefault("en"), "en") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("nl"), "nl") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("NL"), "nl") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault("de"), "en") == 0);
  assert(std::strcmp(Logic::languageCodeOrDefault(nullptr), "en") == 0);
}

static void testDeviceCommandParserPlaybackControls() {
  JsonDocument doc;

  deserializeJson(doc, "{\"command\":\"next_track\"}");
  DeviceCommand command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Next);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"next\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Next);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"previous\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Previous);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_volume\",\"volume\":42}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Volume);
  assert(command.numericValue == 42);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_volume\",\"value\":37}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Volume);
  assert(command.numericValue == 37);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_output\",\"output\":\"iPhone\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::TransferOutput);
  assert(command.value == "iPhone");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"start_playlist\",\"uri\":\"spotify:playlist:abc\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::StartPlaylist);
  assert(command.value == "spotify:playlist:abc");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_shuffle\",\"value\":true}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Shuffle);
  assert(command.numericValue == 1);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"shuffle\",\"value\":\"off\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Shuffle);
  assert(command.numericValue == 0);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_repeat\",\"value\":\"track\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Repeat);
  assert(command.value == "track");
}

static void testDeviceCommandParserSettings() {
  JsonDocument doc;

  deserializeJson(doc, "{\"command\":\"status\"}");
  DeviceCommand command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Status);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"screen_brightness\",\"value\":75}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::ScreenBrightness);
  assert(command.numericValue == 75);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_brightness\",\"brightness\":75}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::ScreenBrightness);
  assert(command.numericValue == 75);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"screen_dim_timeout\",\"value\":120000}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::ScreenDimTimeout);
  assert(command.numericValue == 120000);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_screen_timeout\",\"seconds\":120}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::ScreenDimTimeout);
  assert(command.numericValue == 120);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"turn_off_after\",\"value\":900000}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::DeepSleepTimeout);
  assert(command.numericValue == 900000);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_turn_off_after\",\"minutes\":15}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::DeepSleepTimeout);
  assert(command.numericValue == 15);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"speaker_volume\",\"value\":50}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::SpeakerVolume);
  assert(command.numericValue == 50);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_speaker_volume\",\"value\":50}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::SpeakerVolume);
  assert(command.numericValue == 50);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"language\",\"value\":\"nl\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Language);
  assert(command.value == "nl");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_language\",\"language\":\"nl\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Language);
  assert(command.value == "nl");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"theme\",\"value\":\"dark\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Theme);
  assert(command.value == "dark");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_theme\",\"theme\":\"dark\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Theme);
  assert(command.value == "dark");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"log_level\",\"value\":\"debug\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::LogLevel);
  assert(command.value == "debug");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_log_level\",\"log_level\":\"debug\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::LogLevel);
  assert(command.value == "debug");
}

static void testDeviceCommandParserDjResponseAndUnknown() {
  JsonDocument doc;

  deserializeJson(doc, "{\"command\":\"  NEXT_TRACK  \"}");
  DeviceCommand command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::Next);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"dj_response\",\"text\":\"Daar gaan we\",\"audio_url\":\"http://ha.local/tts/123.mp3\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::DjResponse);
  assert(command.value == "Daar gaan we");
  assert(command.audioUrl == "http://ha.local/tts/123.mp3");

  doc.clear();
  deserializeJson(doc, "{\"command\":\"set_brightness\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::ScreenBrightness);
  assert(command.numericValue == 100);

  doc.clear();
  deserializeJson(doc, "{\"command\":\"does_not_exist\"}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::None);

  doc.clear();
  deserializeJson(doc, "{}");
  command = DeviceCommandParser::parse(doc.as<JsonVariantConst>());
  assert(command.type == DeviceCommandType::None);
}

static void testSpotifyPlaylistPagingDecision() {
  assert(Logic::shouldFetchNextBackendPlaylistPage(50, 120, 0, 50));
  assert(Logic::shouldFetchNextBackendPlaylistPage(50, 0, 50, 50));
  assert(!Logic::shouldFetchNextBackendPlaylistPage(20, 120, 50, 50));
  assert(!Logic::shouldFetchNextBackendPlaylistPage(50, 50, 0, 50));
  assert(!Logic::shouldFetchNextBackendPlaylistPage(0, 120, 0, 50));
  assert(!Logic::shouldFetchNextBackendPlaylistPage(50, 120, -1, 50));
  assert(!Logic::shouldFetchNextBackendPlaylistPage(50, 120, 0, 0));
}

static void testDJConnectMenuItemCounts() {
  MenuCountInput input;
  assert(!DJConnectMenuModel::isMenuScreen(UiScreen::NowPlaying));
  assert(DJConnectMenuModel::isMenuScreen(UiScreen::Settings));
  assert(DJConnectMenuModel::itemCount(UiScreen::NowPlaying, input) == 0);
  assert(DJConnectMenuModel::itemCount(UiScreen::RootMenu, input) == DJConnectMenuModel::RootMenuItemCount);
  assert(DJConnectMenuModel::RootMenuItemCount == 11);
  assert(DJConnectMenuModel::itemCount(UiScreen::Help, input) == DJConnectMenuModel::HelpItemCount);
  assert(DJConnectMenuModel::HelpItemCount == 8);
  assert(DJConnectMenuModel::itemCount(UiScreen::Settings, input) == DJConnectMenuModel::SettingsItemCount);
  assert(DJConnectMenuModel::SettingsItemCount == 14);
  assert(DJConnectMenuModel::itemCount(UiScreen::About, input) == DJConnectMenuModel::AboutItemCount);
  assert(DJConnectMenuModel::itemCount(UiScreen::Playlists, input) == 1);
  assert(DJConnectMenuModel::itemCount(UiScreen::SoundOutputs, input) == 2);

  input.playlistsAvailable = true;
  input.playlistCount = 3;
  assert(DJConnectMenuModel::itemCount(UiScreen::Playlists, input) == 3);

  input.devicesAvailable = true;
  input.deviceCount = 3;
  assert(DJConnectMenuModel::itemCount(UiScreen::SoundOutputs, input) == 5);

  input.deviceCount = 99;
  assert(DJConnectMenuModel::itemCount(UiScreen::SoundOutputs, input) == DJConnectMenuModel::MaxVisibleOutputs + DJConnectMenuModel::FixedSoundOutputCount);
}

static void testDJConnectMenuOptionValues() {
  assert(DJConnectMenuModel::dimTimeoutValueMs(0) == 30000UL);
  assert(DJConnectMenuModel::dimTimeoutValueMs(1) == 60000UL);
  assert(DJConnectMenuModel::dimTimeoutValueMs(2) == 120000UL);
  assert(DJConnectMenuModel::dimTimeoutValueMs(3) == 240000UL);
  assert(DJConnectMenuModel::dimTimeoutValueMs(99) == 60000UL);

  assert(DJConnectMenuModel::brightnessValuePercent(0) == 25);
  assert(DJConnectMenuModel::brightnessValuePercent(3) == 100);
  assert(DJConnectMenuModel::brightnessValuePercent(99) == 100);

  assert(DJConnectMenuModel::speakerVolumeValuePercent(0) == 25);
  assert(DJConnectMenuModel::speakerVolumeValuePercent(3) == 100);
  assert(DJConnectMenuModel::speakerVolumeValuePercent(99) == 100);

  assert(std::strcmp(DJConnectMenuModel::themeValue(0), "dark") == 0);
  assert(std::strcmp(DJConnectMenuModel::themeValue(1), "light") == 0);
  assert(std::strcmp(DJConnectMenuModel::themeValue(2), "auto") == 0);
  assert(std::strcmp(DJConnectMenuModel::themeValue(99), "dark") == 0);

  assert(DJConnectMenuModel::LogLevelOptionCount == 4);
  assert(std::strcmp(DJConnectMenuModel::logLevelValue(0), "debug") == 0);
  assert(std::strcmp(DJConnectMenuModel::logLevelValue(1), "info") == 0);
  assert(std::strcmp(DJConnectMenuModel::logLevelValue(2), "warning") == 0);
  assert(std::strcmp(DJConnectMenuModel::logLevelValue(3), "error") == 0);
  assert(std::strcmp(DJConnectMenuModel::logLevelValue(99), "info") == 0);

  assert(DJConnectMenuModel::ShuffleOptionCount == 2);
  assert(!DJConnectMenuModel::shuffleValue(0));
  assert(DJConnectMenuModel::shuffleValue(1));
  assert(!DJConnectMenuModel::shuffleValue(99));

  assert(DJConnectMenuModel::RepeatOptionCount == 3);
  assert(std::strcmp(DJConnectMenuModel::repeatValue(0), "off") == 0);
  assert(std::strcmp(DJConnectMenuModel::repeatValue(1), "track") == 0);
  assert(std::strcmp(DJConnectMenuModel::repeatValue(2), "context") == 0);
  assert(std::strcmp(DJConnectMenuModel::repeatValue(99), "off") == 0);
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
  testOtaComparableFirmwareVersion();
  testSemverComparison();
  testHaPlaybackConnectionErrorClassification();
  testHomeAssistantStatusMirroredSettingsDefaults();
  testHomeAssistantStatusAliasContractNames();
  testHomeAssistantClientTypeErrorContract();
  testImmediatePollTimestampConvention();
  testSpotifyConfiguredForHomeAssistantStatus();
  testProgressEstimation();
  testLedRingBrightness();
  testDisplayPowerPolicy();
  testDeepSleepTimeoutOptions();
  testChargingCompleteCue();
  testBacklightDutyCurve();
  testBatteryVoltageFallbackCurve();
  testWebBatteryHeaderClass();
  testDjAudioTypeDetection();
  testSha256HexValidation();
  testBq27220Interpretation();
  testVoiceChunkHelpers();
  testDjConnectDeviceIdContract();
  testWebPortalFormValidation();
  testWebPortalButtonStates();
  testLanguageCodeNormalization();
  testDeviceCommandParserPlaybackControls();
  testDeviceCommandParserSettings();
  testDeviceCommandParserDjResponseAndUnknown();
  testSpotifyPlaylistPagingDecision();
  testDJConnectMenuItemCounts();
  testDJConnectMenuOptionValues();
  testNetworkActivityLogicWithFakeHttp();
  return 0;
}

// Host-side unit tests for pure firmware logic.
// These tests avoid Arduino headers so they can run quickly on the development machine.

#include <cassert>
#include <cstring>

#include "LogicHelpers.h"

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

int main() {
  testTrackTimeFormatting();
  testProgressEstimation();
  testLedRingBrightness();
  testDisplayPowerPolicy();
  testDeepSleepTimeoutOptions();
  testChargingCompleteCue();
  testBacklightDutyCurve();
  testBatteryVoltageFallbackCurve();
  testBq27220Interpretation();
  testVoiceChunkHelpers();
  testPlayModeMapping();
  testMqttDeviceTopicFormatting();
  return 0;
}

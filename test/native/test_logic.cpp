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
  assert(Logic::batteryPercentFromVoltage(3300) == 0);
  assert(Logic::batteryPercentFromVoltage(4146) == 95);
  assert(Logic::batteryPercentFromVoltage(4200) == 100);
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
  assert(stuckReading.percent == 95);
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
  assert(!discharging.percentEstimated);
  assert(discharging.percent == 75);
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
  assert(!full.percentEstimated);

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
  assert(!charging.percentEstimated);
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
  return 0;
}

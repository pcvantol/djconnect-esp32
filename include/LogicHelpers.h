// Pure calculation helpers that can be unit-tested on the host without Arduino hardware.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace Logic {

// Keeps percentage-like values inside the UI/API range used by Spotify and the LED ring.
inline int clampPercent(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

// Computes one LED's green brightness for the volume ring.
// Each LED represents 1/8 of the configured range, with the active edge LED partially lit.
inline int ledSegmentBrightness(int volumePercent, int segmentIndex, int segmentCount, int maxVolumePercent = 100) {
  if (segmentIndex < 0 || segmentIndex >= segmentCount || segmentCount <= 0) {
    return 0;
  }

  if (maxVolumePercent <= 0) {
    maxVolumePercent = 100;
  }
  if (volumePercent < 0) {
    volumePercent = 0;
  }
  if (volumePercent > maxVolumePercent) {
    volumePercent = maxVolumePercent;
  }

  const int scaledLevel = (volumePercent * segmentCount * 255) / maxVolumePercent;
  const int segmentLevel = scaledLevel - (segmentIndex * 255);
  if (segmentLevel < 0) {
    return 0;
  }
  if (segmentLevel > 255) {
    return 255;
  }
  return segmentLevel;
}

// Applies the display idle policy: full brightness, gradual dim, then completely off.
inline int displayPowerPercent(
    int activeBrightnessPercent,
    int dimBrightnessPercent,
    uint32_t dimStartAfterMs,
    uint32_t dimTargetAfterMs,
    uint32_t offAfterMs,
    uint32_t idleMs) {
  activeBrightnessPercent = clampPercent(activeBrightnessPercent);
  dimBrightnessPercent = clampPercent(dimBrightnessPercent);
  if (offAfterMs > 0 && idleMs >= offAfterMs) {
    return 0;
  }
  if (dimTargetAfterMs > 0 && idleMs >= dimTargetAfterMs) {
    return dimBrightnessPercent;
  }
  if (dimStartAfterMs > 0 && idleMs >= dimStartAfterMs && dimTargetAfterMs > dimStartAfterMs) {
    const uint32_t elapsed = idleMs - dimStartAfterMs;
    const uint32_t span = dimTargetAfterMs - dimStartAfterMs;
    const int delta = activeBrightnessPercent - dimBrightnessPercent;
    return activeBrightnessPercent - static_cast<int>((static_cast<uint32_t>(delta) * elapsed) / span);
  }
  return activeBrightnessPercent;
}

// Maps UI brightness to PWM duty. A quadratic curve makes 50% visibly dimmer than full brightness.
inline int backlightDutyForPercent(int percent, int maxDuty) {
  percent = clampPercent(percent);
  if (percent == 0 || maxDuty <= 0) {
    return 0;
  }

  int duty = (percent * percent * maxDuty) / 10000;
  if (duty <= 0) {
    duty = 1;
  }
  return duty > maxDuty ? maxDuty : duty;
}

// Returns the configured deep-sleep timeout option: 5, 15, 30, or 60 minutes.
inline uint32_t deepSleepTimeoutMsForIndex(size_t index) {
  static constexpr uint32_t values[] = {300000UL, 900000UL, 1800000UL, 3600000UL};
  const size_t count = sizeof(values) / sizeof(values[0]);
  return values[index < count ? index : 0];
}

// Maps a persisted timeout value back to the nearest menu selection, defaulting to 5 minutes.
inline size_t deepSleepTimeoutIndexForMs(uint32_t timeoutMs) {
  static constexpr uint32_t values[] = {300000UL, 900000UL, 1800000UL, 3600000UL};
  const size_t count = sizeof(values) / sizeof(values[0]);
  for (size_t index = 0; index < count; index++) {
    if (values[index] == timeoutMs) {
      return index;
    }
  }
  return 0;
}

// True exactly when the firmware should emit the once-per-charge near-full cue.
inline bool shouldPlayChargingCompleteCue(bool chargerConnected, int percent, bool alreadyPlayed) {
  return chargerConnected && percent > 90 && !alreadyPlayed;
}

// Voltage-based Li-ion estimate. The BQ27220 SoC register is kept only as diagnostics.
inline int batteryPercentFromVoltage(int voltageMv) {
  if (voltageMv >= 4150) return 100;
  if (voltageMv >= 4100) return 90;
  if (voltageMv >= 4000) return 80;
  if (voltageMv >= 3900) return 65;
  if (voltageMv >= 3800) return 50;
  if (voltageMv >= 3700) return 35;
  if (voltageMv >= 3600) return 20;
  if (voltageMv >= 3500) return 10;
  if (voltageMv >= 3300) return 5;
  return 0;
}

// Estimates the current playback position between Spotify polling calls.
inline int estimatedProgressMs(
    int progressMs,
    int durationMs,
    bool isPlaying,
    uint32_t progressSyncedAt,
    uint32_t nowMs) {
  if (durationMs <= 0) {
    return 0;
  }

  int estimated = progressMs;
  if (isPlaying && progressSyncedAt > 0 && nowMs >= progressSyncedAt) {
    estimated += static_cast<int>(nowMs - progressSyncedAt);
  }

  if (estimated < 0) {
    return 0;
  }
  if (estimated > durationMs) {
    return durationMs;
  }
  return estimated;
}

// Formats milliseconds as M:SS or H:MM:SS into a caller-provided buffer.
inline bool formatTrackTime(int ms, char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) {
    return false;
  }

  const int totalSeconds = ms > 0 ? ms / 1000 : 0;
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds / 60) % 60;
  const int seconds = totalSeconds % 60;

  const int written = hours > 0
                          ? snprintf(buffer, bufferSize, "%d:%02d:%02d", hours, minutes, seconds)
                          : snprintf(buffer, bufferSize, "%d:%02d", minutes, seconds);
  return written >= 0 && static_cast<size_t>(written) < bufferSize;
}

struct Bq27220Reading {
  bool available = false;
  bool charging = false;
  bool discharging = false;
  bool full = false;
  bool percentEstimated = false;
  int percent = -1;
  int gaugePercent = -1;
  int voltageMv = 0;
  int currentMa = 0;
};

// Interprets BQ27220 standard-command reads into the state shown by the UI.
inline Bq27220Reading interpretBq27220(
    uint16_t stateOfCharge,
    bool hasVoltage,
    uint16_t voltage,
    bool hasStatus,
    uint16_t status,
    bool hasCurrent,
    uint16_t currentRaw,
    int chargeCurrentThresholdMa) {
  Bq27220Reading reading;
  reading.available = true;
  reading.gaugePercent = clampPercent(static_cast<int>(stateOfCharge));
  reading.percent = reading.gaugePercent;
  reading.voltageMv = hasVoltage ? static_cast<int>(voltage) : 0;

  if (hasStatus) {
    // BatteryStatus bit 0 is DSG; bit 9 is FC in the BQ27220 technical reference manual.
    reading.discharging = (status & 0x0001) != 0;
    reading.full = (status & 0x0200) != 0;
  }

  if (hasCurrent) {
    reading.currentMa = static_cast<int16_t>(currentRaw);
  }

  const int absoluteCurrent = reading.currentMa < 0 ? -reading.currentMa : reading.currentMa;
  const bool hasChargeCurrent = !hasCurrent || absoluteCurrent >= chargeCurrentThresholdMa;
  reading.charging = !reading.discharging && !reading.full && hasChargeCurrent;

  if (hasVoltage) {
    reading.percent = batteryPercentFromVoltage(reading.voltageMv);
    reading.percentEstimated = true;
  }

  return reading;
}

}  // namespace Logic

// Pure calculation helpers that can be unit-tested on the host without Arduino hardware.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

// Computes one LED's brightness for the volume ring, independent of the final UI color.
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

// CSS state used by the web header battery icon. Charging is added beside the color class.
inline const char *batteryHeaderClass(int percent, bool charging) {
  const char *level = percent < 20 ? "low" : percent < 50 ? "medium" : "high";
  if (!charging) {
    return level;
  }
  if (percent < 20) {
    return "charging low";
  }
  if (percent < 50) {
    return "charging medium";
  }
  return "charging high";
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

// Voice chunks sent to HA Assist are prefixed later with the one-byte STT handler id.
inline size_t voiceAudioPayloadBytes(size_t availableBytes, size_t maxChunkBytes) {
  if (availableBytes == 0 || maxChunkBytes == 0) {
    return 0;
  }
  return availableBytes < maxChunkBytes ? availableBytes : maxChunkBytes;
}

// HA Assist binary audio frames include one handler-id byte before PCM payload bytes.
inline size_t voiceAssistBinaryFrameBytes(size_t availableBytes, size_t maxChunkBytes) {
  const size_t payloadBytes = voiceAudioPayloadBytes(availableBytes, maxChunkBytes);
  return payloadBytes == 0 ? 0 : payloadBytes + 1;
}

// Centralizes the "recording too long" boundary used by push-to-talk.
inline bool shouldAutoStopVoiceRecording(uint32_t elapsedMs, uint32_t maxRecordMs) {
  return maxRecordMs > 0 && elapsedMs >= maxRecordMs;
}

// Prevents empty STT results from reaching the SpotifyDJ integration endpoint.
inline bool shouldSendRecognizedVoiceText(size_t textLength) {
  return textLength > 0;
}

// Converts HA voice endpoint failures into user-facing diagnostics.
inline bool isHomeAssistantPairingInvalidStatus(int statusCode) {
  return statusCode == 401 || statusCode == 403 || statusCode == 404;
}

inline const char *voiceHttpFailureMessage(int statusCode) {
  if (statusCode == 404) {
    return "HA voice endpoint not found. Reset pairing and set up the SpotifyDJ integration again.";
  }
  if (statusCode == 401 || statusCode == 403) {
    return "HA authorization failed. Reset pairing and pair again.";
  }
  return nullptr;
}

// ESP32 Preferences/NVS keys are limited to 15 characters.
inline bool preferencesKeyFits(const char *key) {
  return key != nullptr && strlen(key) <= 15;
}

inline bool shouldLockMqttAuthRetries(int connectCode, uint8_t authFailureCount, uint8_t maxAuthFailures) {
  const bool authFailure = connectCode == 4 || connectCode == 5;
  return authFailure && maxAuthFailures > 0 && authFailureCount >= maxAuthFailures;
}

inline bool isSpotifyPlaylistContextUri(const char *uri) {
  static constexpr const char *prefix = "spotify:playlist:";
  return uri != nullptr && strncmp(uri, prefix, strlen(prefix)) == 0 && uri[strlen(prefix)] != '\0';
}

// True when Spotify's paged playlist response indicates there can be another page to inspect.
inline bool shouldFetchNextSpotifyPlaylistPage(size_t itemCount, int total, int offset, int pageSize) {
  if (itemCount == 0 || pageSize <= 0 || offset < 0) {
    return false;
  }
  if (itemCount < static_cast<size_t>(pageSize)) {
    return false;
  }
  return total <= 0 || offset + static_cast<int>(itemCount) < total;
}

// Formats the HA-provisioned per-device MQTT topics without depending on Arduino String.
inline bool formatMqttDeviceTopic(const char *deviceId, const char *suffix, char *buffer, size_t bufferSize) {
  if (deviceId == nullptr || suffix == nullptr || buffer == nullptr || bufferSize == 0) {
    return false;
  }
  const int written = snprintf(buffer, bufferSize, "spotifydj/%s/%s", deviceId, suffix);
  return written >= 0 && static_cast<size_t>(written) < bufferSize;
}

// Converts Spotify's shuffle/repeat fields into the four UI modes exposed in settings.
inline const char *playModeFromSpotifyState(bool shuffle, const char *repeatState) {
  if (repeatState != nullptr && strcmp(repeatState, "track") == 0) {
    return "repeat_once";
  }
  if (repeatState != nullptr && strcmp(repeatState, "context") == 0) {
    return "repeat_infinite";
  }
  return shuffle ? "shuffle" : "normal";
}

// Labels stay centralized so the device menu, web UI defaults, and tests agree.
inline const char *playModeLabel(const char *mode) {
  if (mode != nullptr && strcmp(mode, "shuffle") == 0) {
    return "Shuffle";
  }
  if (mode != nullptr && strcmp(mode, "repeat_once") == 0) {
    return "Repeat once";
  }
  if (mode != nullptr && strcmp(mode, "repeat_infinite") == 0) {
    return "Repeat infinite";
  }
  return "No shuffle";
}

// Normalizes persisted/UI language codes. Unknown values intentionally fall back to English.
inline const char *languageCodeOrDefault(const char *code) {
  if (code == nullptr) {
    return "en";
  }
  return (strcmp(code, "nl") == 0 || strcmp(code, "NL") == 0) ? "nl" : "en";
}

// Validates manual Spotify credential repair input before it touches NVS.
inline bool spotifyRepairCredentialsValid(const char *storedClientId, const char *submittedClientId, const char *submittedRefreshToken) {
  const bool hasStoredClientId = storedClientId != nullptr && storedClientId[0] != '\0';
  const bool hasSubmittedClientId = submittedClientId != nullptr && submittedClientId[0] != '\0';
  const bool hasRefreshToken = submittedRefreshToken != nullptr && submittedRefreshToken[0] != '\0';
  return hasRefreshToken && (hasStoredClientId || hasSubmittedClientId);
}

inline const char *spotifyMarketOrDefault(const char *market) {
  return market != nullptr && market[0] != '\0' ? market : "NL";
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

  const bool hasChargeCurrent = hasCurrent && reading.currentMa >= chargeCurrentThresholdMa;
  reading.charging = !reading.discharging && !reading.full && hasChargeCurrent;

  if (hasVoltage) {
    reading.percent = batteryPercentFromVoltage(reading.voltageMv);
    reading.percentEstimated = true;
  }

  return reading;
}

}  // namespace Logic

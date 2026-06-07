// Central hardware pinout and timing constants for the LilyGO T-Embed-CC1101 build.
// Keeping these in one namespace makes board or UX tuning changes easy to find.
#pragma once

#ifndef SPOTIFYDJ_VERSION
#define SPOTIFYDJ_VERSION dev
#endif

#ifndef SPOTIFYDJ_VERSION_TAG
#define SPOTIFYDJ_VERSION_TAG vdev
#endif

#include <Arduino.h>

#define SPOTIFYDJ_STRINGIFY_VALUE(value) #value
#define SPOTIFYDJ_STRINGIFY(value) SPOTIFYDJ_STRINGIFY_VALUE(value)

namespace Config {
static const char *const AppVersion = SPOTIFYDJ_STRINGIFY(SPOTIFYDJ_VERSION_TAG);
static const char *const AppVersionNumber = SPOTIFYDJ_STRINGIFY(SPOTIFYDJ_VERSION);
static const char *const BuildMarker =
    "SpotifyDJ " SPOTIFYDJ_STRINGIFY(SPOTIFYDJ_VERSION_TAG) " / " SPOTIFYDJ_STRINGIFY(SPOTIFYDJ_VERSION) " booting";

// Board power, storage, radio, display, I2C, encoder, and WS2812 pin mapping.
static constexpr uint8_t BoardUserKeyPin = 6;
static constexpr uint8_t BoardPowerEnablePin = 15;
static constexpr uint8_t DisplayBacklightPin = 21;
static constexpr uint8_t SdCardChipSelectPin = 13;
static constexpr uint8_t LoraChipSelectPin = 12;
static constexpr uint8_t I2cSdaPin = 8;
static constexpr uint8_t I2cSclPin = 18;
static constexpr uint8_t Ws2812LedCount = 8;
static constexpr uint8_t Ws2812DataPin = 14;
static constexpr uint8_t LedRingBrightness = 48;
static constexpr uint8_t DisplayBacklightPwmChannel = 0;
static constexpr uint8_t DisplayBacklightPwmResolution = 8;
static constexpr uint8_t EncoderPinA = 4;
static constexpr uint8_t EncoderPinB = 5;
static constexpr uint8_t EncoderButtonPin = 0;
static constexpr uint8_t SpeakerBclkPin = 46;
static constexpr uint8_t SpeakerLrclkPin = 40;
static constexpr uint8_t SpeakerDataPin = 7;
static constexpr uint8_t MicrophoneDataPin = 42;
static constexpr uint8_t MicrophoneClockPin = 39;

// BQ27220 standard command registers used by BatteryMonitor.
static constexpr uint8_t Bq27220I2cAddress = 0x55;
static constexpr uint8_t Bq27220BatteryStatusCommand = 0x0A;
static constexpr uint8_t Bq27220CurrentCommand = 0x0C;
static constexpr uint8_t Bq27220VoltageCommand = 0x08;
static constexpr uint8_t Bq27220StateOfChargeCommand = 0x2C;

// Polling, debounce-adjacent, and display animation timings.
static constexpr uint32_t PlaybackPollIntervalMs = 15000;
static constexpr uint32_t BatteryPollIntervalMs = 15000;
static constexpr uint32_t HaStatusIntervalMs = 60000;
static constexpr uint32_t VolumeFlushDelayMs = 450;
static constexpr uint32_t SoftResetHoldMs = 10000;
static constexpr uint32_t HardResetComboHoldMs = 10000;
static constexpr uint32_t DisplayDimStartAfterMs = 10000;
static constexpr uint32_t DisplayDimAfterMs = 20000;
static constexpr uint32_t DisplayOffAfterMs = 60000;
static constexpr uint32_t DeviceSleepAfterMs = 300000;
static constexpr uint32_t TitleScrollStartDelayMs = 1000;
static constexpr uint32_t TitleScrollFrameMs = 90;
static constexpr uint32_t DisplayBacklightPwmFrequency = 5000;
static constexpr uint32_t HttpConnectTimeoutMs = 4000;
static constexpr uint32_t HttpIoTimeoutMs = 5000;
static constexpr uint32_t HttpLongIoTimeoutMs = 15000;
static constexpr uint32_t VoiceCommandIoTimeoutMs = 60000;
static constexpr uint32_t WakeWordPollIntervalMs = 40;
static constexpr size_t WakeWordPcmChunkBytes = 512;
static constexpr uint32_t StressTestStepIntervalMs = 250;
static constexpr uint32_t StressTestDurationMs = 120000;
static constexpr uint32_t DjAudioIoTimeoutMs = 20000;
static constexpr uint32_t OtaConnectTimeoutMs = 8000;
static constexpr uint32_t OtaIoTimeoutMs = 15000;
static constexpr uint32_t OtaStreamIdleTimeoutMs = 15000;
static constexpr uint32_t TlsHandshakeTimeoutMs = 5000;
static constexpr uint32_t WatchdogTimeoutSeconds = 12;
static constexpr uint32_t SlowLoopWarningMs = 250;
static constexpr uint32_t HeapLogIntervalMs = 300000;
static constexpr uint32_t WifiConnectTimeoutMs = 30000;
static constexpr uint32_t WifiFailureSleepAfterMs = 120000;
static constexpr uint64_t LowBatteryWakeCheckUs = 30000000ULL;
static constexpr uint64_t UsbAttachWakePollUs = 30000000ULL;
static constexpr uint32_t ProvisioningPortalTimeoutMs = 600000;
static constexpr uint32_t PairingModeTimeoutMs = 600000;
static constexpr uint32_t SetupPromptBeepIntervalMs = 30000;
static constexpr uint32_t SetupPromptBeepDurationMs = 600000;
static constexpr uint32_t SpeakerSampleRate = 16000;
static constexpr uint32_t VoiceSampleRate = 16000;
static constexpr uint32_t VoiceMaxRecordMs = 15000;
static constexpr size_t VoiceMaxWavBytes = 2UL * 1024UL * 1024UL;
static constexpr size_t VoicePcmChunkBytes = 1024;

// UI tuning constants.
static constexpr int TitleScrollGapPx = 36;
static constexpr int VolumeStepPercent = 5;
static constexpr int MaxSpotifyVolumePercent = 60;
static constexpr int BatteryChargeCurrentThresholdMa = 5;
static constexpr int DisplayDimBrightnessPercent = 50;

// Captive portal setup mode.
static const char *const ProvisioningApSsid = "SpotifyDJ Setup";

// Playback backend credentials and API endpoints live in Home Assistant, not on the ESP.
static const char *const LikedProxyPlaylistName = "SpotifyDJ Liked Proxy";
static const char *const BootstrapFirmwareReleaseApiUrl =
    "https://api.github.com/repos/pcvantol/spotify-dj-firmware/releases/latest";
static const char *const BootstrapFirmwareManifestAsset = "firmware_manifest.json";

// POSIX timezone rule for Europe/Amsterdam, including CET/CEST daylight saving changes.
static const char *const AmsterdamTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
}  // namespace Config

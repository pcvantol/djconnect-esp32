// Central hardware pinout and timing constants for the LilyGO T-Embed-CC1101 build.
// Keeping these in one namespace makes board or UX tuning changes easy to find.
#pragma once

#ifndef DJCONNECT_VERSION
#define DJCONNECT_VERSION dev
#endif

#ifndef DJCONNECT_VERSION_TAG
#define DJCONNECT_VERSION_TAG vdev
#endif

#include <Arduino.h>

#include "BoardProfile.h"

#define DJCONNECT_STRINGIFY_VALUE(value) #value
#define DJCONNECT_STRINGIFY(value) DJCONNECT_STRINGIFY_VALUE(value)

namespace Config {
static const char *const AppVersion = DJCONNECT_STRINGIFY(DJCONNECT_VERSION_TAG);
static const char *const AppVersionNumber = DJCONNECT_STRINGIFY(DJCONNECT_VERSION);
static const char *const AppTagline = "Muziekbediening met karakter";
static const char *const WebsiteUrl = "https://djconnect.dev";
static const char *const DefaultHomeAssistantUrl = "http://homeassistant.local:8123";
static const char *const BuildMarker =
    "DJConnect " DJCONNECT_STRINGIFY(DJCONNECT_VERSION_TAG) " / " DJCONNECT_STRINGIFY(DJCONNECT_VERSION) " booting";

// Board power, storage, radio, display, I2C, encoder, and WS2812 pin mapping.
static const char *const BoardName = BoardProfile::BoardName;
static const char *const DeviceModel = BoardProfile::DeviceModel;
static constexpr bool HasBoardPowerEnable = BoardProfile::HasBoardPowerEnable;
static constexpr bool HasSdCardChipSelect = BoardProfile::HasSdCardChipSelect;
static constexpr bool HasLoraChipSelect = BoardProfile::HasLoraChipSelect;
static constexpr bool HasLedRing = BoardProfile::HasLedRing;
static constexpr bool HasBq27220BatteryGauge = BoardProfile::HasBq27220BatteryGauge;
static constexpr bool HasSpeaker = BoardProfile::HasSpeaker;
static constexpr bool HasMicrophone = BoardProfile::HasMicrophone;
static constexpr bool HasRotaryEncoder = BoardProfile::HasRotaryEncoder;
static constexpr bool HasSideRotationButtons = BoardProfile::HasSideRotationButtons;
static constexpr int BoardUserKeyPin = BoardProfile::BoardUserKeyPin;
static constexpr int BoardPowerEnablePin = BoardProfile::BoardPowerEnablePin;
static constexpr int DisplayBacklightPin = BoardProfile::DisplayBacklightPin;
static constexpr uint8_t DisplayRotation = BoardProfile::DisplayRotation;
static constexpr int SdCardChipSelectPin = BoardProfile::SdCardChipSelectPin;
static constexpr int LoraChipSelectPin = BoardProfile::LoraChipSelectPin;
static constexpr int I2cSdaPin = BoardProfile::I2cSdaPin;
static constexpr int I2cSclPin = BoardProfile::I2cSclPin;
static constexpr uint8_t Ws2812LedCount = BoardProfile::Ws2812LedCount;
static constexpr int Ws2812DataPin = BoardProfile::Ws2812DataPin;
static constexpr uint8_t LedRingBrightness = 48;
static constexpr uint8_t DisplayBacklightPwmResolution = 8;
static constexpr int EncoderPinA = BoardProfile::EncoderPinA;
static constexpr int EncoderPinB = BoardProfile::EncoderPinB;
static constexpr int EncoderButtonPin = BoardProfile::EncoderButtonPin;
static constexpr int SpeakerBclkPin = BoardProfile::SpeakerBclkPin;
static constexpr int SpeakerLrclkPin = BoardProfile::SpeakerLrclkPin;
static constexpr int SpeakerDataPin = BoardProfile::SpeakerDataPin;
static constexpr int MicrophoneDataPin = BoardProfile::MicrophoneDataPin;
static constexpr int MicrophoneClockPin = BoardProfile::MicrophoneClockPin;

// BQ27220 standard command registers used by BatteryMonitor.
static constexpr uint8_t Bq27220I2cAddress = 0x55;
static constexpr uint8_t Bq27220BatteryStatusCommand = 0x0A;
static constexpr uint8_t Bq27220CurrentCommand = 0x0C;
static constexpr uint8_t Bq27220VoltageCommand = 0x08;
static constexpr uint8_t Bq27220StateOfChargeCommand = 0x2C;

// Polling, debounce-adjacent, and display animation timings.
static constexpr uint32_t PlaybackPollIntervalMs = 15000;
static constexpr uint32_t PlaybackBootGraceMs = 30000;
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
static constexpr uint32_t WebVoiceTextOnlySuppressMs = 90000;
static constexpr uint32_t WakeWordPollIntervalMs = 10;
static constexpr size_t WakeWordPcmChunkBytes = 640;
static constexpr float WakeWordProbabilityCutoff = BoardProfile::WakeWordProbabilityCutoff;
static constexpr uint32_t StressTestStepIntervalMs = 250;
static constexpr uint32_t StressTestDurationMs = 120000;
static constexpr uint32_t DjAudioIoTimeoutMs = 20000;
static constexpr uint32_t OtaConnectTimeoutMs = 8000;
static constexpr uint32_t OtaIoTimeoutMs = 90000;
static constexpr uint32_t OtaStreamIdleTimeoutMs = 180000;
static constexpr uint32_t TlsHandshakeTimeoutMs = 5000;
static constexpr uint32_t WatchdogTimeoutSeconds = 12;
static constexpr uint32_t SlowLoopWarningMs = 250;
static constexpr uint32_t HeapLogIntervalMs = 300000;
static constexpr uint32_t WifiConnectTimeoutMs = 15000;
static constexpr uint32_t WifiFailureSleepAfterMs = 120000;
static constexpr uint64_t LowBatteryWakeCheckUs = 30000000ULL;
static constexpr uint64_t UsbAttachWakePollUs = 30000000ULL;
static constexpr uint32_t ProvisioningPortalTimeoutMs = 600000;
static constexpr uint32_t PairingModeTimeoutMs = 600000;
static constexpr uint32_t SetupPromptBeepIntervalMs = 30000;
static constexpr uint32_t SetupPromptBeepDurationMs = 600000;
static constexpr uint32_t SpeakerSampleRate = 16000;
static constexpr uint32_t VoiceSampleRate = 16000;
static constexpr uint32_t VoiceMinRecordMs = 1800;
static constexpr uint32_t VoiceMaxRecordMs = 15000;
static constexpr uint32_t VoiceCueSettleMs = 90;
static constexpr uint32_t VoiceSilenceStopMs = 1200;
static constexpr uint16_t VoiceSilenceRmsThreshold = 220;
static constexpr size_t VoiceMaxWavBytes = 2UL * 1024UL * 1024UL;
static constexpr size_t VoicePcmChunkBytes = 1024;

// UI tuning constants.
static constexpr int TitleScrollGapPx = 36;
static constexpr int VolumeStepPercent = 5;
static constexpr int MaxSpotifyVolumePercent = 60;
static constexpr int BatteryChargeCurrentThresholdMa = 5;
static constexpr int DisplayDimBrightnessPercent = 50;
static constexpr uint32_t WakeSplashDurationMs = 750;

// Captive portal setup mode.
static const char *const ProvisioningApSsid = "DJConnect Setup";

// Playback backend credentials and API endpoints live in Home Assistant, not on the ESP.
static const char *const LikedProxyPlaylistName = "DJConnect Default Playlist";
static const char *const BootstrapFirmwareReleaseApiUrl =
    "https://api.github.com/repos/pcvantol/djconnect-firmware/releases/latest";
static const char *const BootstrapFirmwareManifestAsset = "firmware_manifest.json";

// POSIX timezone rule for Europe/Amsterdam, including CET/CEST daylight saving changes.
static const char *const AmsterdamTimezone = "CET-1CEST,M3.5.0/2,M10.5.0/3";
}  // namespace Config

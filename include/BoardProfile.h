// Board-specific pinout and capabilities.
#pragma once

#include <Arduino.h>

namespace BoardProfile {

static constexpr int OptionalPin = -1;

#if defined(DJCONNECT_BOARD_BOX3)

static const char *const BoardName = "ESP32-S3-BOX-3";
static const char *const DeviceModel = "esp32-s3-box-3";

static constexpr bool HasBoardPowerEnable = false;
static constexpr bool HasSdCardChipSelect = false;
static constexpr bool HasLoraChipSelect = false;
static constexpr bool HasLedRing = false;
static constexpr bool HasBq27220BatteryGauge = false;
static constexpr bool HasSpeaker = false;
static constexpr bool HasMicrophone = false;

static constexpr int BoardUserKeyPin = 0;
static constexpr int BoardPowerEnablePin = OptionalPin;
static constexpr int DisplayBacklightPin = 47;
static constexpr int SdCardChipSelectPin = OptionalPin;
static constexpr int LoraChipSelectPin = OptionalPin;
static constexpr int I2cSdaPin = 8;
static constexpr int I2cSclPin = 18;
static constexpr uint8_t Ws2812LedCount = 1;
static constexpr int Ws2812DataPin = 14;
static constexpr int EncoderPinA = 1;
static constexpr int EncoderPinB = 2;
static constexpr int EncoderButtonPin = 0;

// First-pass BOX-3 audio defaults. The display bring-up path is the initial target;
// verify these against hardware before enabling voice/speaker as release-supported.
static constexpr int SpeakerBclkPin = 17;
static constexpr int SpeakerLrclkPin = 47;
static constexpr int SpeakerDataPin = 15;
static constexpr int MicrophoneDataPin = 40;
static constexpr int MicrophoneClockPin = 41;

#else

static const char *const BoardName = "LilyGO T-Embed-CC1101";
static const char *const DeviceModel = "lilygo-t-embed-s3";

static constexpr bool HasBoardPowerEnable = true;
static constexpr bool HasSdCardChipSelect = true;
static constexpr bool HasLoraChipSelect = true;
static constexpr bool HasLedRing = true;
static constexpr bool HasBq27220BatteryGauge = true;
static constexpr bool HasSpeaker = true;
static constexpr bool HasMicrophone = true;

static constexpr int BoardUserKeyPin = 6;
static constexpr int BoardPowerEnablePin = 15;
static constexpr int DisplayBacklightPin = 21;
static constexpr int SdCardChipSelectPin = 13;
static constexpr int LoraChipSelectPin = 12;
static constexpr int I2cSdaPin = 8;
static constexpr int I2cSclPin = 18;
static constexpr uint8_t Ws2812LedCount = 8;
static constexpr int Ws2812DataPin = 14;
static constexpr int EncoderPinA = 4;
static constexpr int EncoderPinB = 5;
static constexpr int EncoderButtonPin = 0;
static constexpr int SpeakerBclkPin = 46;
static constexpr int SpeakerLrclkPin = 40;
static constexpr int SpeakerDataPin = 7;
static constexpr int MicrophoneDataPin = 42;
static constexpr int MicrophoneClockPin = 39;

#endif

inline bool validPin(int pin) {
  return pin >= 0;
}

}  // namespace BoardProfile

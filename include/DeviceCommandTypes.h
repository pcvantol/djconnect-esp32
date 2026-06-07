#pragma once

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

enum class DeviceCommandType {
  None,
  Next,
  Previous,
  Status,
  Ota,
  Volume,
  TransferOutput,
  StartPlaylist,
  DjResponse,
  ScreenBrightness,
  ScreenDimTimeout,
  DeepSleepTimeout,
  SpeakerVolume,
  Language,
  Theme,
  LogLevel,
};

struct DeviceCommand {
  DeviceCommandType type = DeviceCommandType::None;
  String value;
  String audioUrl;
  int numericValue = 0;
};

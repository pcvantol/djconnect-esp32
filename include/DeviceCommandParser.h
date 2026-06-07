#pragma once

#include <ArduinoJson.h>

#include "DeviceCommandTypes.h"

namespace DeviceCommandParser {

inline char lowerAscii(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

inline bool commandNameEquals(const char *rawName, const char *expected) {
  if (rawName == nullptr || expected == nullptr) {
    return false;
  }
  while (*rawName == ' ' || *rawName == '\t' || *rawName == '\r' || *rawName == '\n') {
    rawName++;
  }
  while (*expected != '\0') {
    if (lowerAscii(*rawName) != *expected) {
      return false;
    }
    rawName++;
    expected++;
  }
  while (*rawName == ' ' || *rawName == '\t' || *rawName == '\r' || *rawName == '\n') {
    rawName++;
  }
  return *rawName == '\0';
}

inline String firstString(JsonVariantConst payload, const char *primary, const char *secondary = nullptr, const char *tertiary = nullptr) {
  if (payload[primary].is<const char *>()) {
    return payload[primary].as<const char *>();
  }
  if (secondary != nullptr && payload[secondary].is<const char *>()) {
    return payload[secondary].as<const char *>();
  }
  if (tertiary != nullptr && payload[tertiary].is<const char *>()) {
    return payload[tertiary].as<const char *>();
  }
  return "";
}

inline int firstInt(JsonVariantConst payload, const char *primary, const char *secondary, int fallback) {
  if (payload[primary].is<int>()) {
    return payload[primary].as<int>();
  }
  if (secondary != nullptr && payload[secondary].is<int>()) {
    return payload[secondary].as<int>();
  }
  return fallback;
}

// Parses Home Assistant native command payloads posted to /api/device/command.
inline DeviceCommand parse(JsonVariantConst payload) {
  DeviceCommand command;
  const char *name = payload["command"] | "";
  if (commandNameEquals(name, "next") || commandNameEquals(name, "next_track")) {
    command.type = DeviceCommandType::Next;
  } else if (commandNameEquals(name, "previous") || commandNameEquals(name, "previous_track")) {
    command.type = DeviceCommandType::Previous;
  } else if (commandNameEquals(name, "status") || commandNameEquals(name, "refresh_status")) {
    command.type = DeviceCommandType::Status;
  } else if (commandNameEquals(name, "dj_response")) {
    command.type = DeviceCommandType::DjResponse;
    command.value = firstString(payload, "text");
  } else if (commandNameEquals(name, "set_volume") || commandNameEquals(name, "volume")) {
    command.type = DeviceCommandType::Volume;
    command.numericValue = firstInt(payload, "value", "volume", 0);
  } else if (commandNameEquals(name, "set_output") || commandNameEquals(name, "transfer_output")) {
    command.type = DeviceCommandType::TransferOutput;
    command.value = firstString(payload, "value", "output");
  } else if (commandNameEquals(name, "start_playlist")) {
    command.type = DeviceCommandType::StartPlaylist;
    command.value = firstString(payload, "value", "playlist", "uri");
  } else if (commandNameEquals(name, "set_brightness")) {
    command.type = DeviceCommandType::ScreenBrightness;
    command.numericValue = firstInt(payload, "value", "brightness", 100);
  } else if (commandNameEquals(name, "set_screen_timeout") || commandNameEquals(name, "set_dim_timeout")) {
    command.type = DeviceCommandType::ScreenDimTimeout;
    command.numericValue = firstInt(payload, "value", "seconds", 60);
  } else if (commandNameEquals(name, "set_turn_off_after")) {
    command.type = DeviceCommandType::DeepSleepTimeout;
    command.numericValue = firstInt(payload, "value", "minutes", 5);
  } else if (commandNameEquals(name, "set_speaker_volume")) {
    command.type = DeviceCommandType::SpeakerVolume;
    command.numericValue = firstInt(payload, "value", "volume", 100);
  } else if (commandNameEquals(name, "set_language")) {
    command.type = DeviceCommandType::Language;
    command.value = firstString(payload, "value", "language");
  } else if (commandNameEquals(name, "set_theme")) {
    command.type = DeviceCommandType::Theme;
    command.value = firstString(payload, "value", "theme");
  } else if (commandNameEquals(name, "set_log_level")) {
    command.type = DeviceCommandType::LogLevel;
    command.value = firstString(payload, "value", "log_level");
  }
  return command;
}

}  // namespace DeviceCommandParser

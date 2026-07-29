#pragma once

#include <ArduinoJson.h>

#include <initializer_list>

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

inline bool commandNameEqualsAny(const char *rawName, std::initializer_list<const char *> expectedNames) {
  for (const char *expected : expectedNames) {
    if (commandNameEquals(rawName, expected)) {
      return true;
    }
  }
  return false;
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

inline bool firstBool(JsonVariantConst payload, const char *primary, const char *secondary, bool fallback) {
  if (payload[primary].is<bool>()) {
    return payload[primary].as<bool>();
  }
  if (secondary != nullptr && payload[secondary].is<bool>()) {
    return payload[secondary].as<bool>();
  }
  if (payload[primary].is<int>()) {
    return payload[primary].as<int>() != 0;
  }
  if (secondary != nullptr && payload[secondary].is<int>()) {
    return payload[secondary].as<int>() != 0;
  }
  const String raw = firstString(payload, primary, secondary);
  if (raw == "true" || raw == "on" || raw == "1") {
    return true;
  }
  if (raw == "false" || raw == "off" || raw == "0") {
    return false;
  }
  return fallback;
}

// Parses Home Assistant native command payloads posted to /api/device/command.
inline DeviceCommand parse(JsonVariantConst payload) {
  DeviceCommand command;
  const char *name = payload["command"] | "";
  command.value = firstString(payload, "command");
  if (commandNameEqualsAny(name, {"play", "resume", "media_play"})) {
    command.type = DeviceCommandType::Play;
  } else if (commandNameEqualsAny(name, {"pause", "media_pause"})) {
    command.type = DeviceCommandType::Pause;
  } else if (commandNameEqualsAny(name, {"play_pause", "toggle_play_pause", "media_play_pause"})) {
    command.type = DeviceCommandType::PlayPause;
  } else if (commandNameEqualsAny(name, {"next", "next_track", "media_next_track"})) {
    command.type = DeviceCommandType::Next;
  } else if (commandNameEqualsAny(name, {"previous", "previous_track", "media_previous_track"})) {
    command.type = DeviceCommandType::Previous;
  } else if (commandNameEqualsAny(name, {"status", "refresh_status"})) {
    command.type = DeviceCommandType::Status;
  } else if (commandNameEquals(name, "dj_response")) {
    command.type = DeviceCommandType::DjResponse;
    command.value = firstString(payload, "text");
    command.audioUrl = firstString(payload, "audio_url", "audioUrl");
  } else if (commandNameEqualsAny(name, {"set_volume", "volume"})) {
    command.type = DeviceCommandType::Volume;
    command.numericValue = firstInt(payload, "value", "volume", 0);
  } else if (commandNameEqualsAny(name, {"set_output", "transfer_output"})) {
    command.type = DeviceCommandType::TransferOutput;
    command.value = firstString(payload, "value", "output");
  } else if (commandNameEquals(name, "start_playlist")) {
    command.type = DeviceCommandType::StartPlaylist;
    command.value = firstString(payload, "value", "playlist", "uri");
  } else if (commandNameEqualsAny(name, {"set_shuffle", "shuffle"})) {
    command.type = DeviceCommandType::Shuffle;
    command.numericValue = firstBool(payload, "value", "shuffle", false) ? 1 : 0;
  } else if (commandNameEqualsAny(name, {"set_repeat", "repeat"})) {
    command.type = DeviceCommandType::Repeat;
    command.value = firstString(payload, "value", "repeat", "repeat_state");
  } else if (commandNameEqualsAny(name, {"screen_brightness", "set_brightness"})) {
    command.type = DeviceCommandType::ScreenBrightness;
    command.numericValue = firstInt(payload, "value", "brightness", 100);
  } else if (commandNameEqualsAny(name, {"screen_dim_timeout", "set_screen_timeout", "set_dim_timeout"})) {
    command.type = DeviceCommandType::ScreenDimTimeout;
    command.numericValue = firstInt(payload, "value", "seconds", 60);
  } else if (commandNameEqualsAny(name, {"turn_off_after", "set_turn_off_after"})) {
    command.type = DeviceCommandType::DeepSleepTimeout;
    command.numericValue = firstInt(payload, "value", "minutes", 5);
  } else if (commandNameEqualsAny(name, {"speaker_volume", "set_speaker_volume"})) {
    command.type = DeviceCommandType::SpeakerVolume;
    command.numericValue = firstInt(payload, "value", "volume", 100);
  } else if (commandNameEqualsAny(name, {"language", "set_language"})) {
    command.type = DeviceCommandType::Language;
    command.value = firstString(payload, "value", "language");
  } else if (commandNameEqualsAny(name, {"theme", "set_theme"})) {
    command.type = DeviceCommandType::Theme;
    command.value = firstString(payload, "value", "theme");
  } else if (commandNameEqualsAny(name, {"log_level", "set_log_level"})) {
    command.type = DeviceCommandType::LogLevel;
    command.value = firstString(payload, "value", "log_level");
  } else if (commandNameEqualsAny(name, {"wake_word", "set_wake_word", "wake_word_enabled", "set_wake_word_enabled"})) {
    command.type = DeviceCommandType::WakeWord;
    command.numericValue = firstBool(payload, "value", "enabled", false) ? 1 : 0;
  } else if (commandNameEqualsAny(name, {"stress_test", "set_stress_test", "monkey_test"})) {
    command.type = DeviceCommandType::StressTest;
    command.numericValue = firstBool(payload, "value", "enabled", true) ? 1 : 0;
  }
  return command;
}

}  // namespace DeviceCommandParser

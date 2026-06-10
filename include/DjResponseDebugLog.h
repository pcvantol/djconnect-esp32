// Compact debug logging helpers for Home Assistant DJ responses.
#pragma once

#include <Arduino.h>

#include "AppLog.h"

inline String djResponseDebugCompactText(const String &text) {
  String compact;
  compact.reserve(min(static_cast<size_t>(text.length()), static_cast<size_t>(96)));
  for (size_t index = 0; index < text.length() && compact.length() < 96; ++index) {
    const char value = text.charAt(index);
    if (value == '\r' || value == '\n' || value == '\t') {
      if (!compact.endsWith(" ")) {
        compact += ' ';
      }
    } else {
      compact += value;
    }
  }
  compact.trim();
  return compact;
}

inline void logDjResponseDebugText(const String &source, const String &text, const String &audioUrl) {
  AppLog.print("DJ response debug: source=");
  AppLog.print(source);
  AppLog.print(" text_chars=");
  AppLog.print(text.length());
  AppLog.print(" audio_url=");
  AppLog.print(audioUrl.isEmpty() ? "none" : "present");
  AppLog.print(" text=\"");
  AppLog.print(djResponseDebugCompactText(text));
  AppLog.println("\"");
}

inline void logDjResponseDebugAudio(const String &source, const String &audioType, int contentLength, bool played) {
  AppLog.print("DJ response debug: source=");
  AppLog.print(source);
  AppLog.print(" audio_type=");
  AppLog.print(audioType);
  AppLog.print(" audio_bytes=");
  AppLog.print(contentLength > 0 ? String(contentLength) : "unknown");
  AppLog.print(" played=");
  AppLog.println(played ? "true" : "false");
}

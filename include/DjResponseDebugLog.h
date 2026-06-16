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

inline void logDjResponseFullText(const String &source, const String &text) {
  static constexpr size_t ChunkSize = 96;
  if (text.isEmpty()) {
    AppLog.print("DJ response text: source=");
    AppLog.print(source);
    AppLog.println(" empty");
    return;
  }

  const size_t total = (text.length() + ChunkSize - 1) / ChunkSize;
  for (size_t chunk = 0; chunk < total; ++chunk) {
    const size_t start = chunk * ChunkSize;
    String part = text.substring(start, min(start + ChunkSize, text.length()));
    part.replace("\r", " ");
    part.replace("\n", " ");
    part.replace("\t", " ");
    part.trim();
    AppLog.print("DJ response text ");
    AppLog.print(chunk + 1);
    AppLog.print("/");
    AppLog.print(total);
    AppLog.print(" source=");
    AppLog.print(source);
    AppLog.print(": ");
    AppLog.println(part);
  }
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

// Small string utilities shared by Spotify API code and display formatting.
#pragma once

#include <Arduino.h>

// Percent-encodes URL query/body values.
String urlEncode(const String &value);

// Formats milliseconds as M:SS or H:MM:SS for the playback footer.
String formatTrackTime(int ms);

// Safely copies Arduino String values into fixed FreeRTOS queue buffers.
void copyToBuffer(char *target, size_t targetSize, const String &value);

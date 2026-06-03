// Shared string helpers.
#include "TextHelpers.h"

#include <ctype.h>

#include "LogicHelpers.h"

String urlEncode(const String &value) {
  static const char hex[] = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(value.length() * 3);

  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

String formatTrackTime(int ms) {
  // Spotify reports track positions in milliseconds; the UI needs compact clock text.
  char buffer[12];
  Logic::formatTrackTime(ms, buffer, sizeof(buffer));
  return String(buffer);
}

void copyToBuffer(char *target, size_t targetSize, const String &value) {
  // Queue payloads use fixed char arrays so FreeRTOS can copy them safely between tasks.
  if (targetSize == 0) {
    return;
  }
  strncpy(target, value.c_str(), targetSize - 1);
  target[targetSize - 1] = '\0';
}

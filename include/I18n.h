// Tiny runtime translation helper for user-facing UI strings.
#pragma once

#include <Arduino.h>

enum class Language : uint8_t {
  English,
  Dutch,
};

namespace I18n {
void setLanguage(Language language);
Language language();
String languageCode();
Language languageFromCode(const String &code);
const char *text(const char *key);
String onOff(bool value);
String connected(bool value);
}  // namespace I18n

// Tiny runtime translation helper for user-facing UI strings.
#pragma once

#include <Arduino.h>

#include "LogicHelpers.h"

enum class Language : uint8_t {
  English,
  Dutch,
  German,
  French,
  Spanish,
};

namespace I18n {
constexpr size_t SupportedLanguageCount = Logic::SupportedLanguageCount;
void setLanguage(Language language);
Language language();
String languageCode();
Language languageFromCode(const String &code);
bool isSupportedLanguageCode(const String &code);
const char *languageCode(Language language);
const char *languageLabelKey(Language language);
Language languageAt(size_t index);
const char *text(const char *key);
String onOff(bool value);
String connected(bool value);
}  // namespace I18n

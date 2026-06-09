#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "SpotifyDJMenuModel.h"

namespace SpotifyDJMenu {

constexpr size_t MenuStackCapacity = SpotifyDJMenuModel::MenuStackCapacity;
constexpr size_t DimTimeoutOptionCount = SpotifyDJMenuModel::DimTimeoutOptionCount;
constexpr size_t BrightnessOptionCount = SpotifyDJMenuModel::BrightnessOptionCount;
constexpr size_t LanguageOptionCount = SpotifyDJMenuModel::LanguageOptionCount;
constexpr size_t ThemeOptionCount = SpotifyDJMenuModel::ThemeOptionCount;
constexpr size_t LogLevelOptionCount = SpotifyDJMenuModel::LogLevelOptionCount;
constexpr size_t SpeakerVolumeOptionCount = SpotifyDJMenuModel::SpeakerVolumeOptionCount;
constexpr size_t ShuffleOptionCount = SpotifyDJMenuModel::ShuffleOptionCount;
constexpr size_t RepeatOptionCount = SpotifyDJMenuModel::RepeatOptionCount;
constexpr size_t SleepTimeoutOptionCount = SpotifyDJMenuModel::SleepTimeoutOptionCount;
constexpr size_t ConfirmOptionCount = SpotifyDJMenuModel::ConfirmOptionCount;
constexpr size_t HardResetOptionCount = SpotifyDJMenuModel::HardResetOptionCount;
constexpr size_t WifiFailureOptionCount = SpotifyDJMenuModel::WifiFailureOptionCount;
constexpr size_t SettingsItemCount = SpotifyDJMenuModel::SettingsItemCount;
constexpr size_t RootMenuItemCount = SpotifyDJMenuModel::RootMenuItemCount;
constexpr size_t GamesItemCount = SpotifyDJMenuModel::GamesItemCount;
constexpr size_t HelpItemCount = SpotifyDJMenuModel::HelpItemCount;
constexpr size_t AboutItemCount = SpotifyDJMenuModel::AboutItemCount;
constexpr size_t MaxVisibleOutputs = SpotifyDJMenuModel::MaxVisibleOutputs;

bool isMenuScreen(UiScreen screen);
size_t itemCount(UiScreen screen, const PlaylistListState &playlists, const DeviceListState &devices);

uint32_t dimTimeoutValueMs(size_t index);
uint8_t brightnessValuePercent(size_t index);
uint8_t speakerVolumeValuePercent(size_t index);
uint32_t sleepTimeoutValueMs(size_t index);

String languageLabel(Language language);
String themeValue(size_t index);
String themeLabel(const String &theme);
String logLevelValue(size_t index);
String logLevelLabel(const String &level);
bool shuffleValue(size_t index);
String shuffleLabel(bool enabled);
String repeatValue(size_t index);
String repeatLabel(const String &repeatState);

}  // namespace SpotifyDJMenu

#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "DJConnectMenuModel.h"

namespace DJConnectMenu {

constexpr size_t MenuStackCapacity = DJConnectMenuModel::MenuStackCapacity;
constexpr size_t DimTimeoutOptionCount = DJConnectMenuModel::DimTimeoutOptionCount;
constexpr size_t BrightnessOptionCount = DJConnectMenuModel::BrightnessOptionCount;
constexpr size_t LanguageOptionCount = DJConnectMenuModel::LanguageOptionCount;
constexpr size_t ThemeOptionCount = DJConnectMenuModel::ThemeOptionCount;
constexpr size_t LogLevelOptionCount = DJConnectMenuModel::LogLevelOptionCount;
constexpr size_t SpeakerVolumeOptionCount = DJConnectMenuModel::SpeakerVolumeOptionCount;
constexpr size_t ShuffleOptionCount = DJConnectMenuModel::ShuffleOptionCount;
constexpr size_t RepeatOptionCount = DJConnectMenuModel::RepeatOptionCount;
constexpr size_t SleepTimeoutOptionCount = DJConnectMenuModel::SleepTimeoutOptionCount;
constexpr size_t ConfirmOptionCount = DJConnectMenuModel::ConfirmOptionCount;
constexpr size_t HardResetOptionCount = DJConnectMenuModel::HardResetOptionCount;
constexpr size_t WifiFailureOptionCount = DJConnectMenuModel::WifiFailureOptionCount;
constexpr size_t SettingsItemCount = DJConnectMenuModel::SettingsItemCount;
constexpr size_t RootMenuItemCount = DJConnectMenuModel::RootMenuItemCount;
constexpr size_t GamesItemCount = DJConnectMenuModel::GamesItemCount;
constexpr size_t HelpItemCount = DJConnectMenuModel::HelpItemCount;
constexpr size_t AboutItemCount = DJConnectMenuModel::AboutItemCount;
constexpr size_t MaxVisibleOutputs = DJConnectMenuModel::MaxVisibleOutputs;

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

}  // namespace DJConnectMenu

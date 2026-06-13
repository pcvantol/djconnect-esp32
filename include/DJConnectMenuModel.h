#pragma once

#include <stddef.h>
#include <stdint.h>

enum class UiScreen {
  NowPlaying,
  AlbumArt,
  Queue,
  Playlists,
  SoundOutputs,
  Logs,
  Games,
  Help,
  Pong,
  Asteroids,
  Flyer,
  MazeChase,
  RootMenu,
  About,
  Settings,
  DimTimeout,
  Brightness,
  Language,
  Theme,
  LogLevel,
  SpeakerVolume,
  ShuffleMode,
  RepeatMode,
  SleepTimeout,
  ChangeWifiConfirm,
  ResetPairingConfirm,
  HardResetConfirm,
};

struct MenuCountInput {
  bool playlistsAvailable = false;
  size_t playlistCount = 0;
  bool devicesAvailable = false;
  size_t deviceCount = 0;
};

namespace DJConnectMenuModel {

constexpr size_t MenuStackCapacity = 5;
constexpr size_t DimTimeoutOptionCount = 4;
constexpr size_t BrightnessOptionCount = 4;
constexpr size_t LanguageOptionCount = 2;
constexpr size_t ThemeOptionCount = 3;
constexpr size_t LogLevelOptionCount = 4;
constexpr size_t SpeakerVolumeOptionCount = 4;
constexpr size_t ShuffleOptionCount = 2;
constexpr size_t RepeatOptionCount = 3;
constexpr size_t SleepTimeoutOptionCount = 4;
constexpr size_t ConfirmOptionCount = 2;
constexpr size_t HardResetOptionCount = ConfirmOptionCount;
constexpr size_t WifiFailureOptionCount = 4;
constexpr size_t SettingsItemCount = 14;
constexpr size_t RootMenuItemCount = 11;
constexpr size_t GamesItemCount = 4;
constexpr size_t HelpItemCount = 8;
constexpr size_t AboutItemCount = 9;
constexpr size_t FixedSoundOutputCount = 2;
constexpr size_t MaxVisibleOutputs = 6;

inline bool isMenuScreen(UiScreen screen) {
  return screen != UiScreen::NowPlaying;
}

inline size_t itemCount(UiScreen screen, const MenuCountInput &input) {
  switch (screen) {
    case UiScreen::AlbumArt:
    case UiScreen::Queue:
    case UiScreen::Logs:
    case UiScreen::Pong:
    case UiScreen::Asteroids:
    case UiScreen::Flyer:
    case UiScreen::MazeChase:
    case UiScreen::NowPlaying:
      return 0;
    case UiScreen::Games:
      return GamesItemCount;
    case UiScreen::Help:
      return HelpItemCount;
    case UiScreen::Playlists:
      return input.playlistsAvailable && input.playlistCount > 0 ? input.playlistCount : 1;
    case UiScreen::SoundOutputs:
      return FixedSoundOutputCount + (input.devicesAvailable && input.deviceCount > 0 ? (input.deviceCount < MaxVisibleOutputs ? input.deviceCount : MaxVisibleOutputs) : 0);
    case UiScreen::RootMenu:
      return RootMenuItemCount;
    case UiScreen::Settings:
      return SettingsItemCount;
    case UiScreen::DimTimeout:
      return DimTimeoutOptionCount;
    case UiScreen::Brightness:
      return BrightnessOptionCount;
    case UiScreen::Language:
      return LanguageOptionCount;
    case UiScreen::Theme:
      return ThemeOptionCount;
    case UiScreen::LogLevel:
      return LogLevelOptionCount;
    case UiScreen::SpeakerVolume:
      return SpeakerVolumeOptionCount;
    case UiScreen::ShuffleMode:
      return ShuffleOptionCount;
    case UiScreen::RepeatMode:
      return RepeatOptionCount;
    case UiScreen::SleepTimeout:
      return SleepTimeoutOptionCount;
    case UiScreen::HardResetConfirm:
    case UiScreen::ChangeWifiConfirm:
    case UiScreen::ResetPairingConfirm:
      return ConfirmOptionCount;
    case UiScreen::About:
      return AboutItemCount;
  }
  return 0;
}

inline uint32_t dimTimeoutValueMs(size_t index) {
  static const uint32_t values[DimTimeoutOptionCount] = {30000, 60000, 120000, 240000};
  return values[index < DimTimeoutOptionCount ? index : 1];
}

inline uint8_t brightnessValuePercent(size_t index) {
  static const uint8_t values[BrightnessOptionCount] = {25, 50, 75, 100};
  return values[index < BrightnessOptionCount ? index : 3];
}

inline uint8_t speakerVolumeValuePercent(size_t index) {
  static const uint8_t values[SpeakerVolumeOptionCount] = {25, 50, 75, 100};
  return values[index < SpeakerVolumeOptionCount ? index : 3];
}

inline const char *themeValue(size_t index) {
  static const char *const values[ThemeOptionCount] = {"dark", "light", "auto"};
  return values[index < ThemeOptionCount ? index : 0];
}

inline const char *logLevelValue(size_t index) {
  static const char *const values[LogLevelOptionCount] = {"debug", "info", "warning", "error"};
  return values[index < LogLevelOptionCount ? index : 1];
}

inline bool shuffleValue(size_t index) {
  return index == 1;
}

inline const char *repeatValue(size_t index) {
  static const char *const values[RepeatOptionCount] = {
      "off",
      "track",
      "context",
  };
  return values[index < RepeatOptionCount ? index : 0];
}

}  // namespace DJConnectMenuModel

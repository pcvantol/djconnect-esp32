#include "SpotifyDJMenu.h"

namespace SpotifyDJMenu {

bool isMenuScreen(UiScreen screen) {
  return SpotifyDJMenuModel::isMenuScreen(screen);
}

size_t itemCount(UiScreen screen, const PlaylistListState &playlists, const DeviceListState &devices) {
  MenuCountInput input;
  input.playlistsAvailable = playlists.available;
  input.playlistCount = playlists.count;
  input.devicesAvailable = devices.available;
  input.deviceCount = devices.count;
  return SpotifyDJMenuModel::itemCount(screen, input);
}

uint32_t dimTimeoutValueMs(size_t index) {
  return SpotifyDJMenuModel::dimTimeoutValueMs(index);
}

uint8_t brightnessValuePercent(size_t index) {
  return SpotifyDJMenuModel::brightnessValuePercent(index);
}

uint8_t speakerVolumeValuePercent(size_t index) {
  return SpotifyDJMenuModel::speakerVolumeValuePercent(index);
}

uint32_t sleepTimeoutValueMs(size_t index) {
  return Logic::deepSleepTimeoutMsForIndex(index);
}

String languageLabel(Language language) {
  return I18n::text(language == Language::Dutch ? "language_dutch" : "language_english");
}

String themeValue(size_t index) {
  return SpotifyDJMenuModel::themeValue(index);
}

String themeLabel(const String &theme) {
  if (theme == "dark") {
    return I18n::text("theme_dark");
  }
  if (theme == "light") {
    return I18n::text("theme_light");
  }
  return I18n::text("theme_auto");
}

String logLevelValue(size_t index) {
  return SpotifyDJMenuModel::logLevelValue(index);
}

String logLevelLabel(const String &level) {
  if (level == "debug") {
    return I18n::text("log_level_debug");
  }
  if (level == "warning") {
    return I18n::text("log_level_warning");
  }
  if (level == "error") {
    return I18n::text("log_level_error");
  }
  return I18n::text("log_level_info");
}

bool shuffleValue(size_t index) {
  return SpotifyDJMenuModel::shuffleValue(index);
}

String shuffleLabel(bool enabled) {
  return I18n::text(enabled ? "shuffle_on" : "shuffle_off");
}

String repeatValue(size_t index) {
  return SpotifyDJMenuModel::repeatValue(index);
}

String repeatLabel(const String &repeatState) {
  if (repeatState == "track" || repeatState == "repeat_once") {
    return I18n::text("repeat_once");
  }
  if (repeatState == "context" || repeatState == "repeat_infinite") {
    return I18n::text("repeat_infinite");
  }
  return I18n::text("repeat_off");
}

}  // namespace SpotifyDJMenu

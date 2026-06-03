// Downloads and caches the current Spotify album art in LittleFS.
#pragma once

#include <Arduino.h>

#include "AppState.h"

class AlbumArtManager {
public:
  void begin();
  void requestCurrentSongArt(const SpotifyState &playback);
  void cleanupIfDue();

  bool hasImage() const;
  String imagePath() const;
  String status() const;

private:
  String cachePathForUrl(const String &url) const;
  String timestampPathForImagePath(const String &imagePath) const;
  bool isCacheFresh(const String &imagePath) const;
  bool downloadImage(const String &url, const String &imagePath);
  void writeTimestamp(const String &imagePath);
  uint32_t nowSeconds() const;
  uint32_t hashUrl(const String &url) const;

  bool ready_ = false;
  String currentUrl_;
  String currentPath_;
  String status_ = "No album art";
  uint32_t lastCleanupAt_ = 0;
};

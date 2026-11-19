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
  String urlOriginForLog(const String &url) const;
  void writeTimestamp(const String &imagePath);
  uint32_t nowSeconds() const;
  uint32_t hashUrl(const String &url) const;

  bool ready_ = false;
  bool downloadInProgress_ = false;
  String currentUrl_;
  String currentPath_;
  String lastFailedUrl_;
  String lastAttemptUrl_;
  String status_ = "No album art";
  uint32_t lastCleanupAt_ = 0;
  uint32_t lastFailureAt_ = 0;
  uint32_t lastAttemptAt_ = 0;
};

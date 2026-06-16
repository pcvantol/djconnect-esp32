// LittleFS-backed Spotify album art cache.
#include "AlbumArtManager.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_heap_caps.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "AppLog.h"
#include "Config.h"
#include "NetworkActivity.h"

static constexpr uint32_t CacheTtlSeconds = 24UL * 60UL * 60UL;
static constexpr uint32_t CleanupIntervalMs = 30UL * 60UL * 1000UL;
static constexpr uint32_t FailureRetryBackoffMs = 30000UL;
static constexpr uint32_t AttemptCooldownMs = 15000UL;
static constexpr uint32_t AlbumConnectTimeoutMs = 2500UL;
static constexpr uint32_t AlbumReadTimeoutMs = 4500UL;
static constexpr size_t MaxImageBytes = 220 * 1024;
static const char *const AlbumCachePrefix = "/album2_";

void AlbumArtManager::begin() {
  ready_ = LittleFS.begin(true);
  status_ = ready_ ? "Album cache ready" : "Album cache failed";
}

void AlbumArtManager::requestCurrentSongArt(const SpotifyState &playback) {
  if (!ready_) {
    status_ = "Album cache unavailable";
    return;
  }
  if (downloadInProgress_) {
    status_ = "Album art download busy";
    return;
  }
  if (playback.albumImageUrl.isEmpty()) {
    currentUrl_ = "";
    currentPath_ = "";
    status_ = "No album art URL";
    AppLog.println("Album art: no URL in playback state");
    return;
  }

  currentUrl_ = playback.albumImageUrl;
  currentPath_ = cachePathForUrl(currentUrl_);
  const uint32_t now = millis();
  if (lastAttemptUrl_ == currentUrl_ && now - lastAttemptAt_ < AttemptCooldownMs) {
    status_ = "Album art attempt cooling down";
    AppLog.println("Album art: attempt cooling down");
    if (!isCacheFresh(currentPath_)) {
      currentPath_ = "";
    }
    return;
  }
  if (lastFailedUrl_ == currentUrl_ && millis() - lastFailureAt_ < FailureRetryBackoffMs) {
    status_ = "Album art retry cooling down";
    AppLog.println("Album art: retry cooling down");
    currentPath_ = "";
    return;
  }
  if (isCacheFresh(currentPath_)) {
    status_ = "Album art cached";
    AppLog.print("Album art: cached ");
    AppLog.println(currentPath_);
    return;
  }

  status_ = "Downloading album art";
  AppLog.print("Album art: downloading, free=");
  AppLog.print(heap_caps_get_free_size(MALLOC_CAP_8BIT));
  AppLog.print(" largest=");
  AppLog.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  AppLog.print("Album art URL origin: ");
  AppLog.println(urlOriginForLog(currentUrl_));
  lastAttemptUrl_ = currentUrl_;
  lastAttemptAt_ = now;
  downloadInProgress_ = true;
  if (downloadImage(currentUrl_, currentPath_)) {
    downloadInProgress_ = false;
    writeTimestamp(currentPath_);
    status_ = "Album art ready";
    AppLog.print("Album art: ready ");
    AppLog.println(currentPath_);
  } else {
    downloadInProgress_ = false;
    AppLog.print("Album art: failed ");
    AppLog.println(status_);
    lastFailedUrl_ = currentUrl_;
    lastFailureAt_ = millis();
    currentPath_ = "";
  }
}

void AlbumArtManager::cleanupIfDue() {
  if (!ready_ || millis() - lastCleanupAt_ < CleanupIntervalMs) {
    return;
  }
  lastCleanupAt_ = millis();

  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    const String name = file.name();
    file.close();
    if ((name.startsWith("/album_") || name.startsWith(AlbumCachePrefix)) && name.endsWith(".jpg") && !isCacheFresh(name)) {
      LittleFS.remove(name);
      LittleFS.remove(timestampPathForImagePath(name));
    }
    file = root.openNextFile();
  }
}

bool AlbumArtManager::hasImage() const {
  return ready_ && !currentPath_.isEmpty() && LittleFS.exists(currentPath_);
}

String AlbumArtManager::imagePath() const {
  return currentPath_;
}

String AlbumArtManager::status() const {
  return status_;
}

String AlbumArtManager::cachePathForUrl(const String &url) const {
  return String(AlbumCachePrefix) + String(hashUrl(url), HEX) + ".jpg";
}

String AlbumArtManager::timestampPathForImagePath(const String &imagePath) const {
  String timestampPath = imagePath;
  timestampPath.replace(".jpg", ".ts");
  return timestampPath;
}

bool AlbumArtManager::isCacheFresh(const String &imagePath) const {
  if (!LittleFS.exists(imagePath)) {
    return false;
  }

  File timestamp = LittleFS.open(timestampPathForImagePath(imagePath), "r");
  if (!timestamp) {
    return false;
  }

  const uint32_t savedAt = timestamp.readString().toInt();
  timestamp.close();
  const uint32_t now = nowSeconds();
  return savedAt > 0 && now >= savedAt && now - savedAt <= CacheTtlSeconds;
}

bool AlbumArtManager::downloadImage(const String &url, const String &imagePath) {
  if (WiFi.status() != WL_CONNECTED) {
    status_ = "WiFi disconnected";
    return false;
  }

  const bool isHttps = url.startsWith("https://");
  const bool isHttp = url.startsWith("http://");
  if (!isHttps && !isHttp) {
    status_ = "Album URL unsupported";
    return false;
  }

  WiFiClientSecure secureClient;
  if (isHttps) {
    secureClient.setInsecure();
    secureClient.setTimeout(AlbumReadTimeoutMs);
  }

  HTTPClient http;
  NetworkActivity activity("album_art_download", AlbumReadTimeoutMs);
  http.setConnectTimeout(AlbumConnectTimeoutMs);
  http.setTimeout(AlbumReadTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const bool begun = isHttps ? http.begin(secureClient, url) : http.begin(url);
  if (!begun) {
    status_ = "Album HTTP begin failed";
    activity.finishError("begin failed");
    return false;
  }

  const int code = http.GET();
  if (code != 200) {
    status_ = "Album HTTP " + String(code);
    if (code < 0) {
      AppLog.print("Album art transport: ");
      AppLog.println(HTTPClient::errorToString(code));
    }
    http.end();
    activity.finish(code);
    return false;
  }

  const int expectedBytes = http.getSize();

  File file = LittleFS.open(imagePath, "w");
  if (!file) {
    status_ = "Album cache write failed";
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[512];
  size_t totalBytes = 0;
  uint32_t lastDataAt = millis();
  bool timedOut = false;
  while (http.connected()) {
    const size_t available = stream->available();
    if (available == 0) {
      if (expectedBytes > 0 && totalBytes >= static_cast<size_t>(expectedBytes)) {
        break;
      }
      if (!stream->connected()) {
        break;
      }
      if (millis() - lastDataAt > 5000) {
        status_ = "Album download timeout";
        timedOut = true;
        break;
      }
      delay(10);
      continue;
    }

    const int readBytes = stream->readBytes(buffer, min(sizeof(buffer), available));
    if (readBytes <= 0) {
      break;
    }
    totalBytes += readBytes;
    lastDataAt = millis();
    if (totalBytes > MaxImageBytes) {
      status_ = "Album art too large";
      file.close();
      http.end();
      activity.finishError("too large");
      LittleFS.remove(imagePath);
      return false;
    }
    file.write(buffer, readBytes);
    delay(1);
  }

  file.close();
  http.end();
  if (totalBytes == 0) {
    status_ = "Album download empty";
    activity.finishError("empty");
    LittleFS.remove(imagePath);
    return false;
  }
  if (timedOut) {
    activity.finishError("timeout");
    LittleFS.remove(imagePath);
    return false;
  }
  if (expectedBytes > 0 && totalBytes < static_cast<size_t>(expectedBytes)) {
    status_ = "Album download partial";
    activity.finishError("partial");
    LittleFS.remove(imagePath);
    return false;
  }
  activity.finish(code, "cached");
  return true;
}

String AlbumArtManager::urlOriginForLog(const String &url) const {
  const int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) {
    return "unsupported";
  }
  const int hostStart = schemeEnd + 3;
  int hostEnd = url.indexOf('/', hostStart);
  if (hostEnd < 0) {
    hostEnd = url.length();
  }
  String origin = url.substring(0, hostEnd);
  const int queryStart = origin.indexOf('?');
  if (queryStart >= 0) {
    origin = origin.substring(0, queryStart);
  }
  return origin;
}

void AlbumArtManager::writeTimestamp(const String &imagePath) {
  File timestamp = LittleFS.open(timestampPathForImagePath(imagePath), "w");
  if (timestamp) {
    timestamp.print(nowSeconds());
    timestamp.close();
  }
}

uint32_t AlbumArtManager::nowSeconds() const {
  const time_t now = time(nullptr);
  return now > 1704067200 ? static_cast<uint32_t>(now) : millis() / 1000UL;
}

uint32_t AlbumArtManager::hashUrl(const String &url) const {
  uint32_t hash = 2166136261UL;
  for (size_t index = 0; index < url.length(); index++) {
    hash ^= static_cast<uint8_t>(url[index]);
    hash *= 16777619UL;
  }
  return hash;
}

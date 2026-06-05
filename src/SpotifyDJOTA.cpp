// Firmware OTA download/install helper for Home Assistant-triggered updates.
#include "SpotifyDJOTA.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "AppLog.h"
#include "Config.h"
#include "I18n.h"
#include "NetworkActivity.h"

namespace {
const char *ExpectedModel = "lilygo-t-embed-s3";
}

bool SpotifyDJOTA::canUpdate(const BatteryState *battery, String &message) const {
  if (battery == nullptr || !battery->available || battery->percent < 0) {
    message = "Battery state unknown, allowing OTA";
    return true;
  }
  if (battery->percent > 40 || battery->charging || battery->full) {
    return true;
  }
  message = "Battery too low for OTA";
  return false;
}

bool SpotifyDJOTA::performUpdate(
    const SpotifyDJOTARequest &request,
    const BatteryState *battery,
    DisplayManager *display,
    LedRing *ledRing,
    String &message) {
  if (request.device != ExpectedModel) {
    message = "Wrong device target";
    return false;
  }
  if (request.url.isEmpty()) {
    message = "OTA URL missing";
    return false;
  }
  if (!canUpdate(battery, message)) {
    return false;
  }
  if (!request.sha256.isEmpty()) {
    AppLog.println("[SpotifyDJ] OTA SHA256 provided; TODO verify while streaming before reboot");
  }

  AppLog.print("[SpotifyDJ] OTA target version: ");
  AppLog.println(request.version);

  HTTPClient http;
  NetworkActivity activity("ota_download", Config::OtaIoTimeoutMs);
  NetworkActivity::configureHttp(http, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  const bool secure = request.url.startsWith("https://");
  if (secure) {
    // TODO: pin the release certificate or verify SHA256 before accepting production OTA binaries.
    secureClient.setInsecure();
  }

  const bool begun = secure ? http.begin(secureClient, request.url) : http.begin(plainClient, request.url);
  if (!begun) {
    message = "OTA HTTP begin failed";
    activity.finishError("begin failed");
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    message = "OTA download failed " + String(code);
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(message);
    http.end();
    activity.finish(code);
    return false;
  }

  const int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    message = "OTA Update.begin failed";
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(Update.errorString());
    http.end();
    activity.finishError("Update.begin failed");
    return false;
  }

  if (display != nullptr) {
    display->forceBacklightPercent(100);
    if (battery != nullptr) {
      display->showBootMessage(I18n::text("firmware_update_progress"), *battery);
    } else {
      display->showBootMessage(I18n::text("firmware_update_progress"));
    }
  }
  if (ledRing != nullptr) {
    ledRing->showSolid(CRGB::Purple, 100);
  }

  uint8_t buffer[1024];
  Stream *stream = http.getStreamPtr();
  size_t written = 0;
  size_t lastLogged = 0;
  uint32_t lastProgressAt = millis();
  while (stream != nullptr && (contentLength <= 0 || written < static_cast<size_t>(contentLength))) {
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected() && contentLength <= 0) {
        break;
      }
      if (millis() - lastProgressAt > Config::OtaStreamIdleTimeoutMs) {
        message = "OTA stream timeout";
        AppLog.println("[SpotifyDJ] OTA stream timeout");
        Update.abort();
        http.end();
        activity.finishError("stream timeout");
        return false;
      }
      delay(1);
      yield();
      continue;
    }

    size_t toRead = min(static_cast<size_t>(available), sizeof(buffer));
    if (contentLength > 0) {
      toRead = min(toRead, static_cast<size_t>(contentLength) - written);
    }
    const size_t read = stream->readBytes(buffer, toRead);
    if (read == 0) {
      continue;
    }
    const size_t chunkWritten = Update.write(buffer, read);
    if (chunkWritten != read) {
      message = "OTA write failed";
      AppLog.println("[SpotifyDJ] OTA write failed");
      Update.abort();
      http.end();
      activity.finishError("write failed");
      return false;
    }

    written += chunkWritten;
    lastProgressAt = millis();
    if (written - lastLogged >= 65536 || (contentLength > 0 && written == static_cast<size_t>(contentLength))) {
      AppLog.print("[SpotifyDJ] OTA written bytes=");
      AppLog.println(written);
      lastLogged = written;
    }
    delay(1);
    yield();
  }

  if (contentLength > 0 && written != static_cast<size_t>(contentLength)) {
    message = "OTA short write";
    AppLog.println("[SpotifyDJ] OTA short write");
    Update.abort();
    http.end();
    activity.finishError("short write");
    return false;
  }

  if (!Update.end(true)) {
    message = "OTA finalize failed";
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(Update.errorString());
    http.end();
    activity.finishError("finalize failed");
    return false;
  }

  http.end();
  message = "OTA started";
  AppLog.println("[SpotifyDJ] OTA update written successfully");
  activity.finish(code, "written");
  return true;
}

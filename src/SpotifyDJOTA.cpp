// Firmware OTA download/install helper for Home Assistant-triggered updates.
#include "SpotifyDJOTA.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "AppLog.h"

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
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    message = "OTA download failed " + String(code);
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(message);
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    message = "OTA Update.begin failed";
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(Update.errorString());
    http.end();
    return false;
  }

  if (display != nullptr) {
    display->forceBacklightPercent(100);
    if (battery != nullptr) {
      display->showBootMessage("Firmware update\nin progress..", *battery);
    } else {
      display->showBootMessage("Firmware update\nin progress..");
    }
  }
  if (ledRing != nullptr) {
    ledRing->showSolid(CRGB::Purple, 100);
  }

  const size_t written = Update.writeStream(http.getStream());
  if (contentLength > 0 && written != static_cast<size_t>(contentLength)) {
    message = "OTA short write";
    AppLog.println("[SpotifyDJ] OTA short write");
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end(true)) {
    message = "OTA finalize failed";
    AppLog.print("[SpotifyDJ] ");
    AppLog.println(Update.errorString());
    http.end();
    return false;
  }

  http.end();
  message = "OTA started";
  AppLog.println("[SpotifyDJ] OTA update written successfully");
  return true;
}

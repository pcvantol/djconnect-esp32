// Firmware OTA download/install helper for Home Assistant-triggered updates.
#include "DJConnectOTA.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <memory>

#include "AppLog.h"
#include "Config.h"
#include "GitHubTls.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "ScopedWatchdogPause.h"

namespace {
constexpr size_t OtaDownloadBufferBytes = 1460;
constexpr uint8_t OtaMaxRedirects = 5;
constexpr const char *OtaSpoolPath = "/ota_update.bin";

String sha256Hex(const unsigned char digest[32]) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(64);
  for (size_t index = 0; index < 32; index++) {
    out += hex[(digest[index] >> 4) & 0x0F];
    out += hex[digest[index] & 0x0F];
  }
  return out;
}

bool equalsIgnoreCase(const String &left, const String &right) {
  if (left.length() != right.length()) {
    return false;
  }
  for (size_t index = 0; index < left.length(); index++) {
    if (Logic::asciiLower(left[index]) != Logic::asciiLower(right[index])) {
      return false;
    }
  }
  return true;
}

void serviceOtaLoop(LedRing *ledRing) {
  ScopedWatchdogPause::resetIfAttached();
  if (ledRing != nullptr) {
    ledRing->showFirmwareUpdateAnimation();
  }
  delay(1);
  yield();
}

bool isHttpRedirect(int code) {
  return code == HTTP_CODE_MOVED_PERMANENTLY ||
         code == HTTP_CODE_FOUND ||
         code == HTTP_CODE_SEE_OTHER ||
         code == HTTP_CODE_TEMPORARY_REDIRECT ||
         code == HTTP_CODE_PERMANENT_REDIRECT;
}

String urlHost(const String &url) {
  const int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) {
    return "";
  }
  const int hostStart = schemeEnd + 3;
  int hostEnd = url.indexOf('/', hostStart);
  if (hostEnd < 0) {
    hostEnd = url.length();
  }
  return url.substring(hostStart, hostEnd);
}

String stripPort(const String &host) {
  const int portStart = host.indexOf(':');
  if (portStart < 0) {
    return host;
  }
  return host.substring(0, portStart);
}

void logHostResolution(const String &host) {
  const String hostname = stripPort(host);
  if (hostname.isEmpty()) {
    return;
  }
  IPAddress address;
  if (WiFi.hostByName(hostname.c_str(), address)) {
    AppLog.print("OTA download IP: ");
    AppLog.println(address.toString());
  } else {
    AppLog.print("OTA download DNS failed: ");
    AppLog.println(hostname);
  }
}

void logTlsError(WiFiClientSecure &client) {
  char errorBuffer[128] = {};
  const int errorCode = client.lastError(errorBuffer, sizeof(errorBuffer));
  if (errorCode == 0 && errorBuffer[0] == '\0') {
    return;
  }
  AppLog.print("OTA download TLS error: ");
  AppLog.print(errorCode);
  if (errorBuffer[0] != '\0') {
    AppLog.print(" ");
    AppLog.print(errorBuffer);
  }
  AppLog.println();
}

const char *caForDownloadHost(const String &host) {
  const String hostname = stripPort(host);
  if (hostname == "release-assets.githubusercontent.com") {
    return GitHubReleaseAssetsCa;
  }
  return GitHubApiCa;
}

}

bool DJConnectOTA::canUpdate(const BatteryState *battery, String &message) const {
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

bool DJConnectOTA::performUpdate(
    const DJConnectOTARequest &request,
    const BatteryState *battery,
    DisplayManager *display,
    LedRing *ledRing,
    SoundManager *sound,
    String &message) {
  if (request.device != Config::DeviceModel) {
    message = "Wrong device target";
    return false;
  }
  if (request.url.isEmpty()) {
    message = "OTA URL missing";
    return false;
  }
  if (!request.url.startsWith("https://")) {
    message = "OTA HTTPS URL required";
    return false;
  }
  if (!Logic::isSha256Hex(request.sha256.c_str())) {
    message = "OTA SHA256 missing or invalid";
    return false;
  }
  if (!canUpdate(battery, message)) {
    return false;
  }

  AppLog.print("OTA target version: ");
  AppLog.println(request.version);
  AppLog.print("OTA download URL: ");
  AppLog.println(request.url);
  if (sound != nullptr) {
    sound->playOtaStart();
  }
  if (display != nullptr) {
    display->forceBacklightPercent(100);
    const String displayMessage = String(I18n::text("firmware_update_progress")) +
                                  "\n" + request.version;
    if (battery != nullptr) {
      display->showBootMessage(displayMessage, *battery);
    } else {
      display->showBootMessage(displayMessage);
    }
  }
  if (ledRing != nullptr) {
    ledRing->showFirmwareUpdateAnimation();
  }
  auto failWithCue = [&]() {
    if (sound != nullptr) {
      sound->playOtaFailed();
      delay(220);
    }
  };

  NetworkActivity activity("ota_download", Config::OtaIoTimeoutMs);
  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setHandshakeTimeout(Config::TlsHandshakeTimeoutMs);
  secureClient.setTimeout(Config::OtaIoTimeoutMs);

  String downloadUrl = request.url;
  int code = 0;
  uint8_t redirects = 0;
  while (true) {
    NetworkActivity::configureHttp(http, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const String host = urlHost(downloadUrl);
    AppLog.print("OTA download host: ");
    AppLog.println(host);
    logHostResolution(host);
    secureClient.setCACert(caForDownloadHost(host));
    const bool begun = http.begin(secureClient, downloadUrl);
    if (!begun) {
      message = "OTA HTTP begin failed";
      activity.finishError("begin failed");
      failWithCue();
      return false;
    }
    http.addHeader("User-Agent", "DJConnect");
    http.addHeader("Accept", "application/octet-stream");

    code = http.GET();
    if (!isHttpRedirect(code)) {
      break;
    }
    const String location = http.getLocation();
    AppLog.print("OTA redirect ");
    AppLog.print(code);
    AppLog.print(": ");
    AppLog.println(location);
    http.end();
    secureClient.stop();
    if (location.isEmpty() || !location.startsWith("https://")) {
      message = "OTA redirect invalid";
      activity.finishError("redirect invalid");
      failWithCue();
      return false;
    }
    redirects++;
    if (redirects > OtaMaxRedirects) {
      message = "OTA redirect limit exceeded";
      activity.finishError("redirect limit");
      failWithCue();
      return false;
    }
    downloadUrl = location;
    serviceOtaLoop(ledRing);
  }

  if (code != HTTP_CODE_OK) {
    message = "OTA download failed " + String(code);
    AppLog.println(message);
    AppLog.print("OTA final URL: ");
    AppLog.println(downloadUrl);
    if (code < 0) {
      AppLog.print("OTA download transport: ");
      AppLog.println(http.errorToString(code));
      logTlsError(secureClient);
    }
    http.end();
    activity.finish(code);
    failWithCue();
    return false;
  }

  std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[OtaDownloadBufferBytes]);
  if (!buffer) {
    message = "OTA buffer allocation failed";
    AppLog.println(message);
    http.end();
    activity.finishError("buffer alloc failed");
    failWithCue();
    return false;
  }

  const int contentLength = http.getSize();
  AppLog.print("OTA content length=");
  AppLog.println(contentLength);
  if (!LittleFS.begin(true)) {
    message = "OTA LittleFS unavailable";
    AppLog.println(message);
    http.end();
    activity.finishError("littlefs unavailable");
    failWithCue();
    return false;
  }
  LittleFS.remove(OtaSpoolPath);
  fs::File spoolFile = LittleFS.open(OtaSpoolPath, "w");
  if (!spoolFile) {
    message = "OTA spool open failed";
    AppLog.println(message);
    http.end();
    activity.finishError("spool open failed");
    failWithCue();
    return false;
  }

  Stream *stream = http.getStreamPtr();
  size_t downloaded = 0;
  size_t lastLogged = 0;
  size_t lastProgressCue = 0;
  uint32_t lastProgressAt = millis();
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  if (mbedtls_sha256_starts(&shaContext, 0) != 0) {
    message = "OTA SHA256 init failed";
    spoolFile.close();
    LittleFS.remove(OtaSpoolPath);
    http.end();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("sha init failed");
    failWithCue();
    return false;
  }
  while (stream != nullptr && (contentLength <= 0 || downloaded < static_cast<size_t>(contentLength))) {
    serviceOtaLoop(ledRing);
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected() && contentLength <= 0) {
        break;
      }
      if (millis() - lastProgressAt > Config::OtaStreamIdleTimeoutMs) {
        message = "OTA stream timeout";
        AppLog.println("OTA stream timeout");
        spoolFile.close();
        LittleFS.remove(OtaSpoolPath);
        http.end();
        mbedtls_sha256_free(&shaContext);
        activity.finishError("stream timeout");
        failWithCue();
        return false;
      }
      delay(10);
      continue;
    }

    size_t toRead = min(static_cast<size_t>(available), OtaDownloadBufferBytes);
    if (contentLength > 0) {
      toRead = min(toRead, static_cast<size_t>(contentLength) - downloaded);
    }
    const size_t read = stream->readBytes(buffer.get(), toRead);
    if (read == 0) {
      continue;
    }
    serviceOtaLoop(ledRing);
    if (mbedtls_sha256_update(&shaContext, buffer.get(), read) != 0) {
      message = "OTA SHA256 update failed";
      AppLog.println("OTA SHA256 update failed");
      spoolFile.close();
      LittleFS.remove(OtaSpoolPath);
      http.end();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("sha update failed");
      failWithCue();
      return false;
    }
    serviceOtaLoop(ledRing);
    const size_t chunkSpooled = spoolFile.write(buffer.get(), read);
    if (chunkSpooled != read) {
      message = "OTA spool write failed";
      AppLog.println(message);
      spoolFile.close();
      LittleFS.remove(OtaSpoolPath);
      http.end();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("spool write failed");
      failWithCue();
      return false;
    }

    downloaded += chunkSpooled;
    lastProgressAt = millis();
    if (sound != nullptr && downloaded - lastProgressCue >= 196608) {
      sound->playOtaProgress();
      lastProgressCue = downloaded;
    }
    if (downloaded - lastLogged >= 65536 || (contentLength > 0 && downloaded == static_cast<size_t>(contentLength))) {
      AppLog.print("OTA downloaded bytes=");
      AppLog.println(downloaded);
      lastLogged = downloaded;
    }
    serviceOtaLoop(ledRing);
  }

  spoolFile.flush();
  spoolFile.close();

  if (contentLength > 0 && downloaded != static_cast<size_t>(contentLength)) {
    message = "OTA short download";
    AppLog.println(message);
    LittleFS.remove(OtaSpoolPath);
    http.end();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("short download");
    failWithCue();
    return false;
  }

  unsigned char digest[32] = {};
  if (mbedtls_sha256_finish(&shaContext, digest) != 0) {
    message = "OTA SHA256 finalize failed";
    AppLog.println("OTA SHA256 finalize failed");
    LittleFS.remove(OtaSpoolPath);
    http.end();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("sha finalize failed");
    failWithCue();
    return false;
  }
  mbedtls_sha256_free(&shaContext);
  const String actualSha = sha256Hex(digest);
  if (!equalsIgnoreCase(actualSha, request.sha256)) {
    message = "OTA SHA256 mismatch";
    AppLog.println("OTA SHA256 mismatch");
    LittleFS.remove(OtaSpoolPath);
    http.end();
    activity.finishError("sha mismatch");
    failWithCue();
    return false;
  }
  AppLog.println("OTA SHA256 verified");
  http.end();
  secureClient.stop();
  serviceOtaLoop(ledRing);

  const size_t updateSize = contentLength > 0 ? static_cast<size_t>(contentLength) : downloaded;
  AppLog.print("OTA update partition bytes=");
  AppLog.println(ESP.getFreeSketchSpace());
  if (!Update.begin(updateSize > 0 ? updateSize : UPDATE_SIZE_UNKNOWN)) {
    message = "OTA Update.begin failed";
    AppLog.print("OTA Update.begin error: ");
    AppLog.println(Update.errorString());
    LittleFS.remove(OtaSpoolPath);
    activity.finishError("Update.begin failed");
    failWithCue();
    return false;
  }

  fs::File updateFile = LittleFS.open(OtaSpoolPath, "r");
  if (!updateFile) {
    message = "OTA spool read failed";
    AppLog.println(message);
    Update.abort();
    LittleFS.remove(OtaSpoolPath);
    activity.finishError("spool read failed");
    failWithCue();
    return false;
  }

  size_t written = 0;
  lastLogged = 0;
  while (updateFile.available()) {
    serviceOtaLoop(ledRing);
    const size_t read = updateFile.read(buffer.get(), OtaDownloadBufferBytes);
    if (read == 0) {
      continue;
    }
    const size_t chunkWritten = Update.write(buffer.get(), read);
    if (chunkWritten != read) {
      message = "OTA write failed";
      AppLog.println("OTA write failed");
      updateFile.close();
      Update.abort();
      LittleFS.remove(OtaSpoolPath);
      activity.finishError("write failed");
      failWithCue();
      return false;
    }
    written += chunkWritten;
    if (written - lastLogged >= 65536 || written == updateSize) {
      AppLog.print("OTA written bytes=");
      AppLog.println(written);
      lastLogged = written;
    }
  }
  updateFile.close();

  if (written != updateSize) {
    message = "OTA short write";
    AppLog.println(message);
    Update.abort();
    LittleFS.remove(OtaSpoolPath);
    activity.finishError("short write");
    failWithCue();
    return false;
  }

  if (!Update.end(true)) {
    message = "OTA finalize failed";
    AppLog.print("OTA finalize error: ");
    AppLog.println(Update.errorString());
    LittleFS.remove(OtaSpoolPath);
    activity.finishError("finalize failed");
    failWithCue();
    return false;
  }

  LittleFS.remove(OtaSpoolPath);
  message = "OTA started";
  AppLog.println("OTA update written successfully");
  if (sound != nullptr) {
    sound->playOtaComplete();
    delay(320);
  }
  activity.finish(code, "written");
  return true;
}

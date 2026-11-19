// Firmware OTA download/install helper for Home Assistant-triggered updates.
#include "DJConnectOTA.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <memory>

#include "AppLog.h"
#include "Config.h"
#include "GitHubTls.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "MemoryDiagnostics.h"
#include "NetworkActivity.h"
#include "ScopedWatchdogPause.h"

namespace {
constexpr size_t OtaDownloadBufferBytes = 1460;
constexpr uint8_t OtaMaxRedirects = 5;
constexpr uint16_t OtaReleaseAssetReadTimeoutMs = 5000;

struct HeapCapsDeleter {
  void operator()(uint8_t *ptr) const {
    if (ptr != nullptr) {
      heap_caps_free(ptr);
    }
  }
};

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

void serviceOtaLoop(DisplayManager *display, LedRing *ledRing) {
  ScopedWatchdogPause::resetIfAttached();
  if (display != nullptr) {
    display->forceBacklightPercent(100);
  }
  if (ledRing != nullptr) {
    ledRing->showFirmwareUpdateAnimation();
  }
  delay(1);
  yield();
}

bool readTlsHeaderLine(WiFiClientSecure &client, String &line, DisplayManager *display, LedRing *ledRing) {
  line = "";
  const uint32_t startedAt = millis();
  while (millis() - startedAt <= Config::OtaStreamIdleTimeoutMs) {
    serviceOtaLoop(display, ledRing);
    int value = -1;
    {
      ScopedWatchdogPause watchdogPause;
      value = client.read();
    }
    if (value >= 0) {
      const char c = static_cast<char>(value);
      if (c == '\n') {
        line.trim();
        return true;
      }
      if (c != '\r') {
        line += c;
      }
      if (line.length() > 1024) {
        return false;
      }
      continue;
    }
    if (!client.connected() && client.available() <= 0) {
      return line.length() > 0;
    }
    delay(5);
  }
  return false;
}

bool writeTlsAll(WiFiClientSecure &client, const String &data, size_t &written, DisplayManager *display, LedRing *ledRing) {
  written = 0;
  const char *cursor = data.c_str();
  size_t remaining = data.length();
  while (remaining > 0) {
    serviceOtaLoop(display, ledRing);
    const size_t chunk = min(remaining, static_cast<size_t>(256));
    size_t bytesWritten = 0;
    {
      ScopedWatchdogPause watchdogPause;
      bytesWritten = client.write(reinterpret_cast<const uint8_t *>(cursor), chunk);
    }
    if (bytesWritten == 0) {
      return false;
    }
    cursor += bytesWritten;
    remaining -= bytesWritten;
    written += bytesWritten;
  }
  return true;
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

String urlPath(const String &url) {
  const int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) {
    return "/";
  }
  const int pathStart = url.indexOf('/', schemeEnd + 3);
  if (pathStart < 0) {
    return "/";
  }
  return url.substring(pathStart);
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

bool isReleaseAssetHost(const String &host) {
  return stripPort(host) == "release-assets.githubusercontent.com";
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
    const String host = urlHost(downloadUrl);
    AppLog.print("OTA download host: ");
    AppLog.println(host);
    logHostResolution(host);
    if (isReleaseAssetHost(host)) {
      code = HTTP_CODE_OK;
      break;
    }

    NetworkActivity::configureHttp(http, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
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
    serviceOtaLoop(display, ledRing);
  }

  const String finalHost = urlHost(downloadUrl);
  const bool rawReleaseAssetDownload = isReleaseAssetHost(finalHost);
  bool updateStarted = false;
  auto abortUpdateIfStarted = [&]() {
    if (updateStarted) {
      Update.abort();
      updateStarted = false;
    }
  };

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

  int contentLength = -1;
  if (rawReleaseAssetDownload) {
    http.end();
    secureClient.stop();
    secureClient.setTimeout(OtaReleaseAssetReadTimeoutMs);
    secureClient.setCACert(caForDownloadHost(finalHost));
    const String hostname = stripPort(finalHost);
    AppLog.println("OTA release asset mode: raw tls v2");
    AppLog.println("OTA release asset TLS connect");
    {
      ScopedWatchdogPause watchdogPause;
      if (!secureClient.connect(hostname.c_str(), 443)) {
        message = "OTA release asset connect failed";
        AppLog.println(message);
        logTlsError(secureClient);
        activity.finishError("asset connect failed");
        abortUpdateIfStarted();
        failWithCue();
        return false;
      }
    }
    serviceOtaLoop(display, ledRing);
    AppLog.println("OTA release asset GET");
    String assetRequest;
    assetRequest.reserve(urlPath(downloadUrl).length() + hostname.length() + 128);
    assetRequest += "GET ";
    assetRequest += urlPath(downloadUrl);
    assetRequest += " HTTP/1.0\r\nHost: ";
    assetRequest += hostname;
    assetRequest += "\r\nUser-Agent: DJConnect\r\nAccept: application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
    size_t requestBytesWritten = 0;
    if (!writeTlsAll(secureClient, assetRequest, requestBytesWritten, display, ledRing)) {
      message = "OTA release asset request write failed";
      AppLog.println(message);
      AppLog.print("OTA release asset request bytes=");
      AppLog.print(requestBytesWritten);
      AppLog.print("/");
      AppLog.println(assetRequest.length());
      logTlsError(secureClient);
      activity.finishError("asset request write failed");
      abortUpdateIfStarted();
      failWithCue();
      return false;
    }
    serviceOtaLoop(display, ledRing);
    if (secureClient.getWriteError() != 0) {
      AppLog.print("OTA release asset request write error: ");
      AppLog.println(secureClient.getWriteError());
      secureClient.clearWriteError();
    }
    AppLog.print("OTA release asset request sent bytes=");
    AppLog.println(requestBytesWritten);

    String statusLine;
    if (!readTlsHeaderLine(secureClient, statusLine, display, ledRing)) {
      message = "OTA release asset status timeout";
      AppLog.println(message);
      logTlsError(secureClient);
      activity.finishError("asset status timeout");
      abortUpdateIfStarted();
      failWithCue();
      return false;
    }
    AppLog.print("OTA release asset status: ");
    AppLog.println(statusLine);
    if (!statusLine.startsWith("HTTP/1.1 200") && !statusLine.startsWith("HTTP/1.0 200")) {
      message = "OTA release asset HTTP failed";
      activity.finishError("asset http failed");
      abortUpdateIfStarted();
      failWithCue();
      return false;
    }
    while (secureClient.connected()) {
      String headerLine;
      if (!readTlsHeaderLine(secureClient, headerLine, display, ledRing)) {
        message = "OTA release asset header timeout";
        AppLog.println(message);
        activity.finishError("asset header timeout");
        abortUpdateIfStarted();
        failWithCue();
        return false;
      }
      if (headerLine.isEmpty()) {
        break;
      }
      if (headerLine.startsWith("Content-Length:") || headerLine.startsWith("content-length:")) {
        contentLength = headerLine.substring(headerLine.indexOf(':') + 1).toInt();
      }
    }
  } else {
    contentLength = http.getSize();
  }

  std::unique_ptr<uint8_t, HeapCapsDeleter> buffer(static_cast<uint8_t *>(heap_caps_malloc(
      OtaDownloadBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  if (!buffer) {
    message = "OTA buffer allocation failed";
    AppLog.println(message);
    http.end();
    secureClient.stop();
    abortUpdateIfStarted();
    activity.finishError("buffer alloc failed");
    failWithCue();
    return false;
  }

  AppLog.print("OTA content length=");
  AppLog.println(contentLength);
  MemoryDiagnostics::log("ota_before_update_begin");

  const size_t updateSize = contentLength > 0 ? static_cast<size_t>(contentLength) : UPDATE_SIZE_UNKNOWN;
  if (!updateStarted) {
    AppLog.print("OTA update partition bytes=");
    AppLog.println(ESP.getFreeSketchSpace());
    if (!Update.begin(updateSize)) {
      message = "OTA Update.begin failed";
      AppLog.print("OTA Update.begin error: ");
      AppLog.println(Update.errorString());
      http.end();
      secureClient.stop();
      activity.finishError("Update.begin failed");
      failWithCue();
      return false;
    }
    updateStarted = true;
    MemoryDiagnostics::log("ota_after_update_begin");
  }

  Stream *stream = rawReleaseAssetDownload ? static_cast<Stream *>(&secureClient) : http.getStreamPtr();
  size_t downloaded = 0;
  size_t lastLogged = 0;
  size_t lastProgressCue = 0;
  uint32_t lastProgressAt = millis();
  bool loggedFirstBodyBytes = false;
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  if (mbedtls_sha256_starts(&shaContext, 0) != 0) {
    message = "OTA SHA256 init failed";
    abortUpdateIfStarted();
    http.end();
    secureClient.stop();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("sha init failed");
    failWithCue();
    return false;
  }
  while (stream != nullptr && (contentLength <= 0 || downloaded < static_cast<size_t>(contentLength))) {
    serviceOtaLoop(display, ledRing);
    const int available = stream->available();
    size_t toRead = OtaDownloadBufferBytes;
    if (available > 0) {
      toRead = min(static_cast<size_t>(available), OtaDownloadBufferBytes);
    }
    if (contentLength > 0) {
      toRead = min(toRead, static_cast<size_t>(contentLength) - downloaded);
    }
    if (toRead == 0) {
      break;
    }

    size_t read = 0;
    if (rawReleaseAssetDownload) {
      int rawRead = 0;
      ScopedWatchdogPause watchdogPause;
      rawRead = secureClient.read(buffer.get(), toRead);
      read = rawRead > 0 ? static_cast<size_t>(rawRead) : 0;
    } else {
      read = stream->readBytes(buffer.get(), toRead);
    }
    if (read == 0) {
      if (!http.connected() && contentLength <= 0) {
        break;
      }
      if (millis() - lastProgressAt > Config::OtaStreamIdleTimeoutMs) {
        message = "OTA stream timeout";
        AppLog.println("OTA stream timeout");
        abortUpdateIfStarted();
        http.end();
        secureClient.stop();
        mbedtls_sha256_free(&shaContext);
        activity.finishError("stream timeout");
        failWithCue();
        return false;
      }
      delay(10);
      continue;
    }
    if (!loggedFirstBodyBytes) {
      AppLog.print("OTA first body bytes=");
      AppLog.println(read);
      loggedFirstBodyBytes = true;
    }
    serviceOtaLoop(display, ledRing);
    if (mbedtls_sha256_update(&shaContext, buffer.get(), read) != 0) {
      message = "OTA SHA256 update failed";
      AppLog.println("OTA SHA256 update failed");
      abortUpdateIfStarted();
      http.end();
      secureClient.stop();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("sha update failed");
      failWithCue();
      return false;
    }
    serviceOtaLoop(display, ledRing);
    const size_t chunkWritten = Update.write(buffer.get(), read);
    if (chunkWritten != read) {
      message = "OTA write failed";
      AppLog.print("OTA write failed written=");
      AppLog.print(chunkWritten);
      AppLog.print("/");
      AppLog.print(read);
      AppLog.print(" progress=");
      AppLog.print(Update.progress());
      AppLog.print(" remaining=");
      AppLog.print(Update.remaining());
      AppLog.print(" first_byte=0x");
      AppLog.print(buffer.get()[0], HEX);
      AppLog.print(" error=");
      AppLog.println(Update.errorString());
      abortUpdateIfStarted();
      http.end();
      secureClient.stop();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("write failed");
      failWithCue();
      return false;
    }

    downloaded += chunkWritten;
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
    serviceOtaLoop(display, ledRing);
  }

  if (contentLength > 0 && downloaded != static_cast<size_t>(contentLength)) {
    message = "OTA short download";
    AppLog.println(message);
    abortUpdateIfStarted();
    http.end();
    secureClient.stop();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("short download");
    failWithCue();
    return false;
  }

  unsigned char digest[32] = {};
  if (mbedtls_sha256_finish(&shaContext, digest) != 0) {
    message = "OTA SHA256 finalize failed";
    AppLog.println("OTA SHA256 finalize failed");
    abortUpdateIfStarted();
    http.end();
    secureClient.stop();
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
    abortUpdateIfStarted();
    http.end();
    secureClient.stop();
    activity.finishError("sha mismatch");
    failWithCue();
    return false;
  }
  AppLog.println("OTA SHA256 verified");
  http.end();
  secureClient.stop();
  serviceOtaLoop(display, ledRing);

  if (!Update.end(true)) {
    message = "OTA finalize failed";
    AppLog.print("OTA finalize error: ");
    AppLog.println(Update.errorString());
    activity.finishError("finalize failed");
    failWithCue();
    return false;
  }

  message = "OTA started";
  AppLog.println("OTA update written successfully");
  if (sound != nullptr) {
    sound->playOtaComplete();
    delay(320);
  }
  activity.finish(code, "written");
  return true;
}

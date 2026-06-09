// Firmware OTA download/install helper for Home Assistant-triggered updates.
#include "DJConnectOTA.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>
#include <memory>

#include "AppLog.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "ScopedWatchdogPause.h"

namespace {
const char *ExpectedModel = "lilygo-t-embed-s3";
constexpr size_t OtaDownloadBufferBytes = 1460;

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
  if (request.device != ExpectedModel) {
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

  HTTPClient http;
  NetworkActivity activity("ota_download", Config::OtaIoTimeoutMs);
  NetworkActivity::configureHttp(http, Config::OtaConnectTimeoutMs, Config::OtaIoTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(5);
  WiFiClientSecure secureClient;
  // Firmware authenticity is enforced with the manifest SHA256 below.
  secureClient.setInsecure();
  secureClient.setTimeout(Config::OtaIoTimeoutMs);

  const bool begun = http.begin(secureClient, request.url);
  if (!begun) {
    message = "OTA HTTP begin failed";
    activity.finishError("begin failed");
    failWithCue();
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    message = "OTA download failed " + String(code);
    AppLog.println(message);
    http.end();
    activity.finish(code);
    failWithCue();
    return false;
  }

  const int contentLength = http.getSize();
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    message = "OTA Update.begin failed";
    AppLog.print("OTA Update.begin error: ");
    AppLog.println(Update.errorString());
    http.end();
    activity.finishError("Update.begin failed");
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
  Stream *stream = http.getStreamPtr();
  size_t written = 0;
  size_t lastLogged = 0;
  size_t lastProgressCue = 0;
  uint32_t lastProgressAt = millis();
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  if (mbedtls_sha256_starts(&shaContext, 0) != 0) {
    message = "OTA SHA256 init failed";
    Update.abort();
    http.end();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("sha init failed");
    failWithCue();
    return false;
  }
  while (stream != nullptr && (contentLength <= 0 || written < static_cast<size_t>(contentLength))) {
    serviceOtaLoop(ledRing);
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected() && contentLength <= 0) {
        break;
      }
      if (millis() - lastProgressAt > Config::OtaStreamIdleTimeoutMs) {
        message = "OTA stream timeout";
        AppLog.println("OTA stream timeout");
        Update.abort();
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
      toRead = min(toRead, static_cast<size_t>(contentLength) - written);
    }
    const size_t read = stream->readBytes(buffer.get(), toRead);
    if (read == 0) {
      continue;
    }
    serviceOtaLoop(ledRing);
    if (mbedtls_sha256_update(&shaContext, buffer.get(), read) != 0) {
      message = "OTA SHA256 update failed";
      AppLog.println("OTA SHA256 update failed");
      Update.abort();
      http.end();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("sha update failed");
      failWithCue();
      return false;
    }
    serviceOtaLoop(ledRing);
    const size_t chunkWritten = Update.write(buffer.get(), read);
    if (chunkWritten != read) {
      message = "OTA write failed";
      AppLog.println("OTA write failed");
      Update.abort();
      http.end();
      mbedtls_sha256_free(&shaContext);
      activity.finishError("write failed");
      failWithCue();
      return false;
    }

    written += chunkWritten;
    lastProgressAt = millis();
    if (sound != nullptr && written - lastProgressCue >= 196608) {
      sound->playOtaProgress();
      lastProgressCue = written;
    }
    if (written - lastLogged >= 65536 || (contentLength > 0 && written == static_cast<size_t>(contentLength))) {
      AppLog.print("OTA written bytes=");
      AppLog.println(written);
      lastLogged = written;
    }
    serviceOtaLoop(ledRing);
  }

  if (contentLength > 0 && written != static_cast<size_t>(contentLength)) {
    message = "OTA short write";
    AppLog.println("OTA short write");
    Update.abort();
    http.end();
    mbedtls_sha256_free(&shaContext);
    activity.finishError("short write");
    failWithCue();
    return false;
  }

  unsigned char digest[32] = {};
  if (mbedtls_sha256_finish(&shaContext, digest) != 0) {
    message = "OTA SHA256 finalize failed";
    AppLog.println("OTA SHA256 finalize failed");
    Update.abort();
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
    Update.abort();
    http.end();
    activity.finishError("sha mismatch");
    failWithCue();
    return false;
  }
  AppLog.println("OTA SHA256 verified");

  if (!Update.end(true)) {
    message = "OTA finalize failed";
    AppLog.print("OTA finalize error: ");
    AppLog.println(Update.errorString());
    http.end();
    activity.finishError("finalize failed");
    failWithCue();
    return false;
  }

  http.end();
  message = "OTA started";
  AppLog.println("OTA update written successfully");
  if (sound != nullptr) {
    sound->playOtaComplete();
    delay(320);
  }
  activity.finish(code, "written");
  return true;
}

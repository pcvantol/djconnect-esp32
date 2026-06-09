// Sends recognized push-to-talk text to the DJConnect Home Assistant integration.
#include "VoiceHttpClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "AppLog.h"
#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "ScopedWatchdogPause.h"

namespace {
String readHttpBodyWithWatchdog(HTTPClient &http, uint32_t timeoutMs) {
  String body;
  WiFiClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    return body;
  }

  const int contentLength = http.getSize();
  if (contentLength > 0) {
    body.reserve(contentLength);
  }

  uint32_t lastDataAt = millis();
  int remaining = contentLength;
  while (remaining != 0 && millis() - lastDataAt < timeoutMs) {
    ScopedWatchdogPause::resetIfAttached();
    const int available = stream->available();
    if (available <= 0) {
      delay(1);
      yield();
      continue;
    }

    char buffer[193] = {};
    const size_t want = contentLength > 0
                            ? min(static_cast<int>(sizeof(buffer) - 1), remaining)
                            : min(static_cast<int>(sizeof(buffer) - 1), available);
    const int got = stream->readBytes(buffer, want);
    if (got <= 0) {
      delay(1);
      yield();
      continue;
    }
    buffer[got] = '\0';
    body += buffer;
    lastDataAt = millis();
    if (remaining > 0) {
      remaining -= got;
    }
  }
  ScopedWatchdogPause::resetIfAttached();
  return body;
}

String compactHttpErrorBody(const String &body) {
  String compact;
  compact.reserve(min(static_cast<size_t>(body.length()), static_cast<size_t>(160)));
  for (size_t i = 0; i < body.length() && compact.length() < 160; ++i) {
    const char c = body.charAt(i);
    if (c == '\r' || c == '\n' || c == '\t') {
      if (!compact.endsWith(" ")) {
        compact += ' ';
      }
    } else {
      compact += c;
    }
  }
  compact.trim();
  return compact;
}

void logVoiceHttpErrorBody(const String &context, const String &body) {
  const String compact = compactHttpErrorBody(body);
  if (compact.isEmpty()) {
    return;
  }
  AppLog.print(context);
  AppLog.print(": ");
  AppLog.println(compact);
}

void logVoiceHttpClientError(const String &context, int code) {
  if (code >= 0) {
    return;
  }
  AppLog.print(context);
  AppLog.print(": ");
  AppLog.println(HTTPClient::errorToString(code));
}

static constexpr uint8_t HaNotFoundInvalidationThreshold = 3;
static constexpr uint32_t HaNotFoundInvalidationWindowMs = 60000;
}  // namespace

void VoiceHttpClient::begin(DJConnectDevice &device) {
  device_ = &device;
}

bool VoiceHttpClient::sendStatus(bool recording, const String &state, const String &lastError) {
  pairingInvalidated_ = false;
  (void)recording;
  (void)state;
  (void)lastError;
  return device_ != nullptr && device_->isPaired();
}

bool VoiceHttpClient::sendRecognizedText(const String &recognizedText, String &message, String *audioUrl) {
  pairingInvalidated_ = false;
  if (device_ == nullptr || !device_->isPaired()) {
    message = "No HA pairing";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    message = "WiFi disconnected";
    return false;
  }
  const String token = device_->getDeviceToken();
  const String url = endpoint("/api/djconnect/voice");
  if (token.isEmpty()) {
    message = "No device token";
    return false;
  }
  if (url.isEmpty()) {
    message = "No HA URL";
    return false;
  }
  if (!Logic::shouldSendRecognizedVoiceText(recognizedText.length())) {
    message = "No speech recognized";
    return false;
  }

  JsonDocument doc;
  doc["device_id"] = device_->getDeviceId();
  doc["client_type"] = device_->getClientType();
  doc["text"] = recognizedText;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  NetworkActivity activity("voice_command", Config::VoiceCommandIoTimeoutMs);
  NetworkActivity::configureHttp(http, Config::HttpConnectTimeoutMs, Config::VoiceCommandIoTimeoutMs);
  if (!http.begin(url)) {
    message = "Voice HTTP begin failed";
    activity.finishError("begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-DJConnect-Device-ID", device_->getDeviceId());
  http.addHeader("X-DJConnect-Text", recognizedText);

  AppLog.print("Voice text command chars=");
  AppLog.println(recognizedText.length());
  ScopedWatchdogPause::resetIfAttached();
  int code = 0;
  {
    ScopedWatchdogPause watchdogPause;
    code = http.POST(body);
  }
  ScopedWatchdogPause::resetIfAttached();
  AppLog.print("Voice command response: ");
  AppLog.println(code);
  logVoiceHttpClientError("Voice command client error", code);
  const String response = readHttpBodyWithWatchdog(http, Config::VoiceCommandIoTimeoutMs);
  http.end();
  activity.finish(code);
  if (code < 200 || code >= 300) {
    logVoiceHttpErrorBody("Voice command error body", response);
    JsonDocument errorDoc;
    const DeserializationError jsonError = deserializeJson(errorDoc, response);
    const char *errorKey = jsonError ? "" : (errorDoc["error"] | "");
    const char *errorMessage = jsonError ? "" : (errorDoc["message"] | "");
    if (Logic::isDjConnectInvalidClientType(errorKey)) {
      AppLog.println("HA rejected payload: missing client_type=esp32");
      message = "HA rejected payload: missing client_type=esp32";
    } else if (Logic::isDjConnectVersionMismatch(code, errorKey)) {
      updatePairingInvalidationForStatus(code);
      message = errorDoc["message"] | "Update DJConnect firmware/integration";
    } else if (code == 404) {
      updatePairingInvalidationForStatus(code);
      message = I18n::text("voice_ha_endpoint_missing");
    } else if (code == 401 || code == 403) {
      updatePairingInvalidationForStatus(code);
      message = I18n::text("voice_ha_auth_failed");
    } else {
      updatePairingInvalidationForStatus(code);
      message = strlen(errorMessage) > 0 ? String(errorMessage) : "Voice HTTP " + String(code);
    }
    return false;
  }

  JsonDocument responseDoc;
  if (!response.isEmpty() && !deserializeJson(responseDoc, response)) {
    const char *responseText = responseDoc["text"] | responseDoc["dj_text"] | "";
    const char *responseAudioUrl = responseDoc["audio_url"] | "";
    if (audioUrl != nullptr) {
      *audioUrl = responseAudioUrl;
    }
    if (strlen(responseText) > 0) {
      message = responseText;
      return true;
    }
  }
  message = "Voice command sent";
  return true;
}

bool VoiceHttpClient::uploadWav(const String &path, String &message, String *audioUrl) {
  pairingInvalidated_ = false;
  if (device_ == nullptr || !device_->isPaired()) {
    message = "No HA pairing";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    message = "WiFi disconnected";
    return false;
  }
  const String token = device_->getDeviceToken();
  const String url = endpoint("/api/djconnect/voice");
  if (token.isEmpty()) {
    message = "No device token";
    return false;
  }
  if (url.isEmpty()) {
    message = "No HA URL";
    return false;
  }

  fs::File file = LittleFS.open(path, "r");
  if (!file) {
    message = "Voice file open failed";
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > Config::VoiceMaxWavBytes) {
    file.close();
    message = "Audio too large";
    return false;
  }

  HTTPClient http;
  NetworkActivity activity("voice_wav_upload", Config::VoiceCommandIoTimeoutMs);
  NetworkActivity::configureHttp(http, Config::HttpConnectTimeoutMs, Config::VoiceCommandIoTimeoutMs);
  if (!http.begin(url)) {
    file.close();
    message = "Voice HTTP begin failed";
    activity.finishError("begin failed");
    return false;
  }
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-DJConnect-Device-ID", device_->getDeviceId());
  AppLog.print("Voice WAV upload bytes=");
  AppLog.println(fileSize);

  int code = 0;
  {
    ScopedWatchdogPause watchdogPause;
    code = http.sendRequest("POST", &file, fileSize);
  }
  file.close();
  ScopedWatchdogPause::resetIfAttached();
  AppLog.print("Voice WAV response: ");
  AppLog.println(code);
  logVoiceHttpClientError("Voice WAV client error", code);
  const String response = readHttpBodyWithWatchdog(http, Config::VoiceCommandIoTimeoutMs);
  http.end();
  activity.finish(code);

  if (code < 200 || code >= 300) {
    logVoiceHttpErrorBody("Voice WAV error body", response);
    JsonDocument errorDoc;
    const DeserializationError jsonError = deserializeJson(errorDoc, response);
    const char *errorKey = jsonError ? "" : (errorDoc["error"] | "");
    const char *errorMessage = jsonError ? "" : (errorDoc["message"] | "");
    if (Logic::isDjConnectInvalidClientType(errorKey)) {
      AppLog.println("HA rejected payload: missing client_type=esp32");
      message = "HA rejected payload: missing client_type=esp32";
    } else if (Logic::isDjConnectVersionMismatch(code, errorKey)) {
      updatePairingInvalidationForStatus(code);
      message = errorDoc["message"] | "Update DJConnect firmware/integration";
    } else if (code == 404) {
      updatePairingInvalidationForStatus(code);
      message = I18n::text("voice_ha_endpoint_missing");
    } else if (code == 401 || code == 403) {
      updatePairingInvalidationForStatus(code);
      message = I18n::text("voice_ha_auth_failed");
    } else {
      updatePairingInvalidationForStatus(code);
      message = strlen(errorMessage) > 0 ? String(errorMessage) : "Voice HTTP " + String(code);
    }
    return false;
  }

  JsonDocument responseDoc;
  if (!response.isEmpty() && !deserializeJson(responseDoc, response)) {
    const char *responseText = responseDoc["text"] | responseDoc["dj_text"] | "";
    const char *responseAudioUrl = responseDoc["audio_url"] | "";
    if (audioUrl != nullptr) {
      *audioUrl = responseAudioUrl;
    }
    if (strlen(responseText) > 0) {
      message = responseText;
      return true;
    }
  }
  message = "Voice command sent";
  return true;
}

bool VoiceHttpClient::pairingInvalidated() const {
  return pairingInvalidated_;
}

bool VoiceHttpClient::updatePairingInvalidationForStatus(int statusCode) {
  if (statusCode == 404) {
    const uint32_t now = millis();
    if (firstHaNotFoundAt_ == 0 || now - firstHaNotFoundAt_ > HaNotFoundInvalidationWindowMs) {
      firstHaNotFoundAt_ = now;
      consecutiveHaNotFoundCount_ = 0;
    }
    consecutiveHaNotFoundCount_++;
    AppLog.print("Home Assistant route not found count=");
    AppLog.println(consecutiveHaNotFoundCount_);
    pairingInvalidated_ = consecutiveHaNotFoundCount_ >= HaNotFoundInvalidationThreshold;
    return pairingInvalidated_;
  }

  consecutiveHaNotFoundCount_ = 0;
  firstHaNotFoundAt_ = 0;
  pairingInvalidated_ = statusCode == 401 || statusCode == 403;
  return pairingInvalidated_;
}

String VoiceHttpClient::endpoint(const char *path) const {
  if (device_ == nullptr) {
    return "";
  }
  String base = device_->getActiveHaUrl();
  if (base.isEmpty()) {
    return "";
  }
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base + path;
}

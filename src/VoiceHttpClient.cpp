// Sends recognized push-to-talk text to the SpotifyDJ Home Assistant integration.
#include "VoiceHttpClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "AppLog.h"
#include "Config.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"

void VoiceHttpClient::begin(SpotifyDJDevice &device) {
  device_ = &device;
}

bool VoiceHttpClient::sendStatus(bool recording, const String &state, const String &lastError) {
  if (device_ == nullptr || !device_->isPaired()) {
    return false;
  }
  const String token = device_->getDeviceToken();
  const String url = endpoint("/api/spotify_dj/status");
  if (token.isEmpty() || url.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  doc["device_id"] = device_->getDeviceId();
  doc["recording"] = recording;
  doc["state"] = state;
  if (!lastError.isEmpty()) {
    doc["last_error"] = lastError;
  }
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  NetworkActivity activity("voice_status", Config::HttpLongIoTimeoutMs);
  NetworkActivity::configureLongHttp(http);
  if (!http.begin(url)) {
    activity.finishError("begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());
  const int code = http.POST(body);
  http.end();
  activity.finish(code);
  AppLog.print("[SpotifyDJ] voice status response: ");
  AppLog.println(code);
  return code >= 200 && code < 300;
}

bool VoiceHttpClient::sendRecognizedText(const String &recognizedText, String &message, String *audioUrl) {
  if (device_ == nullptr || !device_->isPaired()) {
    message = "No HA pairing";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    message = "WiFi disconnected";
    return false;
  }
  const String token = device_->getDeviceToken();
  const String url = endpoint("/api/spotify_dj/voice");
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
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());
  http.addHeader("X-SpotifyDJ-Text", recognizedText);

  AppLog.print("[SpotifyDJ] voice text command chars=");
  AppLog.println(recognizedText.length());
  const int code = http.POST(body);
  AppLog.print("[SpotifyDJ] voice command response: ");
  AppLog.println(code);
  const String response = http.getString();
  http.end();
  activity.finish(code);
  if (code < 200 || code >= 300) {
    message = "Voice HTTP " + String(code);
    return false;
  }

  JsonDocument responseDoc;
  if (!response.isEmpty() && !deserializeJson(responseDoc, response)) {
    const char *responseText = responseDoc["text"] | "";
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

String VoiceHttpClient::endpoint(const char *path) const {
  if (device_ == nullptr) {
    return "";
  }
  String base = device_->getHaUrl();
  if (base.isEmpty()) {
    return "";
  }
  if (base.endsWith("/")) {
    base.remove(base.length() - 1);
  }
  return base + path;
}

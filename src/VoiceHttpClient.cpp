// Sends push-to-talk WAV audio to Home Assistant and plays the WAV response.
#include "VoiceHttpClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "AppLog.h"

void VoiceHttpClient::begin(SpotifyDJDevice &device, SoundManager &sound) {
  device_ = &device;
  sound_ = &sound;
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
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  if (!http.begin(url)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());
  const int code = http.POST(body);
  http.end();
  AppLog.print("[SpotifyDJ] voice status response: ");
  AppLog.println(code);
  return code >= 200 && code < 300;
}

bool VoiceHttpClient::uploadAndPlay(const String &wavPath, String &message) {
  if (device_ == nullptr || sound_ == nullptr || !device_->isPaired()) {
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

  fs::File file = LittleFS.open(wavPath, "r");
  if (!file) {
    message = "Voice file missing";
    return false;
  }
  const size_t size = file.size();
  if (size == 0 || size > 2UL * 1024UL * 1024UL) {
    file.close();
    message = "Audio too large";
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(30000);
  if (!http.begin(url)) {
    file.close();
    message = "Voice HTTP begin failed";
    return false;
  }
  http.addHeader("Content-Type", "audio/wav");
  http.addHeader("Authorization", "Bearer " + token);
  http.addHeader("X-SpotifyDJ-Device-ID", device_->getDeviceId());

  AppLog.print("[SpotifyDJ] voice upload bytes: ");
  AppLog.println(size);
  const int code = http.sendRequest("POST", &file, size);
  file.close();
  AppLog.print("[SpotifyDJ] voice response: ");
  AppLog.println(code);
  if (code < 200 || code >= 300) {
    message = "Voice HTTP " + String(code);
    const String body = http.getString();
    if (!body.isEmpty()) {
      AppLog.println(body.substring(0, 96));
    }
    http.end();
    return false;
  }

  sendStatus(false, "playing_response");
  WiFiClient *stream = http.getStreamPtr();
  const bool played = stream != nullptr && sound_->playWavStream(*stream, http.getSize());
  http.end();
  if (!played) {
    message = "Voice playback failed";
    return false;
  }
  message = "Voice response played";
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

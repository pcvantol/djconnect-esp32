// DJ response audio dispatcher. Keeps HTTP type detection out of SpotifyDJApp.
#include "DjResponseAudioPlayer.h"

#include <HTTPClient.h>
#include <WiFi.h>

#include "AppLog.h"
#include "Config.h"
#include "LedRing.h"
#include "LogicHelpers.h"
#include "NetworkActivity.h"
#include "SoundManager.h"

namespace {
class PrefixStream : public Stream {
public:
  PrefixStream(const uint8_t *prefix, size_t prefixLength, Stream &inner)
      : prefix_(prefix),
        prefixLength_(prefixLength),
        inner_(inner) {}

  int available() override {
    return static_cast<int>(prefixLength_ - prefixOffset_) + inner_.available();
  }

  int read() override {
    if (prefixOffset_ < prefixLength_) {
      return prefix_[prefixOffset_++];
    }
    return inner_.read();
  }

  int peek() override {
    if (prefixOffset_ < prefixLength_) {
      return prefix_[prefixOffset_];
    }
    return inner_.peek();
  }

  void flush() override {
    inner_.flush();
  }

  size_t write(uint8_t) override {
    return 0;
  }

private:
  const uint8_t *prefix_ = nullptr;
  size_t prefixLength_ = 0;
  size_t prefixOffset_ = 0;
  Stream &inner_;
};
} // namespace

void DjResponseAudioPlayer::begin(SoundManager &sound, LedRing *ledRing) {
  sound_ = &sound;
  ledRing_ = ledRing;
}

void showDjResponseLedFrame(void *context) {
  auto *ring = static_cast<LedRing *>(context);
  if (ring != nullptr) {
    ring->showDjResponseAnimation();
  }
}

DjResponseAudioResult DjResponseAudioPlayer::play(const String &audioUrl) {
  DjResponseAudioResult result;
  if (sound_ == nullptr) {
    AppLog.println("DJ response audio skipped: speaker unavailable");
    result.audioType = "unknown";
    return result;
  }
  if (audioUrl.isEmpty()) {
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    AppLog.println("DJ response audio skipped: WiFi disconnected");
    result.audioType = "unknown";
    return result;
  }

  HTTPClient http;
  NetworkActivity activity("dj_audio_download", Config::DjAudioIoTimeoutMs);
  NetworkActivity::configureHttp(http, Config::HttpConnectTimeoutMs, Config::DjAudioIoTimeoutMs);
  if (!http.begin(audioUrl)) {
    AppLog.println("DJ response audio begin failed");
    activity.finishError("begin failed");
    result.audioType = "unknown";
    return result;
  }

  AppLog.println("DJ response audio download");
  static const char *headers[] = {"Content-Type"};
  http.collectHeaders(headers, 1);
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    AppLog.print("DJ response audio HTTP ");
    AppLog.println(code);
    http.end();
    activity.finish(code);
    result.audioType = "unknown";
    return result;
  }

  const int contentLength = http.getSize();
  Stream *stream = http.getStreamPtr();
  Logic::DjAudioType audioType = Logic::djAudioTypeFromContentType(http.header("Content-Type").c_str());
  uint8_t prefix[12] = {};
  size_t prefixLength = 0;
  if (stream != nullptr && audioType == Logic::DjAudioType::Unknown) {
    const uint32_t startedAt = millis();
    while (prefixLength < sizeof(prefix) && millis() - startedAt < Config::HttpConnectTimeoutMs) {
      const int value = stream->read();
      if (value >= 0) {
        prefix[prefixLength++] = static_cast<uint8_t>(value);
      } else {
        delay(1);
        yield();
      }
    }
    audioType = Logic::djAudioTypeFromHeaderBytes(prefix, prefixLength);
  }

  result.audioType = Logic::djAudioTypeName(audioType);
  bool ok = false;
  if (ledRing_ != nullptr) {
    sound_->setStreamActivityCallback(showDjResponseLedFrame, ledRing_);
    ledRing_->showDjResponseAnimation();
  }
  if (stream != nullptr && audioType == Logic::DjAudioType::Wav) {
    PrefixStream prefixed(prefix, prefixLength, *stream);
    ok = sound_->playWavStream(prefixed, contentLength);
  } else if (stream != nullptr && audioType == Logic::DjAudioType::Mp3) {
    ok = sound_->playMp3Stream(*stream, prefix, prefixLength, contentLength);
  } else {
    AppLog.println("DJ response audio type unsupported");
  }
  sound_->setStreamActivityCallback(nullptr, nullptr);

  http.end();
  activity.finish(code, ok ? "played" : "playback failed");
  result.spoken = ok;
  AppLog.print("DJ response audio type=");
  AppLog.print(result.audioType);
  AppLog.print(" played=");
  AppLog.println(ok ? "true" : "false");
  return result;
}

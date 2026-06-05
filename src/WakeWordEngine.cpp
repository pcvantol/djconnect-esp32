// Optional wake-word front-end for "Spotify DJ".
#include "WakeWordEngine.h"

#include "AppLog.h"
#include "Config.h"

extern "C" bool spotifydj_micro_wake_word_detect(const int16_t *samples, size_t sampleCount) __attribute__((weak));

void WakeWordEngine::begin() {
  available_ = spotifydj_micro_wake_word_detect != nullptr;
  AppLog.print("Wake word: ");
  AppLog.println(available_ ? "Spotify DJ model hook available" : "model hook not installed");
}

void WakeWordEngine::setCallback(Callback callback, void *context) {
  callback_ = callback;
  callbackContext_ = context;
}

void WakeWordEngine::loop(VoiceRecorder &recorder) {
  if (!enabled_ || !available_ || callback_ == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastPollAt_ < Config::WakeWordPollIntervalMs) {
    return;
  }
  lastPollAt_ = now;

  uint8_t audio[Config::WakeWordPcmChunkBytes] = {};
  size_t bytesRead = 0;
  if (!recorder.readMonitorChunk(audio, sizeof(audio), bytesRead) || bytesRead < sizeof(int16_t)) {
    return;
  }

  const size_t sampleCount = bytesRead / sizeof(int16_t);
  if (spotifydj_micro_wake_word_detect(reinterpret_cast<const int16_t *>(audio), sampleCount)) {
    AppLog.println("Wake word: Spotify DJ detected");
    callback_(callbackContext_);
  }
}

bool WakeWordEngine::available() const {
  return available_;
}

bool WakeWordEngine::enabled() const {
  return enabled_;
}

void WakeWordEngine::setEnabled(bool enabled) {
  enabled_ = enabled;
}

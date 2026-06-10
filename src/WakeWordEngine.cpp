// Optional wake-word front-end for "oke nabu" and legacy "DJConnect" hooks.
#include "WakeWordEngine.h"

#include "AppLog.h"
#include "Config.h"
#include "OkayNabuWakeWordModel.h"

extern "C" bool djconnect_micro_wake_word_detect(const int16_t *samples, size_t sampleCount) __attribute__((weak));
// Implemented only when a TFLite Micro/microWakeWord runtime is linked. The
// vendored Okay Nabu model bytes alone are not an executable detector.
extern "C" bool djconnect_oke_nabu_wake_word_detect(const int16_t *samples, size_t sampleCount) __attribute__((weak));
extern "C" void djconnect_oke_nabu_wake_word_release() __attribute__((weak));

void WakeWordEngine::begin() {
  available_ = djconnect_oke_nabu_wake_word_detect != nullptr || djconnect_micro_wake_word_detect != nullptr;
  AppLog.print("Wake word model: ");
  AppLog.print(kOkayNabuWakeWordName);
  AppLog.print(", bytes=");
  AppLog.print(kOkayNabuWakeWordModelLen);
  AppLog.print(", cutoff=");
  AppLog.println(Config::WakeWordProbabilityCutoff, 2);
  AppLog.print("Wake word: ");
  if (djconnect_oke_nabu_wake_word_detect != nullptr) {
    AppLog.println("oke nabu model hook available");
  } else if (djconnect_micro_wake_word_detect != nullptr) {
    AppLog.println("DJConnect legacy model hook available");
  } else {
    AppLog.println("model hook not installed");
  }
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
    if (now - lastEmptyLogAt_ > 5000) {
      AppLog.line("Wake word: monitor waiting for microphone samples");
      lastEmptyLogAt_ = now;
    }
    return;
  }
  if (!loggedFirstAudio_) {
    AppLog.line(String("Wake word: monitor active, chunk bytes=") + String(bytesRead));
    loggedFirstAudio_ = true;
  } else if (now - lastMonitorLogAt_ > 30000) {
    AppLog.line("Wake word: monitor active");
    lastMonitorLogAt_ = now;
  }

  const size_t sampleCount = bytesRead / sizeof(int16_t);
  const int16_t *samples = reinterpret_cast<const int16_t *>(audio);
  if (djconnect_oke_nabu_wake_word_detect != nullptr &&
      djconnect_oke_nabu_wake_word_detect(samples, sampleCount)) {
    AppLog.println("Wake word: oke nabu detected");
    callback_(callbackContext_);
    return;
  }
  if (djconnect_micro_wake_word_detect != nullptr &&
      djconnect_micro_wake_word_detect(samples, sampleCount)) {
    AppLog.println("Wake word: DJConnect detected");
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

void WakeWordEngine::releaseResources() {
  loggedFirstAudio_ = false;
  if (djconnect_oke_nabu_wake_word_release != nullptr) {
    djconnect_oke_nabu_wake_word_release();
  }
}

// Push-to-talk microphone recorder for the onboard PDM microphone.
#pragma once

#include <Arduino.h>

class VoiceRecorder {
public:
  bool begin();
  bool start();
  bool update();
  bool stop();
  bool abort();

  bool isRecording() const;
  bool isReady() const;
  uint32_t elapsedMs() const;
  size_t wavSize() const;
  String wavPath() const;
  String error() const;

private:
  void writePlaceholderHeader();
  bool rewriteWavHeader();

  bool ready_ = false;
  bool recording_ = false;
  uint32_t startedAt_ = 0;
  size_t dataBytes_ = 0;
  String error_;
};

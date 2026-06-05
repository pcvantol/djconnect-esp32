// Routes Home Assistant DJ response audio URLs to the supported on-device decoders.
#pragma once

#include <Arduino.h>

class SoundManager;

struct DjResponseAudioResult {
  bool spoken = false;
  String audioType = "none";
};

class DjResponseAudioPlayer {
public:
  // Stores the speaker backend used for WAV/MP3 playback.
  void begin(SoundManager &sound);

  // Downloads just enough metadata to detect the audio type, then streams it to the speaker.
  DjResponseAudioResult play(const String &audioUrl);

private:
  SoundManager *sound_ = nullptr;
};

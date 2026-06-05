// Routes Home Assistant DJ response audio URLs to the supported on-device decoders.
#pragma once

#include <Arduino.h>

class SoundManager;
class LedRing;

struct DjResponseAudioResult {
  bool spoken = false;
  String audioType = "none";
};

class DjResponseAudioPlayer {
public:
  // Stores the speaker and LED backends used for DJ response playback feedback.
  void begin(SoundManager &sound, LedRing *ledRing = nullptr);

  // Downloads just enough metadata to detect the audio type, then streams it to the speaker.
  DjResponseAudioResult play(const String &audioUrl);

private:
  SoundManager *sound_ = nullptr;
  LedRing *ledRing_ = nullptr;
};

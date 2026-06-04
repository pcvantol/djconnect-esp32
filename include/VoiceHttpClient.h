// Home Assistant voice upload client for push-to-talk WAV requests.
#pragma once

#include <Arduino.h>

#include "SoundManager.h"
#include "SpotifyDJDevice.h"

class VoiceHttpClient {
public:
  void begin(SpotifyDJDevice &device, SoundManager &sound);

  bool sendStatus(bool recording, const String &state, const String &lastError = "");
  bool uploadAndPlay(const String &wavPath, String &message);

private:
  String endpoint(const char *path) const;

  SpotifyDJDevice *device_ = nullptr;
  SoundManager *sound_ = nullptr;
};

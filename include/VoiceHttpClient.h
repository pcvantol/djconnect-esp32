// Home Assistant voice command client for recognized push-to-talk text.
#pragma once

#include <Arduino.h>

#include "SpotifyDJDevice.h"

class VoiceHttpClient {
public:
  void begin(SpotifyDJDevice &device);

  bool sendStatus(bool recording, const String &state, const String &lastError = "");
  bool sendRecognizedText(const String &recognizedText, String &message, String *audioUrl = nullptr);
  bool pairingInvalidated() const;

private:
  String endpoint(const char *path) const;

  SpotifyDJDevice *device_ = nullptr;
  bool pairingInvalidated_ = false;
};

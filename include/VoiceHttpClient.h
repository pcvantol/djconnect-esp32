// Home Assistant voice command client for recognized push-to-talk text.
#pragma once

#include <Arduino.h>

#include "DJConnectDevice.h"

class VoiceHttpClient {
public:
  using ActivityCallback = void (*)(void *context);

  void begin(DJConnectDevice &device);
  void setActivityCallback(ActivityCallback callback, void *context);

  bool sendStatus(bool recording, const String &state, const String &lastError = "");
  bool sendRecognizedText(const String &recognizedText, String &message, String *audioUrl = nullptr);
  bool uploadWav(const String &path, String &message, String *audioUrl = nullptr);
  bool pairingInvalidated() const;

private:
  String endpoint(const char *path) const;
  bool updatePairingInvalidationForStatus(int statusCode);

  DJConnectDevice *device_ = nullptr;
  bool pairingInvalidated_ = false;
  uint8_t consecutiveHaNotFoundCount_ = 0;
  uint32_t firstHaNotFoundAt_ = 0;
  ActivityCallback activityCallback_ = nullptr;
  void *activityContext_ = nullptr;
};

// Optional wake-word front-end for "Spotify DJ".
#pragma once

#include <Arduino.h>

#include "VoiceRecorder.h"

class WakeWordEngine {
public:
  using Callback = void (*)(void *context);

  // Starts the wake-word layer. It is available only when a model hook is linked in.
  void begin();

  // Sets the callback invoked after the wake phrase is detected.
  void setCallback(Callback callback, void *context);

  // Reads idle microphone frames and runs the linked detector, if present.
  void loop(VoiceRecorder &recorder);

  bool available() const;
  bool enabled() const;
  void setEnabled(bool enabled);

private:
  Callback callback_ = nullptr;
  void *callbackContext_ = nullptr;
  bool available_ = false;
  bool enabled_ = true;
  uint32_t lastPollAt_ = 0;
};

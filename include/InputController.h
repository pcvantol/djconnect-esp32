// Converts physical encoder/button state into high-level input events for the app.
#pragma once

#include <Arduino.h>
#include <RotaryEncoder.h>

#include "Config.h"
#include "DebouncedButton.h"

struct InputEvents {
  // Positive steps mean clockwise rotation should increase volume.
  int encoderSteps = 0;
  bool encoderPress = false;
  bool encoderRelease = false;
  bool encoderHeld = false;
  bool encoderClick = false;
  bool encoderDoubleClick = false;
  bool encoderLongClick = false;
  bool topButtonClick = false;
  bool topButtonPress = false;
  bool topButtonDoubleClick = false;
  bool topButtonLongClick = false;
  bool topButtonHeld = false;
  bool buttonHeld = false;
  bool touched = false;
};

class InputController {
public:
  // Configures the encoder, its interrupt handlers, and both physical buttons.
  void begin();

  // Polls debounced button state plus encoder delta and returns one frame of input events.
  InputEvents poll();

  // Clears delayed click/double-click state after a button press only woke the screen.
  void clearPendingButtonActions();

  // Ignores the release-click for a top-button press already handled while the button was down.
  void consumeTopButtonPress();

private:
  // RotaryEncoder needs ISR ticks for reliable rotation detection.
  static void IRAM_ATTR encoderInterrupt();
  static InputController *activeInstance_;

  RotaryEncoder encoder_{
      Config::EncoderPinA,
      Config::EncoderPinB,
      RotaryEncoder::LatchMode::TWO03};
  DebouncedButton encoderButton_;
  DebouncedButton topButton_;
  int lastEncoderPosition_ = 0;
  bool topButtonClickPending_ = false;
  bool ignoreTopReleaseClick_ = false;
  bool lastEncoderButtonPressed_ = false;
  uint32_t topButtonClickPendingAt_ = 0;
  bool encoderClickPending_ = false;
  uint32_t encoderClickPendingAt_ = 0;

  static constexpr uint32_t TopButtonDoubleClickWindowMs = 350;
  static constexpr uint32_t EncoderDoubleClickWindowMs = 350;
};

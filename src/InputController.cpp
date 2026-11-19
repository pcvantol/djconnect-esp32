// Input polling for the rotary encoder, encoder push button, and top button.
#include "InputController.h"

InputController *InputController::activeInstance_ = nullptr;

void InputController::begin() {
  // The static ISR forwards back into the active instance because attachInterrupt cannot bind methods directly.
  activeInstance_ = this;
  encoderButton_.begin(Config::EncoderButtonPin);
  topButton_.begin(Config::BoardUserKeyPin);
  if constexpr (Config::HasRotaryEncoder) {
    attachInterrupt(digitalPinToInterrupt(Config::EncoderPinA), encoderInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Config::EncoderPinB), encoderInterrupt, CHANGE);
    lastEncoderPosition_ = encoder_.getPosition();
  }
  if constexpr (Config::HasSideRotationButtons) {
    sideRotationDownButton_.begin(Config::EncoderPinA);
    sideRotationUpButton_.begin(Config::EncoderPinB);
  }
  lastEncoderButtonPressed_ = encoderButton_.isPressed();
}

InputEvents InputController::poll() {
  // A tick in the loop complements the ISR and helps catch any missed transitions.
  if constexpr (Config::HasRotaryEncoder) {
    encoder_.tick();
  }
  encoderButton_.update();
  topButton_.update();
  if constexpr (Config::HasSideRotationButtons) {
    sideRotationDownButton_.update();
    sideRotationUpButton_.update();
  }

  InputEvents events;
  if constexpr (Config::HasRotaryEncoder) {
    const int position = encoder_.getPosition();
    if (position != lastEncoderPosition_) {
      // Earlier hardware testing showed this sign gives clockwise = volume up.
      events.encoderSteps = lastEncoderPosition_ - position;
      lastEncoderPosition_ = position;
    }
  }

  const uint32_t now = millis();
  if constexpr (Config::HasSideRotationButtons) {
    events.encoderSteps += pollSideRotationButton(
        sideRotationDownButton_,
        -1,
        sideRotationDownRepeatAt_,
        now);
    events.encoderSteps += pollSideRotationButton(
        sideRotationUpButton_,
        1,
        sideRotationUpRepeatAt_,
        now);
  }

  const bool encoderPressedNow = encoderButton_.isPressed();
  events.encoderPress = encoderButton_.wasPressed();
  events.encoderRelease = lastEncoderButtonPressed_ && !encoderPressedNow;
  events.encoderHeld = encoderPressedNow;
  lastEncoderButtonPressed_ = encoderPressedNow;
  const bool encoderClicked = encoderButton_.wasClicked();
  if (encoderClickPending_ && now - encoderClickPendingAt_ > EncoderDoubleClickWindowMs) {
    encoderClickPending_ = false;
    events.encoderClick = true;
  }
  if (encoderClicked) {
    if (encoderClickPending_ && now - encoderClickPendingAt_ <= EncoderDoubleClickWindowMs) {
      encoderClickPending_ = false;
      events.encoderDoubleClick = true;
    } else {
      encoderClickPending_ = true;
      encoderClickPendingAt_ = now;
    }
  }

  events.encoderLongClick = encoderButton_.wasLongClicked();
  if (events.encoderLongClick) {
    encoderClickPending_ = false;
  }

  events.topButtonPress = topButton_.wasPressed();
  const bool topButtonClicked = topButton_.wasClicked();
  if (topButtonClickPending_ && now - topButtonClickPendingAt_ > TopButtonDoubleClickWindowMs) {
    topButtonClickPending_ = false;
    events.topButtonClick = true;
  }

  if (topButtonClicked) {
    if (ignoreTopReleaseClick_) {
      ignoreTopReleaseClick_ = false;
      topButtonClickPending_ = false;
    } else if (topButtonClickPending_ && now - topButtonClickPendingAt_ <= TopButtonDoubleClickWindowMs) {
      topButtonClickPending_ = false;
      events.topButtonDoubleClick = true;
    } else {
      topButtonClickPending_ = true;
      topButtonClickPendingAt_ = now;
    }
  }

  events.topButtonLongClick = topButton_.wasLongClicked();
  if (events.topButtonLongClick) {
    topButtonClickPending_ = false;
  }
  events.topButtonHeld = topButton_.isPressed();
  const bool sideRotationHeld =
      Config::HasSideRotationButtons &&
      (sideRotationDownButton_.isPressed() || sideRotationUpButton_.isPressed());
  events.buttonHeld = events.encoderHeld || events.topButtonHeld || sideRotationHeld;
  // A held button also counts as activity so a long press wakes the dimmed display immediately.
  events.touched = events.encoderSteps != 0 ||
                   events.encoderPress ||
                   events.encoderRelease ||
                   events.encoderClick ||
                   events.encoderDoubleClick ||
                   events.encoderLongClick ||
                   events.topButtonPress ||
                   events.topButtonClick ||
                   events.topButtonDoubleClick ||
                   events.topButtonLongClick ||
                   events.buttonHeld;
  return events;
}

void InputController::clearPendingButtonActions() {
  topButtonClickPending_ = false;
  ignoreTopReleaseClick_ = false;
  encoderClickPending_ = false;
  topButton_.clearEvents();
  encoderButton_.clearEvents();
  sideRotationDownButton_.clearEvents();
  sideRotationUpButton_.clearEvents();
}

void InputController::consumeTopButtonPress() {
  ignoreTopReleaseClick_ = true;
  topButtonClickPending_ = false;
}

void IRAM_ATTR InputController::encoderInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->encoder_.tick();
  }
}

int InputController::pollSideRotationButton(
    DebouncedButton &button,
    int step,
    uint32_t &repeatAt,
    uint32_t now) {
  if (button.wasPressed()) {
    repeatAt = now + SideRotationInitialRepeatDelayMs;
    return step;
  }

  if (!button.isPressed()) {
    repeatAt = 0;
    return 0;
  }

  if (repeatAt != 0 && static_cast<int32_t>(now - repeatAt) >= 0) {
    repeatAt = now + SideRotationRepeatIntervalMs;
    return step;
  }

  return 0;
}

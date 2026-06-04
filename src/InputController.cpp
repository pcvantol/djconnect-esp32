// Input polling for the rotary encoder, encoder push button, and top button.
#include "InputController.h"

InputController *InputController::activeInstance_ = nullptr;

void InputController::begin() {
  // The static ISR forwards back into the active instance because attachInterrupt cannot bind methods directly.
  activeInstance_ = this;
  encoderButton_.begin(Config::EncoderButtonPin);
  topButton_.begin(Config::BoardUserKeyPin);
  attachInterrupt(digitalPinToInterrupt(Config::EncoderPinA), encoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(Config::EncoderPinB), encoderInterrupt, CHANGE);
  lastEncoderPosition_ = encoder_.getPosition();
}

InputEvents InputController::poll() {
  // A tick in the loop complements the ISR and helps catch any missed transitions.
  encoder_.tick();
  encoderButton_.update();
  topButton_.update();

  InputEvents events;
  const int position = encoder_.getPosition();
  if (position != lastEncoderPosition_) {
    // Earlier hardware testing showed this sign gives clockwise = volume up.
    events.encoderSteps = lastEncoderPosition_ - position;
    lastEncoderPosition_ = position;
  }

  const uint32_t now = millis();
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
  events.buttonHeld = encoderButton_.isPressed() || events.topButtonHeld;
  // A held button also counts as activity so a long press wakes the dimmed display immediately.
  events.touched = events.encoderSteps != 0 ||
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

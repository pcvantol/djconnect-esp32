// Minimal active-low button debouncer with one-shot short/long click events.
#pragma once

#include <Arduino.h>

class DebouncedButton {
public:
  // Configures an active-low GPIO button with the ESP32 internal pull-up enabled.
  void begin(uint8_t pinNumber) {
    pin_ = pinNumber;
    pinMode(pin_, INPUT_PULLUP);
    stablePressed_ = rawPressed();
    lastRawPressed_ = stablePressed_;
    lastRawChangedAt_ = millis();
  }

  // Updates the stable button state; call this often from the main loop.
  void update() {
    const bool raw = rawPressed();
    const uint32_t now = millis();

    if (raw != lastRawPressed_) {
      lastRawPressed_ = raw;
      lastRawChangedAt_ = now;
    }

    if (now - lastRawChangedAt_ < DebounceMs || raw == stablePressed_) {
      return;
    }

    stablePressed_ = raw;
    if (stablePressed_) {
      pressedAt_ = now;
      longClickFiredForPress_ = false;
      return;
    }

    const uint32_t heldMs = now - pressedAt_;
    if (heldMs >= LongClickMs) {
      if (!longClickFiredForPress_) {
        longClickEvent_ = true;
      }
    } else if (heldMs >= ShortClickMinMs) {
      clickEvent_ = true;
    }

    longClickFiredForPress_ = false;
  }

  // Emits the long-click event as soon as the hold threshold is crossed, while the button is still down.
  void updateHeldLongClick() {
    if (!stablePressed_ || longClickFiredForPress_) {
      return;
    }

    if (millis() - pressedAt_ >= LongClickMs) {
      longClickEvent_ = true;
      longClickFiredForPress_ = true;
    }
  }

  // Returns true once per completed short click.
  bool wasClicked() {
    if (!clickEvent_) {
      return false;
    }
    clickEvent_ = false;
    return true;
  }

  // Returns true once per completed long click.
  bool wasLongClicked() {
    updateHeldLongClick();
    if (!longClickEvent_) {
      return false;
    }
    longClickEvent_ = false;
    return true;
  }

  // Exposes the debounced pressed state for "wake on touch" behavior.
  bool isPressed() const {
    return stablePressed_;
  }

  // Drops queued click events when a press was consumed by wake-only display behavior.
  void clearEvents() {
    clickEvent_ = false;
    longClickEvent_ = false;
  }

private:
  // Buttons on this board connect the pin to ground when pressed.
  bool rawPressed() const {
    return digitalRead(pin_) == LOW;
  }

  // These thresholds keep accidental switch bounce from becoming Spotify commands.
  static constexpr uint32_t DebounceMs = 35;
  static constexpr uint32_t ShortClickMinMs = 40;
  static constexpr uint32_t LongClickMs = 1500;

  uint8_t pin_ = 255;
  bool stablePressed_ = false;
  bool lastRawPressed_ = false;
  bool clickEvent_ = false;
  bool longClickEvent_ = false;
  bool longClickFiredForPress_ = false;
  uint32_t lastRawChangedAt_ = 0;
  uint32_t pressedAt_ = 0;
};

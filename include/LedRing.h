// Drives the on-board WS2812 ring as a green volume meter.
#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "Config.h"

class LedRing {
public:
  // Initializes the 8 on-board WS2812 LEDs on IO14.
  void begin();

  // Shows volume as green segments around the ring; partially lit LEDs represent partial steps.
  void showVolume(int volume, bool force = false);

  // Shows all LEDs as one solid color, used for setup/provisioning states.
  void showSolid(const CRGB &color, uint8_t brightnessPercent = 100);

  // Plays one short Spotify-green chase around the ring and fades back to off during normal boot.
  void playBootBounce();

  // Advances the low-battery charging animation from red through yellow toward green.
  void showChargingAnimation();

  // Advances the AP/setup portal rainbow breathing animation.
  void showSetupRainbowBreath();

  // Matches the ring brightness to the display power state; 0 fully hides the ring.
  void setPowerPercent(uint8_t percent);

  // Reports whether FastLED is currently allowed to show visible output.
  bool isOn() const;

private:
  // FastLED owns timing-sensitive output, so the app only sends coarse volume updates here.
  CRGB leds_[Config::Ws2812LedCount];
  bool ready_ = false;
  int lastVolume_ = -999;
  uint8_t powerPercent_ = 100;
  uint32_t lastChargingFrameAt_ = 0;
  uint8_t chargingFrame_ = 0;
  uint32_t lastSetupFrameAt_ = 0;
  uint8_t setupFrame_ = 0;
};

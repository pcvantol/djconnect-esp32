// WS2812 LED ring output.
#include "LedRing.h"

#include "LogicHelpers.h"

namespace {
constexpr uint8_t SpotifyGreenRed = 0x1D;
constexpr uint8_t SpotifyGreenGreen = 0xB9;
constexpr uint8_t SpotifyGreenBlue = 0x54;
constexpr uint8_t VolumeOrangeRed = 0xFF;
constexpr uint8_t VolumeOrangeGreen = 0x8A;
constexpr uint8_t VolumeOrangeBlue = 0x00;

CRGB scaledSpotifyGreen(uint8_t level) {
  return CRGB(
      (SpotifyGreenRed * level) / 255,
      (SpotifyGreenGreen * level) / 255,
      (SpotifyGreenBlue * level) / 255);
}

CRGB scaledVolumeOrange(uint8_t level) {
  return CRGB(
      (VolumeOrangeRed * level) / 255,
      (VolumeOrangeGreen * level) / 255,
      (VolumeOrangeBlue * level) / 255);
}
}  // namespace

void LedRing::begin() {
  FastLED.addLeds<WS2812, Config::Ws2812DataPin, GRB>(leds_, Config::Ws2812LedCount);
  FastLED.setBrightness(Config::LedRingBrightness);
  ready_ = true;
  showVolume(-1, true);
}

void LedRing::showVolume(int volume, bool force) {
  if (!ready_) {
    return;
  }
  if (!force && volume == lastVolume_) {
    return;
  }

  lastVolume_ = volume;
  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);

  if (volume >= 0) {
    // Scale across 8 LEDs with fractional brightness on the last active segment.
    for (uint8_t index = 0; index < Config::Ws2812LedCount; index++) {
      const int level = Logic::ledSegmentBrightness(
          volume,
          index,
          Config::Ws2812LedCount,
          Config::MaxSpotifyVolumePercent);
      leds_[index] = scaledVolumeOrange(level);
    }
  }

  FastLED.show();
}

void LedRing::showPongPaddle(int paddleY) {
  showGamePosition(paddleY, 42, 126, CRGB(255, 138, 0));
}

void LedRing::showGamePosition(int position, int minPosition, int maxPosition, const CRGB &color) {
  if (!ready_) {
    return;
  }

  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);
  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);

  if (maxPosition <= minPosition) {
    leds_[0] = color;
    FastLED.show();
    return;
  }

  const int clampedPosition = constrain(position, minPosition, maxPosition);
  const uint8_t head = map(clampedPosition, minPosition, maxPosition, 0, Config::Ws2812LedCount - 1);
  CRGB tail = color;
  tail.nscale8_video(96);
  leds_[head] = color;
  leds_[(head + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = tail;
  leds_[(head + 1) % Config::Ws2812LedCount] = tail;
  FastLED.show();
}

void LedRing::showSolid(const CRGB &color, uint8_t brightnessPercent) {
  if (!ready_) {
    return;
  }

  powerPercent_ = constrain(brightnessPercent, 0, 100);
  FastLED.setBrightness(map(powerPercent_, 0, 100, 0, 255));
  fill_solid(leds_, Config::Ws2812LedCount, color);
  lastVolume_ = -999;
  FastLED.show();
}

void LedRing::clear() {
  powerPercent_ = 0;
  lastVolume_ = -999;
  if (!ready_) {
    return;
  }
  FastLED.setBrightness(Config::LedRingBrightness);
  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  FastLED.show();
  FastLED.setBrightness(0);
}

void LedRing::playPulse(const CRGB &color) {
  if (!ready_) {
    return;
  }

  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(160);
  for (uint8_t index = 0; index < Config::Ws2812LedCount; index++) {
    fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
    leds_[index] = color;
    leds_[(index + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = color;
    leds_[(index + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount].nscale8(80);
    FastLED.show();
    delay(28);
  }
  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  FastLED.show();
}

void LedRing::playStartupRainbow() {
  if (!ready_) {
    return;
  }

  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  // One calm rainbow lap gives a clear startup cue before WiFi/setup/status animations take over.
  for (uint8_t index = 0; index < Config::Ws2812LedCount; index++) {
    fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
    const uint8_t hue = index * (255 / Config::Ws2812LedCount);
    leds_[index] = CHSV(hue, 255, 255);
    leds_[(index + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = CHSV(hue - 28, 220, 105);
    leds_[(index + Config::Ws2812LedCount - 2) % Config::Ws2812LedCount] = CHSV(hue - 56, 180, 45);
    FastLED.show();
    delay(85);
  }

  // Fade/debounce the ring back to fully off so the later connection/setup state owns the LEDs.
  for (int brightness = 160; brightness >= 0; brightness -= 10) {
    FastLED.setBrightness(brightness < 0 ? 0 : brightness);
    FastLED.show();
    delay(28);
  }
  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  FastLED.setBrightness(0);
  FastLED.show();
  powerPercent_ = 0;
}

void LedRing::playTurnOffRainbow() {
  if (!ready_) {
    return;
  }

  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  for (uint8_t frame = 0; frame < 18; frame++) {
    fill_rainbow(leds_, Config::Ws2812LedCount, frame * 12, 24);
    const uint8_t brightness = map(17 - frame, 0, 17, 0, Config::LedRingBrightness);
    FastLED.setBrightness(brightness);
    FastLED.show();
    delay(38);
  }

  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  FastLED.setBrightness(0);
  FastLED.show();
  powerPercent_ = 0;
}

void LedRing::showWifiConnectingAnimation() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiFrameAt_ < 45) {
    return;
  }
  lastWifiFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  const uint8_t head = wifiFrame_++ % Config::Ws2812LedCount;
  leds_[head] = scaledSpotifyGreen(255);
  leds_[(head + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = scaledSpotifyGreen(110);
  leds_[(head + Config::Ws2812LedCount - 2) % Config::Ws2812LedCount] = scaledSpotifyGreen(40);
  FastLED.show();
}

void LedRing::showWifiTestingAnimation() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiTestingFrameAt_ < 45) {
    return;
  }
  lastWifiTestingFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  const uint8_t head = wifiTestingFrame_++ % Config::Ws2812LedCount;
  leds_[head] = CRGB(255, 180, 0);
  leds_[(head + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = CRGB(90, 170, 255);
  leds_[(head + Config::Ws2812LedCount - 2) % Config::Ws2812LedCount] = CRGB(35, 80, 135);
  FastLED.show();
}

void LedRing::showChargingAnimation() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastChargingFrameAt_ < 120) {
    return;
  }
  lastChargingFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;

  const uint8_t phase = chargingFrame_++ % 24;
  CRGB color;
  if (phase < 8) {
    color = CRGB(255, phase * 16, 0);
  } else if (phase < 16) {
    color = CRGB(255 - ((phase - 8) * 12), 128 + ((phase - 8) * 12), 0);
  } else {
    color = CRGB(159 - ((phase - 16) * 16), 224, (phase - 16) * 6);
  }

  FastLED.setBrightness(Config::LedRingBrightness);
  fill_solid(leds_, Config::Ws2812LedCount, color);
  FastLED.show();
}

void LedRing::showSetupRainbowBreath() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastSetupFrameAt_ < 35) {
    return;
  }
  lastSetupFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;

  const uint8_t breath = beatsin8(18, 3, Config::LedRingBrightness);
  FastLED.setBrightness(breath);
  fill_rainbow(leds_, Config::Ws2812LedCount, setupFrame_++, 18);
  FastLED.show();
}

void LedRing::showHomeAssistantPairingBreath() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastHomeAssistantPairingFrameAt_ < 35) {
    return;
  }
  lastHomeAssistantPairingFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;

  const uint8_t breath = beatsin8(16, 3, Config::LedRingBrightness);
  FastLED.setBrightness(breath);
  fill_solid(leds_, Config::Ws2812LedCount, CRGB(0, 70, 255));
  FastLED.show();
}

void LedRing::showFirmwareUpdateAnimation() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastFirmwareFrameAt_ < 28) {
    return;
  }
  lastFirmwareFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  const uint8_t head = firmwareFrame_++ % Config::Ws2812LedCount;
  leds_[head] = CRGB::Purple;
  leds_[(head + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = CRGB(80, 0, 110);
  leds_[(head + Config::Ws2812LedCount - 2) % Config::Ws2812LedCount] = CRGB(30, 0, 50);
  FastLED.show();
}

void LedRing::showDjResponseAnimation() {
  if (!ready_) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastDjResponseFrameAt_ < 45) {
    return;
  }
  lastDjResponseFrameAt_ = now;
  powerPercent_ = 100;
  lastVolume_ = -999;
  FastLED.setBrightness(Config::LedRingBrightness);

  fill_solid(leds_, Config::Ws2812LedCount, CRGB::Black);
  const uint8_t head = djResponseFrame_++ % Config::Ws2812LedCount;
  leds_[head] = scaledSpotifyGreen(255);
  leds_[(head + Config::Ws2812LedCount - 1) % Config::Ws2812LedCount] = scaledSpotifyGreen(110);
  leds_[(head + Config::Ws2812LedCount - 2) % Config::Ws2812LedCount] = scaledSpotifyGreen(40);
  FastLED.show();
}

void LedRing::setPowerPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  if (percent == powerPercent_) {
    return;
  }

  powerPercent_ = percent;
  if (!ready_) {
    return;
  }

  const uint8_t brightness = (Config::LedRingBrightness * powerPercent_) / 100;
  FastLED.setBrightness(brightness);
  FastLED.show();
}

bool LedRing::isOn() const {
  return ready_ && powerPercent_ > 0;
}

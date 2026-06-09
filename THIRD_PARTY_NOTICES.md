# Third-Party Notices

DJConnect firmware is proprietary software. This file lists third-party software
components used by the firmware build. Their licenses and copyrights remain with
their respective authors.

This notice is provided for attribution and license-awareness purposes. It does
not grant additional rights to the DJConnect firmware itself.

## Firmware Frameworks And Libraries

- Arduino core for ESP32 / ESP32-S3
  - Used for the firmware runtime, WiFi, BLE, WebServer, Preferences, LittleFS,
    Update, ESPmDNS and related ESP32 Arduino APIs.
  - Project/package: `framework-arduinoespressif32`
  - Build platform: pinned pioarduino ESP32 platform with ESP-IDF 5.3 and
    Arduino ESP32 3.x support.

- TFT_eSPI
  - Used for ST7789 display rendering.
  - PlatformIO dependency: `bodmer/TFT_eSPI`

- RotaryEncoder
  - Used for rotary encoder input handling.
  - PlatformIO dependency: `mathertel/RotaryEncoder`

- ArduinoJson
  - Used for JSON parsing and serialization.
  - PlatformIO dependency: `bblanchon/ArduinoJson`

- FastLED
  - Used for WS2812 LED-ring control.
  - PlatformIO dependency: `fastled/FastLED`

- TJpg_Decoder
  - Used for JPEG album art rendering.
  - PlatformIO dependency: `bodmer/TJpg_Decoder`

## Firmware Model Assets

- ESPHome micro-wake-word-models: `Okay Nabu`
  - Used as the bundled wake-word model asset for local wake-word detection.
  - Source: `https://github.com/esphome/micro-wake-word-models`
  - Vendored files: `third_party/micro_wake_word/okay_nabu.json`,
    `third_party/micro_wake_word/okay_nabu.tflite`
  - Model SHA256:
    `0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891`
  - License: Apache License 2.0. See
    `third_party/micro_wake_word/LICENSE`.

- MicroTFLite / TensorFlow Lite Micro
  - Used for local `Okay Nabu` wake-word inference and the TensorFlow
    micro_speech frontend feature extractor.
  - PlatformIO dependency: `johnosbb/MicroTFLite`
  - Includes TensorFlow Lite Micro and TensorFlow microfrontend components.
  - License: Apache License 2.0.

## Platform And Tooling

- PlatformIO
  - Used for firmware dependency management and builds.

- Espressif ESP32 toolchains and flashing tools
  - Used by PlatformIO for compilation and firmware image generation.

## Spotify Trademark Notice

Spotify is a trademark of Spotify AB. DJConnect is not affiliated with,
endorsed by, or sponsored by Spotify AB.

Spotify Web API usage does not imply endorsement, sponsorship or certification
by Spotify AB.

## Home Assistant Notice

DJConnect can integrate with Home Assistant through a custom integration and
local device APIs. Home Assistant and related trademarks/projects belong to
their respective owners. DJConnect is not affiliated with or endorsed by Home
Assistant unless explicitly stated by those owners.

## Maintenance

When firmware dependencies are added, removed or materially changed, update this
file together with `platformio.ini`.

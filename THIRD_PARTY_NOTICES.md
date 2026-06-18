# Third-Party Notices

DJConnect firmware is MIT-licensed open-source software. This file lists
third-party software components used by the firmware build. Their licenses and
copyrights remain with their respective authors.

This notice is provided for attribution and license-awareness purposes. It does
not replace the third-party license texts.

## Firmware Frameworks And Libraries

The complete dependency inventory with versions, license model, source URL and
use in the firmware is maintained in `DESIGN_DECISIONS.md`. This file keeps the
human-readable third-party notice summary and trademark notices.

- Arduino core for ESP32 / ESP32-S3
  - Used for the firmware runtime, WiFi, BLE, WebServer, Preferences, LittleFS,
    Update, ESPmDNS and related ESP32 Arduino APIs.
  - Project/package: `framework-arduinoespressif32`
  - Build platform: pinned pioarduino ESP32 platform with ESP-IDF 5.3 and
    Arduino ESP32 3.x support.

- TFT_eSPI
  - Used for ST7789 display rendering.
  - PlatformIO dependency: `bodmer/TFT_eSPI`
  - Installed version: `2.5.43`.
  - Source: `https://github.com/Bodmer/TFT_eSPI`
  - License: MIT-derived notices in the package `license.txt`.

- RotaryEncoder
  - Used for rotary encoder input handling.
  - PlatformIO dependency: `mathertel/RotaryEncoder`
  - Installed version: `1.6.0`.
  - Source: `http://www.mathertel.de/Arduino/RotaryEncoderLibrary.aspx`
  - License: BSD-style.

- ArduinoJson
  - Used for JSON parsing and serialization.
  - PlatformIO dependency: `bblanchon/ArduinoJson`
  - Installed version: `7.4.3`.
  - Source: `https://github.com/bblanchon/ArduinoJson`
  - License: MIT.

- FastLED
  - Used for WS2812 LED-ring control.
  - PlatformIO dependency: `fastled/FastLED`
  - Installed version: `3.10.3`.
  - Source: `https://github.com/FastLED/FastLED`
  - License: MIT.

- TJpg_Decoder
  - Used for JPEG album art rendering.
  - PlatformIO dependency: `bodmer/TJpg_Decoder`
  - Installed version: `1.1.0`.
  - Source: `https://github.com/Bodmer/TJpg_Decoder`
  - License: FreeBSD-style for wrapper plus Tiny JPEG Decompressor terms.

- ESP8266Audio
  - Used for WAV/MP3 playback helpers over I2S.
  - PlatformIO dependency: `earlephilhower/ESP8266Audio`
  - Installed version: `1.9.9`.
  - Source: `https://github.com/earlephilhower/ESP8266Audio`
  - License: GPL-3.0 according to included `LICENSE`.

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
  - Installed version: `1.0.4`.
  - Source: `https://github.com/johnosbb/MicroTFLite`
  - License: Apache License 2.0.

Ignored transitive libraries downloaded because of `MicroTFLite` metadata are
`ArduinoBLE` 2.0.2, `Arduino_LSM9DS1` 1.1.1 and `Arduino_SpiNINA` 0.0.2. They
are excluded by `lib_ignore` and are not used by the firmware runtime.

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

Firmware release builds run `scripts/update_build_dependencies.sh` before
compilation. Review the generated `build-dependencies.diff`; when framework,
library or tool versions are added, removed or upgraded, update this file
together with `platformio.ini` and `DESIGN_DECISIONS.md` before publishing.

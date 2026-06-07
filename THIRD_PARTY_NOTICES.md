# Third-Party Notices

SpotifyDJ firmware is proprietary software. This file lists third-party software
components used by the firmware build. Their licenses and copyrights remain with
their respective authors.

This notice is provided for attribution and license-awareness purposes. It does
not grant additional rights to the SpotifyDJ firmware itself.

## Firmware Frameworks And Libraries

- Arduino core for ESP32 / ESP32-S3
  - Used for the firmware runtime, WiFi, BLE, WebServer, Preferences, LittleFS,
    Update, ESPmDNS and related ESP32 Arduino APIs.
  - Project/package: `framework-arduinoespressif32`

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

## Platform And Tooling

- PlatformIO
  - Used for firmware dependency management and builds.

- Espressif ESP32 toolchains and flashing tools
  - Used by PlatformIO for compilation and firmware image generation.

## Spotify Trademark Notice

Spotify is a trademark of Spotify AB. SpotifyDJ is not affiliated with,
endorsed by, or sponsored by Spotify AB.

Spotify Web API usage does not imply endorsement, sponsorship or certification
by Spotify AB.

## Home Assistant Notice

SpotifyDJ can integrate with Home Assistant through a custom integration and
local device APIs. Home Assistant and related trademarks/projects belong to
their respective owners. SpotifyDJ is not affiliated with or endorsed by Home
Assistant unless explicitly stated by those owners.

## Maintenance

When firmware dependencies are added, removed or materially changed, update this
file together with `platformio.ini`.

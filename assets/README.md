# DJConnect asset pack

Generated assets for the DJConnect Home Assistant integration, ESP32-S3/LILYGO display firmware, and responsive retina website/PWA.

## Home Assistant integration
Use files from `ha_integration/` in or beside `custom_components/djconnect/` as needed:

- `icon.png` — 256x256
- `icon@2x.png` — 512x512
- `logo.png` — 512x512

## Website / PWA
Use `website/` as your `/icons/` folder. Include the contents of `html-head-snippet.html` in your page `<head>`.

## ESP display
For LILYGO T-Embed style displays:

- `djconnect_icon_170x170.png` — full-width square icon
- `djconnect_splash_170x320.png` — portrait splash for 170x320 display
- `embedded/*.h` — C/C++ PROGMEM RGB565 big-endian byte arrays
- `embedded/*.rgb565` — raw RGB565 big-endian binaries

Example TFT usage depends on your display library. If your driver expects uint16_t pixels rather than bytes, convert the byte pairs to uint16_t or adapt the byte order.

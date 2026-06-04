# AGENTS.md

Guidance for coding agents working on the SpotifyDJ LilyGO firmware.

## Project Overview

SpotifyDJ is an Arduino/PlatformIO firmware for the LilyGO T-Embed-CC1101 / ESP32-S3. It is a Spotify Connect remote with:

- TFT now-playing UI, menus, logs, album art, queue and sound output screens.
- Rotary encoder and top-button controls.
- Battery/charging guards, deep sleep and reset handling.
- WS2812 LED ring feedback.
- Local mobile web portal.
- MQTT/Home Assistant state publishing.
- Home Assistant device-layer support: pairing, mDNS discovery, device API, status posting and OTA trigger endpoints.

The current PlatformIO environment is `t_embed_cc1101`.

## Important Paths

- `platformio.ini`: PlatformIO board/env/build flags.
- `src/main.cpp`: tiny Arduino entrypoint; keep real behavior in classes.
- `include/SpotifyDJApp.h`, `src/SpotifyDJApp.cpp`: top-level app orchestration.
- `include/Config.h`: shared constants, version, pins and timing.
- `include/AppState.h`: shared state structs.
- `include/LogicHelpers.h`: pure helper functions with native unit tests.
- `test/native/test_logic.cpp`: host-side unit tests.
- `include/Secrets.h`: optional non-secret build flags only. Do not put WiFi or Spotify credentials here.
- `include/Secrets.example.h`: template for optional build flags.
- `src/SpotifyDJDevice.*`, `src/SpotifyDJDiscovery.*`, `src/SpotifyDJPairing.*`, `src/SpotifyDJApiServer.*`, `src/SpotifyDJOTA.*`: Home Assistant device-layer modules.

## Build And Test Commands

Run native tests first when changing pure logic:

```sh
c++ -std=c++17 -Iinclude test/native/test_logic.cpp -o /tmp/spotify_remote_unit_tests
/tmp/spotify_remote_unit_tests
```

Build firmware:

```sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Expected recurring third-party warning:

- `esp32-hal-uart.c: warning: 'return' with no value...`

This warning comes from the Arduino ESP32 framework package and is not normally actionable in this project.

## Architecture Rules

Keep concerns separated:

- `SpotifyDJApp` orchestrates modules and owns the main loop flow.
- `SpotifyClient` owns Spotify Web API auth/playback/control calls.
- `DisplayManager` owns drawing only. Do not put network or NVS logic in it.
- `WebPortal` owns the existing mobile dashboard and shared port-80 `WebServer`.
- Home Assistant device-layer code belongs in the `SpotifyDJ*` modules under `src/`.
- `LogicHelpers.h` is for pure, host-testable calculations.

Prefer extending existing modules over introducing new global state. Keep `src/main.cpp` small.

## Home Assistant Device Layer

The HA custom integration uses:

- Domain: `spotify_dj`
- HA pairing endpoint: `POST /api/spotify_dj/pair`
- HA status endpoint: `POST /api/spotify_dj/status`
- HA voice endpoint: `POST /api/spotify_dj/voice`
- ESP OTA endpoint: `POST /api/device/ota`
- ESP Spotify provisioning endpoint: `POST /api/device/provision_spotify`

Local ESP endpoints currently include:

- `GET /api/device/info`
- `GET /api/device/pairing-info`
- `POST /api/device/pair`
- `POST /api/device/provision_spotify`
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`

The local device API registers routes on the existing `WebPortal` `WebServer` instance. Do not start a second port-80 server.

mDNS:

- Hostname: `spotifydj-XXXXXXXXXXXX`
- Service: `_spotifydj._tcp`
- URL: `http://spotifydj-XXXXXXXXXXXX.local`
- TXT records include `name`, `device_id`, `version`, `paired`, `api`, and `model`.

## Secrets And Credential Policy

Do not compile user secrets into firmware.

Forbidden in firmware source:

- WiFi SSID
- WiFi password
- Spotify client id
- Spotify refresh token
- Spotify client secret
- Home Assistant device token

Credentials are provisioned and stored in NVS.

NVS namespaces:

- `provision`: existing device provisioning/settings.
- `spotify`: rotated Spotify token cache.
- `spotifydj`: Home Assistant device-layer storage.

`spotifydj` keys:

- `ha_url`
- `device_token`
- `device_name`
- `spotify_client_id`
- `spotify_refresh_token`
- `spotify_market`
- `firmware_channel`

Never log:

- Spotify refresh token
- HA device token
- WiFi password
- Spotify client secret

Do not introduce a Spotify client secret. The firmware should use PKCE/public-client style credentials only.

## Pairing Mode Rules

When WiFi is configured but Home Assistant is not paired:

- Show pairing code on the display.
- Keep display brightness at 100%.
- Keep pairing mode active for 10 minutes.
- After 10 minutes, enter deep sleep.
- Keep local web/API handling alive.
- Do not process normal playback/menu button actions.
- Soft reset and hard reset must remain available through the reset monitor.

If WiFi is not configured, the device starts in setup/AP provisioning mode before HA pairing can happen.

## Display And UI Rules

Do not redesign the UI casually. Changes should be targeted.

Current statusbar badges:

- `H`: Home Assistant paired
- `M`: MQTT connected
- `S`: Spotify authorized

Green means OK; red means not OK.

Brightness/power behavior is intentionally nuanced:

- Normal idle dim/off policy is handled by `DisplayManager` and `SpotifyDJApp::updateVisualPower()`.
- Setup/AP and HA pairing modes force their own brightness behavior.
- Low-battery and charging guards override normal UI behavior.

## OTA Rules

OTA uses the `default_16MB.csv` partition table, which includes `ota_0` and `ota_1`.

Device OTA endpoint:

- `POST /api/device/ota`
- Requires `Authorization: Bearer <device_token>`.
- Expects JSON with `url`, `sha256`, `version`, and `device`.
- `device` must be `lilygo-t-embed-s3`.
- Battery must be above 40%, or charging/full, if battery state is available.

Current OTA implementation streams via `Update.h`.

SHA256 verification is currently marked as TODO/log warning if a hash is supplied. Do not claim cryptographic validation is complete until implemented.

## MQTT Rules

MQTT is separate from the HA device API. Do not conflate the two.

MQTT publishes retained state and Home Assistant discovery for dashboard status. HA pairing/status uses HTTP endpoints and device token auth.

MQTT connection failures should not block Spotify controls or local web UI.

## Volume Rules

Spotify volume is intentionally capped:

- User-facing volume range: `0` to `60`.
- Encoder volume commands clamp to `Config::MaxSpotifyVolumePercent`.
- Web slider max is `60`.
- LED ring treats `60` as full.

Do not restore `0-100` behavior unless the user explicitly asks.

## Battery And Sleep Rules

Respect existing low-battery behavior:

- Warning sound when battery crosses from above 20% to 20%.
- Below 20%: show charge screen and block normal inputs.
- Below 10%: critical charge flow and deep sleep.
- Charging guard blocks normal WiFi/Spotify behavior until battery recovery threshold.

Hard reset is allowed only above the configured battery threshold. Soft reset has its own lower threshold. Check `SoftResetMonitor` before changing reset behavior.

## Coding Style

- Use `rg` for search.
- Use `apply_patch` for manual edits.
- Keep changes scoped.
- Prefer existing classes and patterns.
- Keep comments useful and concise.
- Avoid logging secrets.
- Use Arduino/ESP32 style APIs already present in the project.
- Do not rewrite Spotify/audio/display pipelines when implementing HA-device features.
- Do not add unrelated refactors.

## Verification Checklist

Before finalizing firmware changes:

1. Run native tests when logic helpers changed.
2. Run PlatformIO build.
3. Scan for accidental secrets when credential/provisioning code changed:

```sh
rg -n "SPOTIFY_CLIENT_ID|SPOTIFY_REFRESH_TOKEN|WIFI_SSID|WIFI_PASSWORD|client_secret|wifi144iot|verbindmet|AQB|5ea462" include src test -S
```

4. Mention any remaining known TODOs, especially OTA SHA256 verification.


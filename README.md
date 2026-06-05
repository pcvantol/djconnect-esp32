# SpotifyDJ Firmware for LilyGO T-Embed-CC1101

SpotifyDJ is proprietary ESP32-S3 firmware for the LilyGO T-Embed-CC1101. The device is a Spotify Connect remote with a display, rotary encoder, top button, LED ring, web portal, MQTT status/control and Home Assistant device integration.

SpotifyDJ is not a Spotify Connect speaker/player. It controls an existing Spotify Connect playback target through the Spotify Web API, such as a phone, computer, receiver, speaker or other Spotify Connect output.

## Features

- Shows the active Spotify Connect output.
- Shows current track/podcast, artist/show, duration and progress.
- Scrolls long title and artist/show text once when the track changes.
- Volume control through the encoder, web portal and MQTT, limited to `0-60`.
- Spotify play mode from device settings and the web portal: normal, shuffle, repeat once or repeat infinite.
- Language setting for the device UI, web portal and captive portal: English or Dutch.
- Theme setting for the device and web portal: Auto, Dark or Light.
- Playlist browser on the device and web portal to start playlists directly.
- Current Song screen with album art download/cache.
- Encoder short press on Now Playing: pause/resume.
- Encoder double press on Now Playing: Current Song / album art screen, only when playback is active.
- Encoder long press: push-to-talk through Home Assistant Assist; release stops listening.
- Top button short press: back in menus, otherwise next track.
- Top button double press: previous track.
- Top button long press: open the menu.
- Top button held for 10 seconds: restart/soft reset.
- Encoder button + top button held for 10 seconds: factory reset when battery state allows it.
- Menus for Up Next, Playlists, Sound Outputs, Settings, About and Logs.
- Mobile web portal with Now Playing, browser push-to-talk, album art, volume slider, outputs, queue, logs, diagnostics, settings, WiFi update and OTA upload.
- Home Assistant pairing with mDNS discovery and device-token authentication.
- BLE WiFi provisioning in setup mode for apps/flows that actively write credentials to the device.
- MQTT/Home Assistant discovery, periodic status publishing and two-way commands.
- Battery/charging guards, turn-off sleep and low-battery screens.
- Charger-aware wake probe while turned off.
- WiFi-failure boot menu with retry, factory reset, restart device and turn off.
- OTA endpoint for Home Assistant-triggered firmware updates.
- Watchdog, slow-loop logging and periodic heap diagnostics.

## Hardware

- LilyGO T-Embed-CC1101.
- ESP32-S3 PlatformIO target.
- ST7789 display.
- Rotary encoder with center button.
- Top button / board user key.
- BQ27220 battery gauge.
- WS2812 LED ring.
- Built-in speaker cues and microphone for push-to-talk.

## Important Limitations

- Spotify Premium is required for Spotify Web API playback control endpoints.
- A Spotify Connect playback context or available Spotify Connect output is required.
- Some Spotify Connect outputs do not support volume control through the Web API.
- OTA through `/api/device/ota` requires HTTPS and verifies the streamed firmware against the manifest SHA256 before rebooting.
- Web portal DJ-response test runs through the ESP and Home Assistant pairing; it does not require browser microphone access.

## License

SpotifyDJ firmware is closed-source proprietary firmware. See [LICENSE](LICENSE).

The license is intended for commercial devices that ship with SpotifyDJ firmware preinstalled. End users may use the firmware on the purchased device. The firmware, source code and firmware binaries may not be copied, modified, reverse engineered, extracted or redistributed without permission. The Home Assistant integration may be provided to customers for free, but it does not grant rights to the firmware.

## Secrets and Provisioning

WiFi, Spotify and Home Assistant secrets are not compiled into firmware.

Do not place these in firmware headers:

- WiFi SSID
- WiFi password
- Spotify client ID
- Spotify refresh token
- Spotify client secret
- Home Assistant device token
- MQTT password

Credentials are provisioned through setup flows and stored in NVS.

`include/Secrets.h` is only for optional build-time flags:

```cpp
#pragma once

// No WiFi or Spotify credentials live in firmware. Provision them via the setup portal.
#define SPOTIFY_MARKET "NL"
#define SPOTIFY_ALLOW_INSECURE_TLS 0
```

Keep `SPOTIFY_ALLOW_INSECURE_TLS` at `0` for normal builds. Set it to `1` only as a temporary local troubleshooting fallback.

## First Setup

1. Flash the firmware.
2. If no WiFi credentials are stored in NVS, the device starts setup/AP mode.
3. Connect to the WiFi network `SpotifyDJ Setup`.
4. Open the captive portal or browse to the AP IP address.
5. Enter WiFi credentials and, optionally, Spotify and MQTT settings.
6. The device tests WiFi and optional Spotify credentials, stores them in NVS and restarts.

Setup/AP mode keeps the screen at 100% brightness, shows the rainbow LED-ring animation, keeps battery/charging state visible and turns off after 10 minutes without successful setup.

## BLE WiFi Provisioning

iOS does not automatically share the current iPhone WiFi password with an ESP32 over BLE. SpotifyDJ supports BLE provisioning where an app, Home Assistant flow or BLE tool actively writes credentials to the device.

During setup/AP mode:

- BLE name: `SpotifyDJ xxxx`, where `xxxx` is the device ID suffix.
- Service UUID: `7f705000-9f8f-4f1a-9b5f-570071fd0001`
- Write characteristic UUID: `7f705001-9f8f-4f1a-9b5f-570071fd0001`
- Status read/notify characteristic UUID: `7f705002-9f8f-4f1a-9b5f-570071fd0001`

Write WiFi credentials as JSON:

```json
{
  "ssid": "MyWiFi",
  "password": "wifi-password"
}
```

Spotify and MQTT are not provisioned through BLE; use Home Assistant provisioning, the captive portal or web settings for those.

## Spotify App and Refresh Token

Create a Spotify app at `https://developer.spotify.com/dashboard` and add this redirect URI:

```text
http://127.0.0.1:8888/callback
```

Generate a refresh token with PKCE. Only the client ID is needed; do not use a client secret on the ESP32.

```bash
python3 scripts/spotify_pkce_refresh_token.py --client-id YOUR_SPOTIFY_CLIENT_ID
```

The script prints the client ID and refresh token for provisioning through the setup portal or Home Assistant pairing flow. Do not commit those values and do not compile them into firmware headers.

The generated token includes `playlist-read-private` so the firmware can find a private `SpotifyDJ Liked Proxy` playlist from your own playlists. If an older token was generated without that scope, regenerate the refresh token and submit it through Home Assistant provisioning or the web portal Spotify repair form.

## Home Assistant Integration

The custom integration domain is `spotify_dj`.

When WiFi is configured but Home Assistant is not paired, the device enters pairing mode. The display shows the SpotifyDJ logo/name, battery state and a large pairing code. The screen stays at 100% brightness for 10 minutes. Normal playback/menu input is blocked, while reset controls, the web portal and the device API remain available.

### mDNS Discovery

After WiFi connect, the device advertises `_spotifydj._tcp`.

Hostname format:

```text
spotifydj-lilygo-XXXXXXXXXXXX
```

Browsable URL:

```text
http://spotifydj-lilygo-XXXXXXXXXXXX.local
```

TXT records include `name`, `device_id`, `version`, `paired`, `api` and `model`.

### Local Device API

Open endpoints:

- `GET /api/device/info`
- `GET /api/device/pairing-info`

Protected endpoints require `Authorization: Bearer <device_token>`:

- `POST /api/device/pair`
- `POST /api/device/provision_spotify`
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`
- `POST /api/device/dj_response`

The device token is stored in NVS and is never logged.

A Postman collection for these local ESP endpoints is available at `postman/SpotifyDJ ESP API.postman_collection.json`. Import it and set `base_url` to the device IP or mDNS URL and `device_token` to the token returned by Home Assistant pairing.

## Spotify Provisioning Payloads

Home Assistant can provision Spotify credentials through pairing/status/provisioning responses. Supported shapes:

```json
{
  "spotify_client_id": "...",
  "spotify_refresh_token": "...",
  "spotify_market": "NL"
}
```

```json
{
  "client_id": "...",
  "refresh_token": "...",
  "market": "NL"
}
```

```json
{
  "spotify": {
    "spotify_client_id": "...",
    "spotify_refresh_token": "...",
    "client_id": "...",
    "refresh_token": "...",
    "market": "NL"
  }
}
```

Nested Spotify fields have priority. If client ID or refresh token is missing, existing stored credentials are kept. Refresh tokens are never logged.

The external JSON field names above are intentionally compatible with the Home Assistant integration. Internally, the firmware stores them in the `spotifydj` NVS namespace with short ESP32 Preferences keys: `sp_client`, `sp_refresh` and `sp_market`.

If OAuth credentials become invalid, the web portal Spotify panel includes a manual repair form. Submit a new refresh token and, only when the device has no stored client ID, a client ID. The device stores the values in the `spotifydj` NVS namespace, immediately tests Spotify authorization, and clears the submitted fields from the page after the request.

## Push-To-Talk Voice Flow

Physical PTT:

1. Hold the encoder button.
2. The device streams raw PCM16 microphone audio to the Home Assistant Assist WebSocket pipeline.
3. Release the encoder button.
4. Home Assistant returns recognized text.
5. The ESP sends recognized text to the SpotifyDJ HA integration at `/api/spotify_dj/voice`.
6. The HA integration can return DJ text and an optional WAV or MP3 `audio_url`.
7. The ESP displays the DJ text briefly, detects the audio type from `Content-Type` or magic bytes, and plays compatible WAV/MP3 audio through the built-in speaker.
8. The UI returns to Now Playing.

Web portal PTT is a simulation button for testing the DJ-response path. The browser sends a fixed localized test command to `/api/voice-text`; the ESP forwards it to Home Assistant and displays/plays the returned DJ response just like the physical PTT flow. This requires WiFi and a successful Home Assistant pairing/device token, but it does not require Spotify credentials, an active Spotify playback session or browser microphone permission.

If the Home Assistant integration has been removed while the ESP still has an old pairing token, the web PTT simulation can return a Home Assistant voice endpoint 404. Reset Home Assistant pairing on the device or web portal, add the SpotifyDJ integration again, and pair the device with the new code.

The ESP also checks pairing health during periodic Home Assistant status updates and during device/web push-to-talk calls. HTTP 401, 403 or 404 responses from the Home Assistant SpotifyDJ endpoints mark the pairing as stale in runtime status, show a reset-pairing message, and turn the Home Assistant status indicator red. The stored pairing is not erased automatically; use Reset Home Assistant pairing to intentionally return to the pairing screen.

Optional micro wake word support is scaffolded for a trained `Spotify DJ` detector. Link a model implementation that provides:

```cpp
extern "C" bool spotifydj_micro_wake_word_detect(const int16_t *samples, size_t sampleCount);
```

The firmware feeds idle mono PCM16 microphone chunks to that hook. Without a linked model hook, wake word monitoring stays inactive and normal encoder push-to-talk behavior is unchanged.

## MQTT

MQTT settings can be entered in the captive portal, web portal or provisioned by Home Assistant. If `mqtt.host` is empty or absent, existing MQTT config is kept and no MQTT connection is attempted.

Topics use the device ID:

```text
spotifydj/<device_id>/status
spotifydj/<device_id>/event
spotifydj/<device_id>/command
```

Supported MQTT command categories include status request, next/previous, volume, transfer sound output, start playlist, DJ response text, screen brightness, dim timeout, turn-off timeout, speaker cue volume, language and theme.

If the broker returns authentication failures (`rc=4` or `rc=5`) three times in a row, the firmware stops MQTT reconnect attempts until credentials are changed or the device restarts. This prevents repeated log spam and needless network wakeups when broker credentials are wrong.

## Web Portal

The web portal starts after WiFi connects and is available at the device IP address and mDNS hostname. It provides Now Playing, DJ-response flow testing, album art, volume, previous/next, play/pause, sound output selection, queue, playlists, Home Assistant status, MQTT settings, Spotify refresh-token repair, WiFi credential update, diagnostics, logs, OTA upload and dark/light/auto theme support. Sound output lists always include `None`/`Geen` and `iPhone` before live Spotify Connect devices. The Home Assistant pairing banner opens the My Home Assistant setup link in a new browser tab so the local ESP page remains available. The header right-aligns status in the same order as the device: H, M, S, WiFi signal bars and a CSS-rendered battery indicator with the percentage inside the icon and a flashing charge marker while charging. The IP address is shown in the WiFi details block.

Spotify refresh-token repair accepts a new refresh token and, when no client ID is already stored on the device, a client ID. Submitted refresh tokens are stored in NVS but are never shown back in the portal or logged.

Spotify controls are disabled when Spotify is not connected or when there is no active playback where that action would not make sense.

The web portal includes Safari/iOS home-screen metadata and an Apple touch icon. On iPhone, open the device web portal in Safari, tap Share and choose `Add to Home Screen`. The shortcut opens the local portal as a standalone web app while the phone can reach the device on the network.

To reduce unnecessary ESP HTTP work, the portal only polls heavier panels when they are visible: logs poll only while the logs panel is visible and not paused, and queue, playlist and sound-output list refreshes run only when their related UI is on screen. Dynamic API and root page responses use `no-store` cache headers, while embedded static assets such as icons and the web manifest use browser cache headers.

Spotify's queue endpoint can omit upcoming tracks for playlist context playback. When the queue response is empty and the current context is a Spotify playlist, the firmware falls back to reading the current playlist tracks and shows the next items after the currently playing track. The `SpotifyDJ Liked Proxy` lookup scans multiple pages of the user's own playlists before falling back to public search, which helps private playlists and larger libraries.

## Firmware Architecture

`SpotifyDJApp` is the top-level coordinator for setup, loop timing, input routing and screen transitions. Storage and hardware policy are kept in smaller modules so production issues can be isolated more easily:

- `ProvisioningController` owns NVS provisioning keys for WiFi, MQTT, setup mode, display settings, language, theme and speaker cue volume.
- `PowerController` owns charger detection policy, timer-wake sleep decisions, button wake masks and watchdog setup/feed.
- `SpotifyDJMenuModel` contains host-testable menu counts and option values; `SpotifyDJMenu` adapts that model to Arduino strings and display labels.
- `NetworkActivity` applies bounded HTTP timeout policy and logs duration/result for long network flows such as Spotify, Home Assistant status, OTA, album art and DJ response audio.

Keep new provisioning writes in `ProvisioningController`, power/sleep/watchdog decisions in `PowerController`, pure menu counts/options in `SpotifyDJMenuModel`, and long HTTP timeout policy through `NetworkActivity`. This keeps `SpotifyDJApp` focused on orchestration instead of becoming the owner of every subsystem.

## Architecture Decisions

- Home Assistant is the trusted backend for pairing, Spotify command interpretation, Assist STT/TTS orchestration, OTA offer handling and optional MQTT provisioning. The ESP stores only the device token and the credentials required for local operation.
- The ESP remains the local edge device for display, controls, LED ring, battery policy, speaker cues, microphone capture and playback of HA-provided DJ response audio. It must keep working when optional HA/MQTT/web features are unused.
- Push-to-talk uses Route B: the ESP streams raw PCM16 to Home Assistant Assist, sends recognized text to the SpotifyDJ integration, then displays/plays the returned DJ response. The ESP does not call OpenAI or upload browser microphone audio.
- Home Assistant pairing validity is a runtime state. Stored NVS pairing is not deleted automatically on HA 401/403/404, but the `H` indicator turns red, PTT/web PTT are disabled and the UI instructs the user to reset pairing.
- Spotify OAuth credentials are accepted through HA provisioning, setup portal or the web repair form. Refresh tokens are never logged or shown back to the user.
- External API payload field names stay integration-friendly, for example `spotify_refresh_token`. Internal ESP32 Preferences keys must stay at 15 characters or less, so Spotify credentials are stored as `sp_client`, `sp_refresh` and `sp_market`.
- Long network operations are bounded by explicit timeout policy and tracked through `NetworkActivity`; UI/input responsiveness should not depend on a blocking Spotify, HA, OTA or audio download call finishing quickly.
- Web portal polling is visibility-aware for logs, queue, playlist and sound-output data. Keep root/status/log API responses uncached, but cache embedded static assets with explicit cache headers.
- The public ESP API is documented as a Postman collection with variables only. Do not commit real device tokens, refresh tokens, MQTT passwords or private HA URLs.
- Product firmware is proprietary/closed-source, while release binaries and manifests are published separately for Home Assistant OTA distribution.

## Battery, Charging and Turn Off

Battery percentage is voltage-estimated. Below 20%, the device shows a charge screen and restricts normal operation. Below 10%, the device shows a short charge prompt and turns off. While charging below 20%, the device stays in a charging screen and does not connect WiFi. Turn-off sleep periodically wakes briefly to probe for USB-C charger attach.

## Device Diagnostics

The device Settings menu includes a `Stress test` toggle. This is a local render/input monkey mode for diagnostics: it cycles through safe screens, menu selections and render paths every 250 ms, logs heap information every 20 steps, and stops automatically after two minutes. It never triggers OTA, factory reset, WiFi changes, Spotify playback mutations or credential changes. It also stops when pairing, voice recording or battery guard screens take over.

## OTA Firmware Updates

Home Assistant can call `POST /api/device/ota` with a valid bearer token and firmware URL. The device checks the target device type and battery/charging state before starting OTA. Release binaries use the distributable asset name `spotifydj-device-vX.Y.Z.bin`; the manifest `device` field is the OTA target and must be `lilygo-t-embed-s3`.

During firmware write, the display shows `Firmware update in progress..`, the LED ring turns purple and the device restarts after a successful update. The manifest `sha256` is required and verified while streaming; mismatches abort the update before reboot.

## GitHub Firmware Release

Release firmware can be prepared locally with `release.sh`. The public firmware repo `pcvantol/spotify-dj-firmware` also contains the release assets consumed by Home Assistant OTA.

The local release helper prepares a source release, injects the release version through PlatformIO build flags, creates `release/spotifydj-device-vX.Y.Z.bin`, writes `release/firmware_manifest.json`, commits, tags and pushes. The pushed git tag then triggers the GitHub Action, which builds and publishes the public firmware release in `pcvantol/spotify-dj-firmware`.

Old public firmware releases can be reviewed and pruned with the separate dry-run first cleanup helper:

```sh
scripts/cleanup_old_releases.sh --repo pcvantol/spotify-dj-firmware --dry-run
scripts/cleanup_old_releases.sh --repo pcvantol/spotify-dj-firmware --keep 1 --execute
```

```bash
./release.sh X.Y.Z
```

Preflight without changing files, building, committing, pushing or publishing:

```bash
./release.sh X.Y.Z --dry-run
```

Publish the generated firmware asset and manifest into the public firmware release repo as part of the same flow:

```bash
./release.sh X.Y.Z --publish-firmware-repo ../spotify-dj-firmware
```

Create the public GitHub release locally instead of waiting for GitHub Actions only when you explicitly need that fallback:

```bash
./release.sh X.Y.Z --gh-release
```

For example, `./release.sh 2.7.6 --dry-run` validates the release plan without touching files. Both `2.7.6` and `v2.7.6` are accepted; the script normalizes tags to `vX.Y.Z`.

Local development builds intentionally remain:

```cpp
#define SPOTIFYDJ_VERSION "dev"
#define SPOTIFYDJ_VERSION_TAG "vdev"
```

Release builds override these values through build flags.

## Development

Build firmware:

```bash
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Run native helper tests:

```bash
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/spotifydj_unit_tests
/tmp/spotifydj_unit_tests
```

Run release-script checks:

```bash
bash test/native/test_release.sh
```

The native test binary covers pure logic such as battery estimation, menu option counts/values, Spotify provisioning parsing, Spotify credential repair validation and network timeout helper behavior. The release shell test covers `release.sh` syntax, dry-run behavior and invalid version handling.

## Security Checklist

Before a release, scan for accidental secrets:

```bash
rg -n "SPOTIFY_CLIENT_ID|SPOTIFY_REFRESH_TOKEN|WIFI_SSID|WIFI_PASSWORD|client_secret|wifi144iot|AQB|5ea462" include src test scripts README.md AGENTS.md CHANGELOG.md -S
```

Expected findings should be documentation placeholders, parser field names, NVS key names or test dummy values only. Real refresh tokens, WiFi passwords, Home Assistant tokens, MQTT passwords and Spotify client secrets must not be committed.

Security defaults:

- `SPOTIFY_ALLOW_INSECURE_TLS` must be `0` for normal builds.
- No Spotify client secret is used on the ESP32.
- Protected device API endpoints require `Authorization: Bearer <device_token>`.
- Refresh tokens and device tokens are never logged.
- MQTT passwords are never logged.
- Factory reset clears local credentials, tokens, settings and local caches.

## Troubleshooting

- `Refresh token missing` or `Spotify client id missing`: provision Spotify credentials through Home Assistant or the setup portal.
- Spotify controls do nothing: check Spotify authorization, Spotify Premium and the active Spotify Connect output.
- `MQTT waiting for broker` or `rc=5`: check host, port, username and password. `rc=5` usually means authentication failed.
- No Home Assistant pairing code: check the web portal pairing banner, mDNS discovery and `/api/device/pairing-info`.
- PTT fails: check Home Assistant pairing, `ha_url`, `device_token`, Assist pipeline and logs.
- OTA fails: check battery/charging state, firmware URL, device type and network reachability.
- Device becomes sluggish: check logs for `Responsiveness: slow loop`, `free_heap`, `min_free_heap` and `largest_block`.

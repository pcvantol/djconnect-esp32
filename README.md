# DJConnect Firmware for LilyGO T-Embed-CC1101

DJConnect is proprietary ESP32-S3 firmware for the LilyGO T-Embed-CC1101. The device is a Home Assistant paired playback remote with a display, rotary encoder, top button, LED ring, web portal and Home Assistant device integration.

DJConnect is not a Spotify Connect speaker/player. The ESP does not store Spotify OAuth credentials or call the Spotify Web API directly. Playback commands are sent to the Home Assistant integration as generic backend-agnostic commands, so Home Assistant can proxy them to Spotify today or another playback backend such as Sonos or `media_player` later.

## Features

- Shows the active playback output reported by Home Assistant.
- Shows current track/podcast, artist/show, duration and progress.
- Scrolls long title and artist/show text once when the track changes.
- Volume control through the encoder, web portal and Home Assistant command API, limited to `0-60`.
- Separate shuffle and repeat controls from the device main menu, Now Playing web portal and Home Assistant command API.
- Language setting for the device UI, web portal and captive portal: English or Dutch.
- Theme setting for the device and web portal: Auto, Dark or Light.
- Log level setting from the device, web portal and Home Assistant command API: debug, info, warning or error. The default is info.
- Playlist browser on the device and web portal to start playlists directly.
- Current song menu screen with album art download/cache.
- Local Games menu and web portal games panel with Pong, Asteroids and Fly mini-games.
- Help screen with button/encoder controls, shown once after initial Home Assistant pairing and available from the main menu.
- Encoder short press on Now Playing: pause/resume.
- Encoder long press on Now Playing: push-to-talk through Home Assistant Assist; release stops listening.
- Top button short press: back in menus, otherwise next track.
- Top button double press: previous track.
- Top button long press: open the menu.
- Top button held for 10 seconds: restart/soft reset.
- Encoder button + top button held for 10 seconds: factory reset when battery state allows it.
- Menus for Current song, Up Next, Playlists, Sound Outputs, Games, Help, Settings, About and Logs.
- Mobile web portal with Now Playing, DJ-response simulation, album art, volume slider, outputs, queue, playlists, local games, logs, diagnostics, settings, WiFi update and OTA upload.
- Home Assistant pairing with mDNS discovery and device-token authentication.
- BLE WiFi provisioning in setup mode for apps/flows that actively write credentials to the device.
- Home Assistant discovery, periodic status updates and two-way commands, including selected settings such as log level.
- Battery/charging guards, turn-off sleep and low-battery screens.
- Charger-aware wake probe while turned off.
- WiFi-failure boot menu with retry, restart device, turn off and confirmed factory reset.
- OTA endpoint for Home Assistant-triggered firmware updates.
- Watchdog, slow-loop logging and periodic heap diagnostics.

## Hardware

- LilyGO T-Embed-CC1101.
- ESP32-S3 PlatformIO target on the pinned pioarduino ESP-IDF 5.3 / Arduino
  ESP32 3.x toolchain. Arduino ESP32 2.x / ESP-IDF 4.x compatibility is not
  maintained.
- ST7789 display.
- Rotary encoder with center button.
- Top button / board user key.
- BQ27220 battery gauge.
- WS2812 LED ring.
- Built-in speaker cues and microphone for push-to-talk.

## Important Limitations

- Playback backend requirements are handled by the Home Assistant integration. For Spotify this may still require Spotify Premium and an available Spotify Connect output.
- Some playback outputs may not support volume or queue metadata; the ESP disables unsupported actions when Home Assistant reports they are unavailable.
- OTA through `/api/device/ota` requires HTTPS and verifies the streamed firmware against the manifest SHA256 before rebooting.
- Web portal DJ-response test runs through the ESP and Home Assistant pairing, displays the returned DJ text on the device and does not require browser microphone access.

## License

DJConnect firmware is closed-source proprietary firmware. See [LICENSE](LICENSE).

The license is intended for commercial devices that ship with DJConnect firmware preinstalled. End users may use the firmware on the purchased device. The firmware, source code and firmware binaries may not be copied, modified, reverse engineered, extracted or redistributed without permission. The Home Assistant integration may be provided to customers for free, but it does not grant rights to the firmware.

## Secrets and Provisioning

WiFi and Home Assistant secrets are not compiled into firmware. Spotify or other playback-backend credentials live in Home Assistant, not on the ESP.

Do not place these in firmware headers:

- WiFi SSID
- WiFi password
- Playback-backend client IDs
- Playback-backend refresh tokens
- Playback-backend client secrets
- Home Assistant device token

Credentials are provisioned through setup flows and stored in NVS.

`include/Secrets.h` is only for optional build-time flags:

```cpp
#pragma once

// No WiFi, Home Assistant token, or playback-backend credentials live in firmware.
#define SPOTIFY_ALLOW_INSECURE_TLS 0
```

Keep `SPOTIFY_ALLOW_INSECURE_TLS` at `0` for normal builds. Set it to `1` only as a temporary local troubleshooting fallback.

## First Setup

1. Flash the firmware.
2. If no WiFi credentials are stored in NVS, the device starts setup/AP mode.
3. Connect to the WiFi network `DJConnect Setup`.
4. Open the captive portal or browse to the AP IP address.
5. Enter WiFi credentials.
6. The device tests WiFi, stores the credentials in NVS and restarts. Playback-backend credentials are configured in Home Assistant.

Setup/AP mode keeps the screen at 100% brightness, shows a deeply fading rainbow LED-ring breath animation, keeps battery/charging state visible, shows that the portal is active for 10 minutes, and turns off after 10 minutes without successful setup. The device screen also offers a center-button turn-off action while setup/AP mode is active.

## BLE WiFi Provisioning

iOS does not automatically share the current iPhone WiFi password with an ESP32 over BLE. DJConnect supports BLE provisioning where an app, Home Assistant flow or BLE tool actively writes credentials to the device.

During setup/AP mode and Home Assistant pairing mode:

- BLE name: `DJConnect xxxx`, where `xxxx` is the device ID suffix.
- Service UUID: `7f705000-9f8f-4f1a-9b5f-570071fd0001`
- Write characteristic UUID: `7f705001-9f8f-4f1a-9b5f-570071fd0001`
- Status read/notify characteristic UUID: `7f705002-9f8f-4f1a-9b5f-570071fd0001`

In setup/AP mode, write WiFi credentials as JSON:

```json
{
  "ssid": "MyWiFi",
  "password": "wifi-password"
}
```

Playback-backend credentials are not provisioned through BLE or stored on the ESP. Configure Spotify, Sonos or other backend credentials in the Home Assistant integration.

In Home Assistant pairing mode, BLE advertising remains active so a Home Assistant Bluetooth Proxy config flow can discover the ESP while the pairing code is visible. The BLE status characteristic reports the current pairing state and visible pair code. Home Assistant should not mark the device as paired merely because HA has generated a local token; the ESP must confirm pairing by storing the device token, and playback commands remain disabled until the next authenticated Home Assistant status check succeeds.

## Playback Backend

The ESP sends generic playback commands to Home Assistant and receives generic playback state back. The firmware intentionally avoids Spotify OAuth storage and direct Spotify Web API calls. Home Assistant owns backend-specific details such as Spotify refresh tokens, Sonos entity selection, media-player services, playlist lookup and queue behavior.

## Home Assistant Integration

The custom integration domain is `djconnect`.

When WiFi is configured but Home Assistant is not paired, the device enters pairing mode. The display shows the DJConnect logo/name, battery state, a large pairing code and a center-button turn-off hint. The screen stays at 100% brightness for 10 minutes and the LED ring breathes blue. Normal playback/menu input is blocked, while reset controls, BLE advertising, the web portal and the device API remain available.

### mDNS Discovery

After WiFi connect, the device advertises `_djconnect._tcp`.

Hostname format:

```text
djconnect-lilygo-XXXXXXXXXXXX
```

Browsable URL:

```text
http://djconnect-lilygo-XXXXXXXXXXXX.local
```

TXT records include `name`, `device_id`, `version`, `paired`, `api` and `model`.

### Local Device API

Open endpoints:

- `GET /api/device/info`
- `GET /api/device/pairing-info`

Protected endpoints require `Authorization: Bearer <device_token>`:

- `POST /api/device/pair`
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`
- `POST /api/device/dj_response`

Playback credentials live in Home Assistant and are never accepted or stored by the ESP.

The device token is stored in NVS and is never logged. During pairing, `/api/device/pair` accepts the Home Assistant callback with `device_token` plus `ha_local_url` and/or `ha_remote_url`. The firmware stores only those backend URLs, Assist pipeline id and lightweight settings such as `device_language`/`language`. The main loop then confirms the pairing through `/api/djconnect/status` and schedules an immediate playback status poll. Playback commands are not sent until that status check returns success, so stale or pending tokens do not generate repeated playback 401s.

Repeated direct callbacks with the same local/remote URLs and `device_token` are treated as idempotent and do not reset pairing validation. Home Assistant should still avoid using `/api/device/pair` as a generic settings-sync endpoint after pairing; use `/api/device/command` for settings and the status response contract for state acknowledgements.

### Local And Cloud Home Assistant URLs

The ESP supports a local-first/cloud-fallback route to Home Assistant:

- `ha_local_url`: the LAN URL the ESP should try first, for example `http://homeassistant.local:8123`.
- `ha_remote_url`: the optional Nabu Casa/cloud URL, for example `https://example.ui.nabu.casa`.

Playback, status, PTT voice and DJ response requests use the active HA route. The firmware briefly probes the local URL and falls back to the remote URL when local HA is not reachable. This keeps normal LAN latency low while still allowing the device to work when it is on another WiFi network with internet access. If both URLs are missing, pairing is rejected.

A Postman collection for these local ESP endpoints is available at `postman/DJConnect ESP API.postman_collection.json`. Import it and set `base_url` to the device IP or mDNS URL and `device_token` to the token returned by Home Assistant pairing.

## Playback Provisioning

The ESP no longer accepts or stores Spotify OAuth credentials and no local Spotify provisioning endpoint is registered. Backend credentials must be configured in Home Assistant. Pairing/status responses may still provide non-secret settings such as language or Assist pipeline id.

## Push-To-Talk Voice Flow

Physical PTT, from the Now Playing screen:

1. Hold the encoder button on the Now Playing screen.
2. The device records mono PCM16 audio as a WAV file on LittleFS.
3. Release the encoder button.
4. The ESP uploads the raw WAV body to the DJConnect HA integration at `/api/djconnect/voice` with the paired device token.
5. The HA integration performs the Home Assistant Assist/STT/TTS work on the backend. If Assist needs the HA WebSocket API, that WebSocket connection belongs inside the HA integration, not on the ESP.
6. The HA integration returns DJ text and an optional WAV or MP3 `audio_url`.
7. The ESP displays the DJ text briefly, detects the audio type from `Content-Type` or magic bytes, and plays compatible WAV/MP3 audio through the built-in speaker.
8. The UI returns to Now Playing.

The Current song screen is a read-only detail screen for album art and scrolling metadata. It uses the same top-button back action as other menu screens and does not start push-to-talk from the encoder button.

Games are local mini-games in the device menu and web portal. Pong shows score and high score in the title bar, uses the encoder for the paddle and pauses when the screen turns off. Asteroids uses the encoder to move horizontally and shoots on encoder press. Fly uses the encoder to move vertically and shoots on encoder press while flying left-to-right. Device game highscores are stored in the `provision` NVS namespace and are cleared by factory reset; web game highscores are stored locally in the browser.

Web portal PTT is a simulation button for testing the DJ-response text path. The browser sends a fixed localized test command to `/api/voice-text`; the ESP forwards it to Home Assistant, displays the returned DJ text on the device and then returns the voice/PTT state to idle. The web simulation intentionally does not play returned TTS audio on the device, so it cannot leave the speaker/audio path busy or block the physical encoder PTT flow. This requires WiFi and a successful Home Assistant pairing/device token, but it does not require playback-backend credentials on the ESP, an active playback session or browser microphone permission.

If the Home Assistant integration has been removed while the ESP still has an old pairing token, the web PTT simulation can return a Home Assistant voice endpoint 404. Reset Home Assistant pairing on the device or web portal, add the DJConnect integration again, and pair the device with the new code.

The ESP also checks pairing health during periodic Home Assistant status updates and during device/web push-to-talk calls. HTTP 401, 403 or 404 responses from the Home Assistant DJConnect endpoints mark the pairing as stale in runtime status, show a reset-pairing message, and turn the Home Assistant status indicator red. A freshly received direct-pairing token is treated as pending until HA accepts a status/playback command; if HA immediately rejects that pending token, the ESP clears only that pending pairing and returns to the pairing-code screen. Established pairings are not erased automatically; use Reset Home Assistant pairing to intentionally return to the pairing screen.

Optional micro wake word support includes the ESPHome `Okay Nabu` model asset
vendored under `third_party/micro_wake_word/` and linked into the firmware as
flash-resident model data. To make detection active, link a local inference
implementation that consumes that model and provides:

```cpp
extern "C" bool djconnect_oke_nabu_wake_word_detect(const int16_t *samples, size_t sampleCount);
```

The older `DJConnect` hook remains supported for compatibility:

```cpp
extern "C" bool djconnect_micro_wake_word_detect(const int16_t *samples, size_t sampleCount);
```

The firmware feeds idle mono PCM16 microphone chunks to the linked hook and prefers the `oke nabu` hook when both are present. Without a linked model hook, wake word monitoring stays inactive and normal encoder push-to-talk behavior is unchanged.

## Home Assistant Native Commands

Home Assistant controls the device through the local authenticated ESP API. Protected routes require:

```text
Authorization: Bearer <device_token>
```

The generic command endpoint is:

```text
POST /api/device/command
```

Supported command payloads:

```json
{"command":"status"}
{"command":"next"}
{"command":"previous"}
{"command":"set_volume","value":35}
{"command":"set_output","value":"iPhone"}
{"command":"start_playlist","value":"spotify:playlist:..."}
{"command":"screen_brightness","value":75}
{"command":"screen_dim_timeout","value":60000}
{"command":"turn_off_after","value":900000}
{"command":"speaker_volume","value":50}
{"command":"language","value":"nl"}
{"command":"theme","value":"dark"}
{"command":"log_level","value":"info"}
{"command":"dj_response","text":"Daar gaan we.","audio_url":"http://homeassistant.local:8123/api/djconnect/tts/example.mp3"}
```

Status is still posted periodically to the Home Assistant integration through `/api/djconnect/status`, and the ESP local API remains available for OTA, reboot, pairing reset, generic device commands and DJ response display/playback. Every ESP-to-Home Assistant JSON payload for `/api/djconnect/status` and `/api/djconnect/command` includes top-level `device_id` and `client_type:"esp32"`; the firmware does not send `device_type` in those payloads. Playback command payloads stay identity-only and must not carry partial device-status snapshots; `/api/djconnect/status` is the authoritative source for battery, firmware, RSSI, pairing, screen, LED, settings and sound-output sensor values. Raw WAV uploads to `/api/djconnect/voice` use the bearer token and `X-DJConnect-Device-ID` headers instead of a JSON body.

The status payload mirrors user device settings both top-level and under `settings`, including `client_type`, `ha_pairing_status`, `local_url`, `ha_local_url`, `ha_remote_url`, `ha_active_url`, `firmware`, `battery_percent`, `wifi_rssi`, `screen_brightness`/`brightness`, `screen_brightness_percent`, `screen_dim_timeout_ms`, `screen_off_timeout_ms`, `turn_off_after_ms`, `speaker_volume`/`cue_volume`, `speaker_volume_percent`, `language`, `theme`, `log_level`, `ota_state`, `update_state` and `sound_output`. It also includes `screen_state`, `led_state`, `screen.state`/`screen.brightness_level` and `led.state` so Home Assistant entities can refresh from the ESP state instead of defaulting to unknown or minimum values.

Playback command responses from Home Assistant should keep authentication failures separate from backend availability. HTTP 401/403/404 marks pairing stale. Temporary playback/backend failures should preferably return HTTP 200 with JSON such as `{"success":false,"backend_available":false,"message":"..."}`; the ESP then turns the playback status indicator red without clearing pairing. If HA returns `{"success":false,"error":"invalid_client_type"}` or an HTTP error body with `error:"invalid_client_type"`, the ESP logs `HA rejected payload: missing client_type=esp32`, treats it as a firmware/HA contract problem and does not clear NVS pairing or the device token.

After boot and Home Assistant setup, the ESP forces an immediate playback status command instead of waiting for the normal polling interval. This keeps the device `S` status indicator and LED-ring state in sync before the first physical control action.

## Web Portal

The web portal starts after WiFi connects and is available at the device IP address and mDNS hostname. It provides Now Playing, DJ-response flow testing, album art, volume, previous/next, play/pause, sound output selection, queue, playlists, local games, Home Assistant status, WiFi credential update, diagnostics, logs, OTA upload and dark/light/auto theme support. Sound output lists always include `None`/`Geen` and `iPhone` before live outputs returned by Home Assistant. The Home Assistant pairing banner opens the My Home Assistant setup link in a new browser tab so the local ESP page remains available. The header right-aligns status in the same order as the device: H, playback music-note icon, WiFi signal bars and a CSS-rendered battery indicator with the percentage inside the icon and a flashing charge marker while charging. The playback music-note indicator is green for active usable playback, grey when the playback backend is reachable but has no active playback, and red on playback proxy errors. The IP address is shown in the WiFi details block.

The device Logs screen shows the newest log tail by default and can be scrolled with the encoder to inspect older buffered entries. Serial, web and device logs use the compact `HH:mm INF` severity prefix format.

Playback controls are disabled when Home Assistant reports playback is not connected or when there is no active playback where that action would not make sense.

The web portal includes Safari/iOS home-screen metadata and an Apple touch icon. On iPhone, open the device web portal in Safari, tap Share and choose `Add to Home Screen`. The shortcut opens the local portal as a standalone web app while the phone can reach the device on the network.

To reduce unnecessary ESP HTTP work, the portal only polls heavier panels when they are visible: logs poll only while the logs panel is visible and not paused, and queue, playlist and sound-output list refreshes run only when their related UI is on screen. Dynamic API and root page responses use `no-store` cache headers, while embedded static assets such as icons and the web manifest use browser cache headers.

Queue, playlist and output data are supplied by the Home Assistant integration. Backend-specific fallbacks, such as Spotify playlist queue reconstruction, belong in Home Assistant.

## Firmware Architecture

`DJConnectApp` is the top-level coordinator for setup, loop timing, input routing and screen transitions. Storage and hardware policy are kept in smaller modules so production issues can be isolated more easily:

- `ProvisioningController` owns NVS provisioning keys for WiFi, setup mode, display settings, language, theme, log level and speaker cue volume.
- `PowerController` owns charger detection policy, timer-wake sleep decisions, button wake masks and watchdog setup/feed.
- `DJConnectMenuModel` contains host-testable menu counts and option values; `DJConnectMenu` adapts that model to Arduino strings and display labels.
- `NetworkActivity` applies bounded HTTP timeout policy and logs duration/result for long network flows such as Home Assistant playback proxy calls, status, OTA, album art and DJ response audio. Playback proxy controls use short waits plus a transient-failure cooldown so repeated HA 5xx/-1 responses cannot stack blocking commands.

Keep new provisioning writes in `ProvisioningController`, power/sleep/watchdog decisions in `PowerController`, pure menu counts/options in `DJConnectMenuModel`, and long HTTP timeout policy through `NetworkActivity`. This keeps `DJConnectApp` focused on orchestration instead of becoming the owner of every subsystem.

## Architecture Decisions

- Home Assistant is the trusted backend for pairing, playback command interpretation, backend credentials, Assist STT/TTS orchestration and OTA offer handling. The ESP stores only WiFi settings and its Home Assistant device token.
- The ESP remains the local edge device for display, controls, LED ring, battery policy, speaker cues, microphone capture and playback of HA-provided DJ response audio. It must keep working when optional HA/web features are unused.
- Push-to-talk uses the DJConnect integration as the backend boundary: the ESP records WAV audio and uploads it to `/api/djconnect/voice`; the HA integration owns Assist/STT/TTS and returns DJ text plus optional audio URL. The ESP does not authenticate directly to Home Assistant core `/api/websocket`, call OpenAI directly or upload browser microphone audio.
- Home Assistant pairing validity is a runtime state. Stored NVS pairing is not deleted automatically on HA 401/403/404, but the `H` indicator turns red, PTT/web PTT and playback proxy commands are disabled, and the UI instructs the user to reset pairing.
- The device Settings menu includes `Change WiFi`, which restarts into the setup/captive portal while preserving the stored Home Assistant pairing token. Use Factory reset or Reset Home Assistant pairing when pairing should be removed.
- Spotify OAuth and other playback-backend credentials are never stored on the ESP.
- Long network operations are bounded by explicit timeout policy and tracked through `NetworkActivity`; UI/input responsiveness should not depend on a blocking Spotify, HA, OTA or audio download call finishing quickly.
- Pre-pairing bootstrap OTA is a safety net for freshly provisioned devices: after WiFi connects but before Home Assistant pairing, release firmware checks the public GitHub firmware release manifest and updates itself if a newer release exists. Local `dev` / `vdev` firmware is intentionally excluded from this automatic path.
- Web portal polling is visibility-aware for logs, queue, playlist and sound-output data. Keep root/status/log API responses uncached, but cache embedded static assets with explicit cache headers.
- The public ESP API is documented as a Postman collection with variables only. Do not commit real device tokens or private HA URLs.
- Product firmware is proprietary/closed-source, while release binaries and manifests are published separately for Home Assistant OTA distribution.

## Battery, Charging and Turn Off

Battery percentage is voltage-estimated. Below 20%, the device shows a charge screen and restricts normal operation. Below 10%, the device shows a short charge prompt and turns off. While charging below 20%, the device stays in a charging screen and does not connect WiFi. Turn-off sleep periodically wakes briefly to probe for USB-C charger attach.

## Device Diagnostics

The device Settings menu includes a `Stress test` toggle. This is a local render/input monkey mode for diagnostics: it cycles through safe screens, menu selections and render paths every 250 ms, logs heap information every 20 steps, and stops automatically after two minutes. It never triggers OTA, factory reset, WiFi changes, Spotify playback mutations or credential changes. It also stops when pairing, voice recording or battery guard screens take over.

## OTA Firmware Updates

Home Assistant can call `POST /api/device/ota` with a valid bearer token and firmware URL. The device checks the target device type and battery/charging state before starting OTA. Release binaries use the distributable asset name `djconnect-device-vX.Y.Z.bin`; the manifest `device` field is the OTA target and must be `lilygo-t-embed-s3`.

Local development builds still show `vdev` on the device, but the Home Assistant/device API reports dev firmware as OTA-comparable version `0.0.0`. This keeps local flashes clearly recognizable while ensuring every published `X.Y.Z` firmware release is seen as an upgrade from a dev build.

Freshly provisioned, unpaired release firmware also performs a pre-pairing bootstrap update check after WiFi connects. It uses the GitHub Releases API for `pcvantol/djconnect-firmware`, follows the normal OTA screen/LED/sound/write flow when a newer release is available, and continues silently into pairing if the check fails. Dev builds are skipped so local development firmware is not replaced automatically.

During normal boot, the LED ring plays one calm rainbow startup lap before WiFi, setup/AP, charging or playback states take over. During WiFi connect the LED ring shows a green chase animation; during Home Assistant pairing it breathes blue; during setup/AP it breathes rainbow. Turn-off/deep-sleep always plays a rainbow fade-out, while top-button soft reset plays a dedicated cue and two bright white LED flashes before reboot. During firmware write, the display shows `Firmware update in progress..`, the LED ring runs a fast purple animation and the device plays start, progress, complete or failure cues through the built-in speaker. Manual web uploads use the same on-device update screen and feedback. The manifest `sha256` is required and verified while streaming for Home Assistant OTA; mismatches abort the update before reboot. OTA downloads tolerate slow GitHub/CDN bursts with a longer idle window while still aborting controlled when the stream genuinely stalls.

## GitHub Firmware Release

Release firmware can be prepared locally with `release.sh`. The public firmware repo `pcvantol/djconnect-firmware` also contains the release assets consumed by Home Assistant OTA.

The local release helper prepares a source release, injects the release version through PlatformIO build flags, creates ignored local `release/djconnect-device-vX.Y.Z.bin` and `release/firmware_manifest.json` artifacts, commits source metadata, tags and pushes. The pushed git tag then triggers the GitHub Action, which builds and publishes the public firmware release in `pcvantol/djconnect-firmware`. The action verifies that the compiled firmware contains the expected `vX.Y.Z` version tag before publishing the single OTA asset `djconnect-device-vX.Y.Z.bin`.

Old public firmware releases can be reviewed and pruned with the separate dry-run first cleanup helper:

```sh
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --dry-run
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --keep 1 --execute
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
./release.sh X.Y.Z --publish-firmware-repo ../djconnect-firmware
```

Create the public GitHub release locally instead of waiting for GitHub Actions only when you explicitly need that fallback:

```bash
./release.sh X.Y.Z --gh-release
```

For example, `./release.sh 3.0.10 --dry-run` validates the release plan without touching files. Both `3.0.10` and `v3.0.10` are accepted; the script normalizes tags to `vX.Y.Z`.

Local development builds intentionally remain:

```cpp
#define DJCONNECT_VERSION "dev"
#define DJCONNECT_VERSION_TAG "vdev"
```

Release builds override these values through build flags.

## Development

Build firmware:

```bash
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

The firmware environment is pinned to the pioarduino ESP32 platform line that
uses ESP-IDF 5.3 and Arduino ESP32 3.x. The source intentionally uses the
Arduino 3.x pin-based LEDC API, IDF 5 watchdog API and mbedTLS 3 SHA256 API
without older Arduino 2.x / ESP-IDF 4.x fallback branches. Recurring legacy-I2S
warnings from `ESP8266Audio` and the current microphone recorder are expected
until those paths move to the IDF 5.x `i2s_std`/`i2s_pdm` drivers.

Run native helper tests:

```bash
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests
/tmp/djconnect_unit_tests
```

Run release-script checks:

```bash
bash test/native/test_release.sh
```

The native test binary covers pure logic such as battery estimation, menu option counts/values including log-level options, device command parsing, Home Assistant pairing/provisioning helpers and network timeout helper behavior. The release shell test covers `release.sh` syntax, dry-run behavior and invalid version handling.

## Security Checklist

Before a release, scan for accidental secrets:

```bash
rg -n "SPOTIFY_CLIENT_ID|SPOTIFY_REFRESH_TOKEN|WIFI_SSID|WIFI_PASSWORD|client_secret|wifi144iot|AQB|5ea462" include src test scripts README.md AGENTS.md CHANGELOG.md -S
```

Expected findings should be documentation placeholders, parser field names, NVS key names or test dummy values only. Real refresh tokens, WiFi passwords, Home Assistant tokens and playback-backend client secrets must not be committed.

Security defaults:

- `SPOTIFY_ALLOW_INSECURE_TLS` must be `0` for normal builds.
- No playback-backend client secret is used on the ESP32.
- Protected device API endpoints require `Authorization: Bearer <device_token>`.
- Device tokens are never logged.
- Factory reset clears local credentials, tokens, settings and local caches.

NVS encryption status:

- The firmware currently stores WiFi and Home Assistant credentials in ESP32 NVS, but the Arduino/PlatformIO build does not enable encrypted NVS yet.
- True NVS Encryption requires an ESP-IDF or Arduino-as-component build with `CONFIG_NVS_ENCRYPTION=y` and a partition table that includes an `nvs_keys` partition.
- Changing the partition table is a factory/serial-flash migration, not an OTA-only change. Existing devices must be re-provisioned or migrated carefully so old plaintext NVS data is not left behind.
- Do not claim encrypted credential storage in releases until the build config and partition table prove that encrypted NVS is active.

## Troubleshooting

- Playback controls do nothing: check Home Assistant pairing, the HA integration command endpoint, backend authorization and the active output.
- No Home Assistant pairing code: check the web portal pairing banner, mDNS discovery and `/api/device/pairing-info`.
- PTT fails: check Home Assistant pairing, `ha_local_url`/`ha_remote_url`, `device_token`, the HA integration `/api/djconnect/voice` handler and HA Assist/TTS logs. The ESP should not report `Assist auth_invalid`; Assist WebSocket authentication belongs on the HA integration side.
- OTA fails: check battery/charging state, firmware URL, device type and network reachability.
- Device becomes sluggish: check logs for `Responsiveness: slow loop`, `free_heap`, `min_free_heap` and `largest_block`.

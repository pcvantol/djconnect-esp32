# DJConnect Firmware

DJConnect. Muziekbediening met karakter.

DJConnect is MIT-licensed ESP32-S3 firmware for the LilyGO T-Embed-CC1101. The device is a Home Assistant paired playback remote with a display, rotary encoder, top button, LED ring, web portal and Home Assistant device integration.

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
- Local Games menu and web portal games panel with Paddle Rally, Meteor Run, Sky Dash and Maze Chase mini-games.
- Help screen with button/encoder controls, shown once after initial Home Assistant pairing and available from the main menu.
- Encoder short press on Now Playing: pause/resume.
- Encoder long press on Now Playing: push-to-talk through Home Assistant Assist; release stops listening.
- Top button short press: back in menus, otherwise next track.
- Top button double press: previous track.
- Top button long press: open the menu.
- Top button held for 10 seconds: restart/soft reset.
- Encoder button + top button held for 10 seconds: factory reset when battery state allows it.
- Menus for Current song, Up Next, Playlists, Sound Outputs, Games, Help, Settings, About and Logs.
- Mobile web portal with Now Playing, DJ announcement simulation, album art, volume slider, outputs, queue, playlists, local games, logs, diagnostics, settings, WiFi update and OTA upload.
- Home Assistant pairing with mDNS discovery and device-token authentication.
- BLE WiFi provisioning in setup mode for apps/flows that actively write credentials to the device.
- Home Assistant discovery, periodic status updates and two-way commands, including selected settings such as log level.
- Battery/charging guards, turn-off sleep and low-battery screens.
- Charger-aware wake probe while turned off.
- WiFi-failure boot menu with retry, restart device, turn off and confirmed factory reset.
- OTA endpoint for Home Assistant-triggered firmware updates.
- Watchdog, slow-loop logging and periodic heap diagnostics.

## Hardware

Supported firmware target:

- LilyGO T-Embed-CC1101 / T-Embed S3 through PlatformIO environment `t_embed_cc1101`.

The LilyGO build uses the ST7789 display, rotary encoder with center button,
top button, BQ27220 battery gauge, WS2812 LED ring, built-in speaker cues and
microphone for push-to-talk.

The firmware uses the pinned pioarduino ESP-IDF 5.3 / Arduino ESP32 3.x toolchain.
Arduino ESP32 2.x / ESP-IDF 4.x compatibility is not maintained.

## Important Limitations

- Playback backend requirements are handled by the Home Assistant integration. For Spotify this may still require Spotify Premium and an available Spotify Connect output.
- Some playback outputs may not support volume or queue metadata; the ESP disables unsupported actions when Home Assistant reports they are unavailable.
- Queue stores and renders up to 100 queue items from Home Assistant. Longer backend queues are truncated on the ESP.
- OTA through `/api/device/ota` requires HTTPS and verifies the streamed firmware against the manifest SHA256 before rebooting.
- Web portal DJ announcement test runs through the ESP and Home Assistant pairing, displays the returned DJ text on the device and does not require browser microphone access.

## Cross-Repo Sync Prompts

Canonical prompts for syncing this firmware repo with the Home Assistant integration, Apple app, Raspberry Pi client and website/docs repos live only in `pcvantol/djconnect/SYNC_PROMPTS.md`. This firmware repo intentionally does not keep a local copy. When an ESP firmware change updates a cross-repo contract, make a follow-up change in `pcvantol/djconnect`.

## Technical Design Decisions

Reverse-engineered firmware design decisions, code-level patterns, coding style
conventions and the full framework/library dependency inventory live in
[`DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md). Keep it updated with every release
when architecture, coding conventions, board support, APIs, release flow or
dependencies change.

Local setup, build, upload and release workflow details live in
[`DEVELOPMENT_ENVIRONMENT.md`](DEVELOPMENT_ENVIRONMENT.md).

## Product Roadmap

Product ideas, killer features, production-release must-haves and premium
feature candidates live only in `pcvantol/djconnect/PRODUCT_ROADMAP.md`. This
firmware repo intentionally does not keep a local copy. When an ESP firmware
change introduces roadmap-relevant product scope, make a follow-up change in
`pcvantol/djconnect`.

## License

DJConnect firmware is open-source software licensed under the MIT License. See [LICENSE](LICENSE).

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

Setup/AP mode keeps the screen at 100% brightness, shows a deeply fading rainbow LED-ring breath animation, keeps battery/charging state visible on boards with a battery gauge, shows that the portal is active for 10 minutes, and turns off after 10 minutes without successful setup. The captive portal uses the same DJConnect icon and blue/purple visual style as the main web portal, and includes the board device model in the title/header. The device screen also offers a center-button turn-off action while setup/AP mode is active.

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

The ESP sends generic playback commands to Home Assistant and receives generic playback state back. The firmware intentionally avoids Spotify OAuth storage and direct Spotify Web API calls. Home Assistant owns backend-specific details such as Spotify refresh tokens, Sonos entity selection, media-player services, playlist lookup and queue behavior. New ESP/client setup or settings flows must not expose or expect legacy playback source/default-playlist override options; those decisions belong in the Home Assistant integration.

List-style playback commands must include safe limits when a client can provide
one. ESP32 firmware sends `{"command":"playlists","limit":20}` so the device and
embedded web portal stay responsive, while iOS/macOS clients may request
`{"command":"playlists","limit":50}`. Home Assistant must also remain backwards
compatible with older clients that omit the playlist limit by defaulting
internally to Spotify's safe maximum of `50`, not to an invalid value. Queue
requests use `{"command":"queue","limit":100}` and Home Assistant should clamp
or validate incoming positive integer limits before calling provider APIs.
Provider-specific technical failures should be logged, while user-facing
responses should stay generic and JSON-shaped.

## Home Assistant Integration

The custom integration domain is `djconnect`.

When WiFi is configured but Home Assistant is not paired, the device enters pairing mode. The display shows the DJConnect logo/name, battery state, the default Home Assistant URL hint `http://homeassistant.local:8123`, a large pairing code and a center-button turn-off hint. Home Assistant/user-facing setup text should label the device URL as `Client adres`. The screen stays at 100% brightness for 10 minutes and the LED ring breathes blue. Normal playback/menu input is blocked, while reset controls, BLE advertising, the web portal and the device API remain available.

### mDNS Discovery

After WiFi connect, an unpaired device advertises `_djconnect._tcp` so Home Assistant can discover it for setup. The ESP stops the mDNS discovery service as soon as Home Assistant pairing is stored, and starts advertising again only after pairing is reset or the device returns to setup/pairing mode.

Hostname format:

```text
djconnect-<device-model>-XXXXXXXXXXXX
```

Browsable URL:

```text
http://djconnect-<device-model>-XXXXXXXXXXXX.local
```

The hostname is the device ID. LilyGO uses `djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX`. TXT records include `name`, `device_id`, `client_type`, `version`, `paired`, `api` and `model`.
The firmware does not use or accept persistent legacy IDs such as `djconnect-XXXXXXXXXXXX`, `djconnect-lilygo-XXXXXXXXXXXX` or `djconnect-[six-digit-code]`. The six-digit setup value is only a temporary `pair_code`.

### Local Device API

Open endpoints:

- `GET /api/device/info`
- `GET /api/device/pairing-info`

`GET /api/device/pairing-info` returns the model-specific `device_id`, `device_name`, temporary `pair_code`, `client_type:"esp32"`, firmware and mDNS `local_url`. It does not return or accept `device_type`.

Protected endpoints require `Authorization: Bearer <device_token>`:

- `POST /api/device/pair`
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`
- `POST /api/device/dj_response`
- `GET /api/device/screenshot.bmp`

Local `dev` / `vdev` firmware reports version `0.0.0` and allows
`/api/device/screenshot.bmp` without a bearer token for local development
screen-capture workflows. Published release firmware keeps this endpoint
protected by the paired device token.

Local `dev` / `vdev` firmware also exposes `POST /api/device/debug/screen`
with a `screen` query/body value so development tooling can open known device
screens before capturing screenshots. Release firmware requires the paired
device token for this route. Use `scripts/capture_device_screens.sh` to capture
all known screens locally or through an SSH jump host on the same LAN.
Release visual checks should overwrite the committed screenshot baselines under
`docs/screenshots/device/` and `docs/screenshots/web/` so UI diffs are visible
in the release commit. Device screenshots come from the ESP debug screenshot
API; web portal screenshots should be captured from the live local portal when
the device is reachable.

Playback credentials live in Home Assistant and are never accepted or stored by the ESP.

The device token is stored in NVS and is never logged. During pairing, `/api/device/pair` accepts the Home Assistant callback with `device_token`, a required LAN `ha_local_url`, Assist pipeline id and lightweight settings such as `device_language`/`language`. The main loop then confirms the pairing through `/api/djconnect/status` over the LAN URL and schedules an immediate playback status poll. Playback commands are not sent until that status check returns success, so stale or pending tokens do not generate repeated playback 401s.

Repeated direct callbacks with the same local URL and `device_token` are treated as idempotent and do not reset pairing validation. Home Assistant should still avoid using `/api/device/pair` as a generic settings-sync endpoint after pairing; use `/api/device/command` for settings and the status response contract for state acknowledgements.

### Local Home Assistant URL

The ESP stores one Home Assistant URL for runtime traffic:

- `ha_local_url`: the LAN URL the ESP should use for normal control, for example `http://192.168.1.10:8123`. The pairing screen shows `http://homeassistant.local:8123` as the default user hint for where Home Assistant is expected on the LAN.

`ha_local_url` must be a real LAN URL and must not contain `.ui.nabu.casa`. If Home Assistant sends a Nabu Casa URL as `ha_local_url`, the ESP rejects pairing instead of entering a half-paired state. Status, playback and voice calls always use the local URL. Cloud/Nabu Casa URLs are not stored or used by the ESP runtime; they belong only in Home Assistant's backend/OAuth configuration flows.

Postman collections are available under `postman/`. `DJConnect ESP API.postman_collection.json` documents the protected Home Assistant device-layer endpoints, and `DJConnect Local API.postman_collection.json` covers the local web portal/API test routes. Import one of them and set `base_url` to the device IP or mDNS URL and `device_token` to the token returned by Home Assistant pairing when testing protected routes.

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

If the ESP log shows `audio_url=none`, the Home Assistant integration returned a text-only DJ announcement. In that case the ESP has no audio URL to fetch and cannot play speaker audio; the fix belongs in the Home Assistant voice/TTS response path. At debug log level, the ESP logs the full audio URL it calls in chunked `DJ response audio URL` lines.

While the device is processing the PTT request or showing the DJ announcement screen, pressing the middle encoder button cancels the rest of the PTT/DJ-announcement flow as soon as possible. If an HA HTTP request is already in flight, the firmware ignores the result when it returns; response audio playback receives a stop request and exits on the next stream loop.

The Current song screen is a read-only detail screen for album art and scrolling metadata. It uses the same top-button back action as other menu screens and does not start push-to-talk from the encoder button.

Games are local mini-games in the device menu and web portal. Paddle Rally shows score and high score in the title bar, uses the encoder for the paddle, plays subtle 8-bit hit/miss cues, flashes paddle/wall hits and pauses briefly before restarting after a miss. Meteor Run uses the encoder to move horizontally and shoots on encoder press; meteors fall straight down with varied smaller shapes/speeds, a subtle star field and hit/miss sounds. Sky Dash uses the encoder to move vertically and shoots on encoder press while flying left-to-right through animated star streaks and varied obstacle shapes/colors. Maze Chase uses the encoder for horizontal movement and center press to switch lanes; four corner power pellets make the ghost temporarily vulnerable, with blinking feedback, Pacman eye direction, ghost-catch/death sounds and a short reset delay after death. Device game highscores are stored in the `provision` NVS namespace and are cleared by factory reset; web game highscores are stored locally in the browser.

Web portal PTT is a simulation button for testing the DJ-announcement text path. The browser sends a fixed localized test command to `/api/voice-text`; the ESP forwards it to Home Assistant, displays the returned DJ text on the device and then returns the voice/PTT state to idle. The web simulation intentionally does not play returned TTS audio on the device, so it cannot leave the speaker/audio path busy or block the physical encoder PTT flow. This requires WiFi and a successful Home Assistant pairing/device token, but it does not require playback-backend credentials on the ESP, an active playback session or browser microphone permission.

If the Home Assistant integration has been removed while the ESP still has an old pairing token, the web PTT simulation can return a Home Assistant voice endpoint 404. Reset Home Assistant pairing on the device or web portal, add the DJConnect integration again, and pair the device with the new code.

The ESP also checks pairing health during periodic Home Assistant status updates and during device/web push-to-talk calls. HTTP 401, 403 or 404 responses from the Home Assistant DJConnect endpoints mark the pairing as stale in runtime status, show a reset-pairing message, and turn the Home Assistant status indicator red. A freshly received direct-pairing token is treated as pending until HA accepts a status/playback command; if HA immediately rejects that pending token, the ESP clears only that pending pairing and returns to the pairing-code screen. Established pairings are not erased automatically; use Reset Home Assistant pairing to intentionally return to the pairing screen.

Micro wake word support includes the ESPHome `Okay Nabu` model asset vendored
under `third_party/micro_wake_word/` and linked into the firmware as
flash-resident model data. The firmware runs it locally with TensorFlow Lite
Micro and the TensorFlow micro_speech frontend: idle mono PCM16 microphone
chunks are converted into 40-bin features every 10 ms, three feature frames are
fed into the streaming model, and detection is accepted when the 3-frame sliding
window reaches the configured cutoff. The LilyGO T-Embed-CC1101 keeps the more
conservative `0.90` cutoff.

Wake-word monitoring is available only while the device is in normal mode, not
already recording voice, and Home Assistant pairing is confirmed. It is stored
as a user setting and defaults to off after factory reset/pairing; it can be
enabled from the device Settings menu, the web portal settings panel or the
Home Assistant wake-word entity. It does not require an active music playback
session, so the user can request music while playback is idle or paused. Direct
pairing exits the BLE/pairing screen before any wake-word runtime can start, so
the TensorFlow arena is allocated only after the user has explicitly enabled the
feature and pairing heap pressure has dropped. On detection, the device starts
the same local PTT WAV upload flow as an encoder-button voice request; no
wake-word audio leaves the device until the wake phrase has been detected and
the normal PTT recording starts.

Wake-word-started recordings stop automatically after the minimum listen window
when microphone RMS stays below the silence threshold for 1.2 seconds, and all
voice recordings remain capped at 15 seconds. This keeps hands-free requests from
waiting indefinitely in a quiet room.

The older `DJConnect` hook remains supported for compatibility:

```cpp
extern "C" bool djconnect_micro_wake_word_detect(const int16_t *samples, size_t sampleCount);
```

The built-in `oke nabu` runtime is preferred over the legacy hook when both are
present. Detection is local-only and does not perform network I/O from the audio
poll path.

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

The status payload mirrors user device settings both top-level and under `settings`, including `client_type`, `ha_pairing_status`, `local_url`, `ha_local_url`, `firmware`, `battery_percent`, `wifi_rssi`, `screen_brightness`/`brightness`, `screen_brightness_percent`, `screen_dim_timeout_ms`, `screen_off_timeout_ms`, `turn_off_after_ms`, `speaker_volume`/`cue_volume`, `speaker_volume_percent`, `language`, `theme`, `log_level`, `wake_word_enabled`/`wake_word`, `ota_state`, `update_state`, `sound_output` and `playback_configured`. It also sends the compatibility hint `spotify_configured` as the same boolean without exposing credentials. The payload includes `screen_state`, `led_state`, `screen.state`/`screen.brightness_level` and `led.state` so Home Assistant entities can refresh from the ESP state instead of defaulting to unknown or minimum values.

Playback command responses from Home Assistant should keep authentication failures separate from backend availability. HTTP 401/403/404 marks pairing stale. Temporary playback/backend failures should preferably return HTTP 200 with JSON such as `{"success":false,"backend_available":false,"message":"..."}`; the ESP then turns the playback status indicator red without clearing pairing and shows a localized Home Assistant Spotify-connection hint instead of raw provider errors. If HA returns `{"success":false,"error":"invalid_client_type"}` or an HTTP error body with `error:"invalid_client_type"`, the ESP logs `HA rejected payload: missing client_type=esp32`, treats it as a firmware/HA contract problem and does not clear NVS pairing or the device token. HTTP 426 with `error:"version_mismatch"` requires a firmware/integration update and also keeps pairing/token intact.

After boot and Home Assistant setup, the ESP posts status immediately, then gives automatic playback polling a short grace period before the first background playback status command. Physical playback controls remain available, while the delayed background poll avoids stacking wake-word startup, HA status and playback proxy work during the tight no-PSRAM boot window.

## Web Portal

The web portal starts after WiFi connects and is available at the device IP address and mDNS hostname. It provides Now Playing, DJ-announcement flow testing, album art, volume, previous/next, play/pause, sound output selection, queue, playlists, local games, Home Assistant status, WiFi credential update, diagnostics, logs, OTA upload and dark/light/auto theme support. Queue and playlist panels have explicit refresh buttons and compact internal scroll areas. Queue and playlist entries render as artwork rows with their own play buttons; playlist artwork is lazy-loaded by the browser when HA provides an image URL. The portal uses the current DJConnect icon style with blue/purple brand accents for headers, panels and primary actions while preserving green/red/yellow status colors for state. Sound output lists always include `None`/`Geen`, then live non-iPhone outputs returned by Home Assistant. The Home Assistant pairing banner opens the My Home Assistant setup link in a new browser tab so the local ESP page remains available. The header shows `Muziekbediening met karakter`, firmware version and board device model, includes a compact `djconnect.dev` website link in the title/status bar, then right-aligns status in the same order as the device: H, playback music-note icon, WiFi signal bars and, only on boards with a battery gauge, a CSS-rendered battery indicator with the percentage inside the icon and a flashing charge marker while charging. The playback music-note indicator is green for active usable playback, grey when the playback backend is reachable but has no active playback, and red on playback proxy errors. The IP address is shown in the WiFi details block.

The device Logs screen shows the newest log tail by default and can be scrolled with the encoder to inspect older buffered entries. Serial, web and device logs use the compact `HH:mm INF` severity prefix format.

Playback controls are disabled when Home Assistant reports playback is not connected or when there is no active playback where that action would not make sense.

The web portal includes Safari/iOS home-screen metadata and an Apple touch icon. On iPhone, open the device web portal in Safari, tap Share and choose `Add to Home Screen`. The shortcut opens the local portal as a standalone web app while the phone can reach the device on the network.

To reduce unnecessary ESP HTTP work, the portal only polls heavier panels when they are visible: logs poll only while the logs panel is visible and not paused, and queue, playlist and sound-output list refreshes run only when their related UI is on screen. Dynamic API and root page responses use `no-store` cache headers, while embedded static assets such as icons and the web manifest use browser cache headers.

Queue, playlist and output data are supplied by the Home Assistant integration. The firmware accepts up to 100 queue items, then de-duplicates returned queue items by URI, or by title/subtitle when no URI is supplied, so a one-track queue is not rendered repeatedly on the device or web portal. Queue items with a direct Spotify track or episode URI remain startable even when Home Assistant does not provide `context_uri` or `queue_context`; when playlist, album or show context is available, the ESP also sends context plus offset metadata for backend playback. Playlist fetching/rendering remains capped at 20 items on ESP32 clients. Backend-specific fallbacks, such as Spotify playlist queue reconstruction, belong in Home Assistant.

## Firmware Architecture

`DJConnectApp` is the top-level coordinator for setup, loop timing, input routing and screen transitions. Storage and hardware policy are kept in smaller modules so production issues can be isolated more easily:

- `ProvisioningController` owns NVS provisioning keys for WiFi, setup mode, display settings, language, theme, log level, speaker cue volume and the wake-word enable flag.
- `PowerController` owns charger detection policy, timer-wake sleep decisions, button wake masks and watchdog setup/feed.
- `DJConnectMenuModel` contains host-testable menu counts and option values; `DJConnectMenu` adapts that model to Arduino strings and display labels.
- `NetworkActivity` applies bounded HTTP timeout policy and logs duration/result for long network flows such as Home Assistant playback proxy calls, status, OTA, album art and DJ response audio. Playback proxy controls use short waits plus a transient-failure cooldown so repeated HA 5xx/-1 responses cannot stack blocking commands.

Keep new provisioning writes in `ProvisioningController`, power/sleep/watchdog decisions in `PowerController`, pure menu counts/options in `DJConnectMenuModel`, and long HTTP timeout policy through `NetworkActivity`. This keeps `DJConnectApp` focused on orchestration instead of becoming the owner of every subsystem.

The LilyGO environment stays on the existing no-PSRAM `esp32-s3-devkitc-1` definition until a supported LilyGO T-Embed-CC1101 PSRAM module variant is explicitly verified.

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
- Product firmware is MIT-licensed open-source software, while release binaries and manifests are published separately for Home Assistant OTA distribution.

## Battery, Charging and Turn Off

Battery percentage is voltage-estimated. Below 20%, the device shows a charge screen and restricts normal operation. Below 10%, the device shows a short charge prompt and turns off. While charging below 20%, the device stays in a charging screen and does not connect WiFi. Normal idle turn-off sleep is suppressed while USB-C/external charging power is detected; on battery, turn-off sleep periodically wakes briefly to probe for USB-C charger attach.

## Device Diagnostics

The device Settings menu includes a `Stress test` toggle. This is a local render/input monkey mode for diagnostics: it cycles through safe screens, menu selections and render paths every 250 ms, logs heap information every 20 steps, and stops automatically after two minutes. It never triggers OTA, factory reset, WiFi changes, Spotify playback mutations or credential changes. It also stops when pairing, voice recording or battery guard screens take over.

## OTA Firmware Updates

Home Assistant can call `POST /api/device/ota` with a valid bearer token and firmware URL. The device checks the target device type and battery/charging state before starting OTA. Release binaries use the LilyGO asset name `djconnect-lilygo-t-embed-s3-vX.Y.Z.bin`. The manifest uses a `firmwares` array with the board profile, target device, asset, SHA256 and size; Home Assistant must select the entry that matches the paired device model. `min_ha_integration` and `max_ha_integration` are derived from the firmware major/minor version, so firmware `X.Y.Z` publishes `min_ha_integration: "X.Y.0"` and exclusive `max_ha_integration: "X.(Y+1).0"`.

Local development builds still show `vdev` on the device, but the Home Assistant/device API reports dev firmware as OTA-comparable version `0.0.0`. This keeps local flashes clearly recognizable while ensuring every published `X.Y.Z` firmware release is seen as an upgrade from a dev build.

Freshly provisioned, unpaired release firmware also performs a pre-pairing bootstrap update check after WiFi connects. It uses the GitHub Releases API for `pcvantol/djconnect-firmware`, follows the normal OTA screen/LED/sound/write flow when a newer release is available, and continues silently into pairing if the check fails. Dev builds are skipped so local development firmware is not replaced automatically.

During normal boot, the display shows the DJConnect tagline `Muziekbediening met karakter` and website URL `https://djconnect.dev` for at least three seconds before WiFi, setup/AP, charging or playback states take over. When the screen wakes from backlight-off normal mode, the first physical input shows the DJConnect splash briefly and then restores the active UI without executing the underlying action. The About screen also shows the website URL and keeps legal notices compact. The LED ring then plays one calm rainbow startup lap. During WiFi connect the LED ring shows a green chase animation; during Home Assistant pairing it breathes blue; during setup/AP it breathes rainbow. Turn-off/deep-sleep always plays a rainbow fade-out, while top-button soft reset plays a dedicated cue and two bright white LED flashes before reboot. During firmware write, the display shows `Firmware update in progress..`, the LED ring runs a fast purple animation and the device plays start, progress, complete or failure cues through the built-in speaker. Manual web uploads use the same on-device update screen and feedback. Before HA-triggered or bootstrap OTA download starts, the firmware releases wake-word/TFLite and active voice/audio resources so GitHub TLS has a large enough free heap block on non-PSRAM boards. The manifest `sha256` is required and verified while streaming for Home Assistant OTA; mismatches abort the update before reboot. OTA downloads tolerate slow GitHub/CDN bursts with a longer idle window while still aborting controlled when the stream genuinely stalls.

## GitHub Firmware Release

Release firmware can be prepared locally with `release.sh`. The public firmware repo `pcvantol/djconnect-firmware` also contains the release assets consumed by Home Assistant OTA.

The local release helper prepares a source release, injects the release version through PlatformIO build flags, updates/upgrades PlatformIO Core plus third-party project packages before building, creates ignored local `release/djconnect-lilygo-t-embed-s3-vX.Y.Z.bin` and `release/firmware_manifest.json` artifacts, commits source metadata, tags and pushes. Release-cycle documentation updates must also refresh `CHAT_BOOTSTRAP.md` so future Codex chats start with the current release, handoff and verification context. Release builds define `DJCONNECT_RELEASE_BUILD=1` and compile with explicit size-oriented `-Os` flags. Link-time optimization is intentionally not enabled because the current Arduino ESP32 / ESP-IDF 5.3 toolchain fails to link the application with `-flto`. The pushed git tag then triggers the GitHub Action, which performs the same dependency update step, builds and publishes the public firmware release in `pcvantol/djconnect-firmware`. The action verifies that the compiled firmware image contains the expected `vX.Y.Z` version tag before publishing the OTA asset and its `.sha256` file. GitHub release notes are extracted from the matching `CHANGELOG.md` section for the release tag, so each release requires a populated `## vX.Y.Z` changelog entry before publishing.

The embedded web portal lives in `src/WebPortal.cpp` as a PROGMEM raw literal.
Run `python3 scripts/minify_webportal.py` after changing the portal markup,
styles or scripts so the served portal remains compact without adding runtime
decompression to the no-PSRAM LilyGO path.

Dependency updates write `release/build-dependencies-before.txt`,
`release/build-dependencies-after.txt` and `release/build-dependencies.diff`
locally, and equivalent per-board reports in GitHub Actions artifacts. If the
diff shows upgraded frameworks, libraries or tools, update
`THIRD_PARTY_NOTICES.md` and `DESIGN_DECISIONS.md` before publishing the release.
These dependency reports are review/audit artifacts only; the public GitHub
firmware release must publish only board firmware binaries, their `.sha256`
files and the firmware manifest.
Before publishing firmware, also revalidate the embedded OTA TLS CA/certificate
bundle in `include/GitHubTls.h` against the current GitHub API and release-asset
redirect certificate chains.

Beta firmware uses the same flow with `--channel beta` or a `vX.Y.Z-beta` tag. Beta assets are named `djconnect-lilygo-t-embed-s3-beta-vX.Y.Z.bin`, the manifest is `firmware_manifest_beta.json`, and the GitHub release is marked as a prerelease.

Old public firmware releases, tags and workflow runs can be reviewed and pruned
with the separate dry-run first cleanup helper. Use `--keep-runs` when you want
to keep more recent GitHub Actions history than release/tag history.

```sh
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --dry-run
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --keep 1 --execute --yes
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --keep 1 --keep-runs 5 --execute --yes
scripts/cleanup_old_releases.sh --repo pcvantol/djconnect-firmware --channel beta --keep 1 --dry-run
```

```bash
./release.sh X.Y.Z
```

Preflight without changing files, building, committing, pushing or publishing:

```bash
./release.sh X.Y.Z --dry-run
```

Prepare a beta release:

```bash
./release.sh X.Y.Z --channel beta
./release.sh vX.Y.Z-beta --dry-run
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

Refresh build dependencies before a manual verification build:

```bash
scripts/update_build_dependencies.sh t_embed_cc1101
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

The native test binary covers pure logic such as battery estimation, menu option counts/values including log-level options, device command parsing, Home Assistant pairing/provisioning helpers and network timeout helper behavior. The release shell test covers `release.sh` syntax, asset snapshot checks, dry-run behavior, invalid version handling, beta release naming and the release cleanup helper dry-run path.

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
- PTT fails: check Home Assistant pairing, `ha_local_url`, `device_token`, the HA integration `/api/djconnect/voice` handler and HA Assist/TTS logs. The ESP should not report `Assist auth_invalid`; Assist WebSocket authentication belongs on the HA integration side.
- OTA fails: check battery/charging state, firmware URL, device type and network reachability.
- Device becomes sluggish: check logs for `Responsiveness: slow loop`, `free_heap`, `min_free_heap` and `largest_block`.

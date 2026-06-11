# DJConnect iOS/macOS App Sync Prompt / Handoff

This handoff is for building a new native iOS/macOS DJConnect client that uses
the same Home Assistant custom integration backend as the ESP32 firmware.

Use this as the sync prompt for a new Apple-client repo. The current ESP
firmware contract line is `v3.1.6`; Apple clients should follow the same
`3.1.x` Home Assistant integration protocol unless that backend contract is
changed deliberately.

The app should be functionally comparable to the ESP device at the Home
Assistant integration contract level, but it is not an ESP emulator. Use
`client_type` to identify the client family:

- iOS app: `ios`
- macOS app: `macos`
- ESP firmware remains: `esp32`

Do not use `device_type` for DJConnect client identity. `device_type` may only
appear as playback-output metadata if returned by the backend.

## Architecture

Home Assistant is the trusted DJConnect backend for:

- pairing;
- bearer-token lifecycle;
- backend playback commands;
- Spotify OAuth and future playback backend credentials;
- Assist/STT/TTS;
- OTA/update offers for device clients where applicable;
- native Home Assistant entities.

The iOS/macOS app owns:

- native UI;
- local app state;
- local audio recording if voice/PTT is implemented;
- local playback of returned DJ announcement audio, if desired;
- local notifications/menus/widgets, if desired.

The app must not store or request Spotify OAuth secrets, refresh tokens, client
secrets, Sonos credentials, Home Assistant long-lived access tokens, or playback
backend credentials. The only DJConnect credential owned by the app is its
DJConnect device bearer token issued by the integration.

## Identity

Use a stable device id per app installation.

Suggested format:

- iOS: `djconnect-ios-<stable-install-id>`
- macOS: `djconnect-macos-<stable-install-id>`

The suffix should be stable across app launches, but should reset if the user
explicitly resets DJConnect pairing in the app. Avoid exposing Apple account,
device serial, hostname, WiFi SSID, or other private identifiers in the id.

Recommended fields:

```json
{
  "device_id": "djconnect-ios-8F3A2C91B45D",
  "device_name": "DJConnect iPhone",
  "client_type": "ios",
  "firmware": "3.1.6",
  "app_version": "3.1.6",
  "platform": "ios"
}
```

For macOS:

```json
{
  "device_id": "djconnect-macos-8F3A2C91B45D",
  "device_name": "DJConnect Mac",
  "client_type": "macos",
  "firmware": "3.1.6",
  "app_version": "3.1.6",
  "platform": "macos"
}
```

The HA integration currently uses `firmware` as the common client version field
for protocol compatibility checks. App clients may also send `app_version`, but
must keep `firmware` populated unless the backend contract is changed.

## Version Contract

DJConnect clients and the HA integration must share the same `major.minor`
protocol version:

- HA `3.1.z` accepts clients `3.1.z`.
- Patch versions may differ.
- `0.0.0` is reserved as a dev-client escape hatch.

If HA returns HTTP `426` with `error: "version_mismatch"`, the app must not
reset pairing or discard the token. Show an update-required state and pause
command/voice retries until the user updates the app or integration.

The public ESP firmware manifest uses `min_ha_integration` derived from the
firmware major/minor line (`X.Y.Z` -> `X.Y.0`). Apple clients should apply the
same major/minor compatibility rule locally even though they do not consume ESP
OTA firmware assets.

Expected response:

```json
{
  "success": false,
  "error": "version_mismatch",
  "message": "DJConnect Home Assistant integration and device firmware major.minor versions must match.",
  "ha_version": "3.1.6",
  "ha_major_minor": "3.1",
  "firmware": "3.0.9",
  "firmware_major_minor": "3.0"
}
```

## Pairing Flow

The app should pair with the Home Assistant DJConnect integration, not directly
with Spotify or any playback backend.

The app needs:

- Home Assistant base URL, local or remote;
- DJConnect pairing token issued by the integration;
- DJConnect bearer token returned/stored by the integration.

Recommended user flow:

1. User enters/selects their Home Assistant URL.
2. App opens the DJConnect pairing/setup flow in Home Assistant, or receives a
   pairing code/deep link depending on the final UX.
3. Integration creates or returns a DJConnect device bearer token for the app
   runtime.
4. App stores only the DJConnect bearer token in Keychain.
5. App starts sending authenticated status and command payloads with
   `client_type`.

Bearer token storage:

- iOS: Keychain item scoped to the app.
- macOS: Keychain item scoped to the app/bundle id.
- Never log the token.
- Never include the token in diagnostics exports.

Auth headers for app -> HA:

```http
Authorization: Bearer <device_token>
X-DJConnect-Device-ID: <device_id>
Content-Type: application/json
```

For raw voice audio:

```http
Authorization: Bearer <device_token>
X-DJConnect-Device-ID: <device_id>
Content-Type: audio/wav
```

## Local App Web API For HA -> App

To be functionally comparable to the ESP firmware, the Apple client must also
offer a small local authenticated Web API for Home Assistant -> app traffic.
Without this, HA can receive app status and playback commands from the app, but
cannot push native entity commands, DJ responses, pairing callbacks, or local
state requests back to the running client.

Run a local HTTP server while the app is reachable on the LAN:

- macOS: run while the app/menu-bar helper is active; optionally launch at
  login for persistent availability.
- iOS/iPadOS: run while the app is foreground/active and has Local Network
  permission. Do not assume a background HTTP server remains reachable after
  suspension; HA must tolerate the app being temporarily unreachable.

Advertise the app with Bonjour/mDNS using the same service as ESP clients:

```text
service: _djconnect._tcp
hostname: <device_id>.local
url: http://<device_id>.local:<port>
TXT: name, device_id, version, paired, api, model, client_type
```

Use stable app device IDs:

```text
djconnect-ios-<stable-install-id>
djconnect-macos-<stable-install-id>
```

Open local endpoints:

```http
GET /api/device/info
GET /api/device/pairing-info
```

Protected local endpoints require:

```http
Authorization: Bearer <device_token>
```

Recommended protected endpoints:

```http
POST /api/device/pair
POST /api/device/command
POST /api/device/dj_response
POST /api/device/forget
```

`POST /api/device/reboot` and `POST /api/device/ota` are ESP-specific and
should not be implemented unless the Apple app has a real equivalent.

`GET /api/device/pairing-info` should return:

```json
{
  "device_id": "djconnect-ios-8F3A2C91B45D",
  "device_name": "DJConnect iPhone",
  "pair_code": "123456",
  "client_type": "ios",
  "firmware": "3.1.6",
  "app_version": "3.1.6",
  "local_url": "http://djconnect-ios-8F3A2C91B45D.local:18080"
}
```

For macOS, use `client_type:"macos"` and a `djconnect-macos-...` device id.
Never use `device_type` for identity.

`POST /api/device/pair` from HA should accept:

```json
{
  "pair_code": "123456",
  "device_id": "djconnect-ios-8F3A2C91B45D",
  "client_type": "ios",
  "device_token": "<device-token>",
  "ha_local_url": "http://192.168.1.x:8123",
  "ha_remote_url": "https://xxxx.ui.nabu.casa",
  "device_language": "nl",
  "language": "nl"
}
```

Rules:

- Accept only this app installation's own `device_id`.
- Accept only the expected `client_type` for the target app (`ios` or `macos`).
- Store only the DJConnect bearer token, HA URLs, language and lightweight
  DJConnect settings.
- Keep `ha_local_url` as the normal route for app -> HA status, command and
  voice calls; `ha_remote_url` is fallback/diagnostics, not the normal path.
- Do not erase the token automatically on a single HA -> app command failure.
- Return concise JSON errors for unauthorized, wrong device id, wrong
  client_type, missing token, or unsupported command.

`POST /api/device/command` is where HA native entities should control the app.
Suggested command scope:

- status request;
- playback controls mirrored into app state if relevant;
- language/theme/log-level updates;
- voice/PTT enable flags if exposed as HA entities;
- diagnostics/log export trigger if explicitly supported.

`POST /api/device/dj_response` should let HA push DJ announcement text and optional
audio URL to the app UI:

```json
{
  "text": "Daar gaan we.",
  "audio_url": "http://homeassistant.local:8123/api/djconnect/tts/token.mp3"
}
```

The app may display the text and play returned WAV/MP3 audio locally if enabled.
If playback is disabled or unsupported, acknowledge the command and show the
text-only response.

## Status Endpoint

Post client status to:

```http
POST /api/djconnect/status
```

Minimum payload:

```json
{
  "device_id": "djconnect-ios-8F3A2C91B45D",
  "client_type": "ios",
  "ha_pairing_status": "paired",
  "firmware": "3.1.6",
  "app_version": "3.1.6",
  "state": "online",
  "status": "online",
  "battery_percent": 85,
  "language": "nl",
  "theme": "dark",
  "log_level": "info"
}
```

Optional app-specific fields:

```json
{
  "platform": "ios",
  "os_version": "18.5",
  "app_build": "30900",
  "local_audio_supported": true,
  "voice_supported": true,
  "screen_state": "on",
  "network_type": "wifi"
}
```

Status responses may include:

```json
{
  "success": true,
  "client_type": "ios",
  "device_language": "nl",
  "language": "nl",
  "backend_available": true,
  "playback": {}
}
```

Use `device_language`/`language` to update app UI language only if the app
supports remote language sync. Otherwise keep it as informational state.

Status is authoritative for Home Assistant entities that represent the app
client. Command payloads must not be used as partial status snapshots.

## Playback Commands

Send generic playback commands to:

```http
POST /api/djconnect/command
```

All command payloads must include `device_id` and `client_type`.
Keep command payloads focused on playback commands and client identity. Do not
send partial device-status snapshots in `/api/djconnect/command`; use
`/api/djconnect/status` as the authoritative source for client status and
settings mirrored into Home Assistant entities.

Examples:

```json
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"status"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"devices"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"queue"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"playlists"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"pause"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"play"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"next"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"previous"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_volume","value":35}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_shuffle","value":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_repeat","value":"context"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"start_liked_proxy","play":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"start_playlist","value":"spotify:playlist:...","play":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_output","value":"iPhone","play":true}
```

Expected success shape:

```json
{
  "success": true,
  "playback": {
    "has_playback": true,
    "is_playing": true,
    "track_name": "Song",
    "artist_name": "Artist",
    "album_image_url": "https://...",
    "progress_ms": 12345,
    "duration_ms": 180000,
    "volume_percent": 32,
    "shuffle": false,
    "repeat_state": "off",
    "device": {
      "id": "spotify-device-id",
      "name": "iPhone",
      "type": "Smartphone",
      "active": true,
      "supports_volume": true,
      "volume_percent": 32
    }
  }
}
```

Backend unavailable is not an auth failure:

```json
{
  "success": false,
  "error": "backend_unavailable",
  "message": "Spotify authorization has expired or was revoked. Reauthorize DJConnect.",
  "backend_available": false,
  "playback": {}
}
```

When backend unavailable:

- keep pairing/token;
- show playback backend unavailable;
- do not send the user through app pairing again;
- throttle retries enough to avoid UI churn.

When HA returns 401/403:

- mark pairing stale/unauthorized;
- keep token until the user explicitly resets pairing;
- show setup-again guidance.

When HA returns 404:

- treat as integration route missing or stale pairing;
- do not erase Keychain automatically;
- show integration/setup recovery.

## Voice/PTT

If implementing push-to-talk:

1. App records mono PCM WAV.
2. App uploads raw WAV to HA:

```http
POST /api/djconnect/voice
Content-Type: audio/wav
Authorization: Bearer <device_token>
X-DJConnect-Device-ID: <device_id>
```

3. HA owns STT, Assist, playback action and TTS.
4. HA returns DJ text and optional audio URL.
5. App displays text and may play returned WAV/MP3 audio locally.

Expected response:

```json
{
  "success": true,
  "text": "Daar gaan we.",
  "dj_text": "Daar gaan we.",
  "audio_url": "http://homeassistant.local:8123/api/djconnect/tts/token.mp3",
  "audio_type": "mp3"
}
```

Rules:

- Do not connect directly to Home Assistant Assist WebSocket from the app for
  DJConnect PTT unless the backend contract is explicitly changed.
- Do not call OpenAI or Spotify directly from the app for DJConnect commands.
- Do not log temporary `audio_url` tokens.
- If returned audio cannot be played, show text-only response.
- If a user cancels the PTT/DJ-announcement flow locally, the app may ignore any
  late HA response from the in-flight request.
- If implementing wake-word support on Apple platforms, keep detection local to
  the app/device where Apple platform policy permits it, then start the same
  `/api/djconnect/voice` WAV upload flow. HA should not need a separate
  wake-word endpoint.

## OTA And Device Updates

ESP OTA is board-specific and uses the public firmware manifest `firmwares[]`
entries for:

- `lilygo-t-embed-s3`
- `esp32-s3-box-3`

Apple clients must not request or install ESP firmware assets. If the Home
Assistant integration exposes update information to Apple clients, it should be
app-store/TestFlight/direct-download metadata, not `/api/device/ota` with ESP
firmware binaries.

## App Settings

The ESP has device settings such as screen brightness, LED and speaker cue
volume. The iOS/macOS app should not copy those settings blindly.

Suggested app-owned settings:

- HA URL selection;
- pairing reset;
- language;
- theme;
- voice/PTT enabled;
- local response audio enabled;
- diagnostics export;
- log level.

If app settings should be mirrored into HA entities, post them in status under
clear app-specific keys. Avoid reusing ESP-only settings like
`screen_brightness` unless the app truly implements equivalent behavior.

## UI Parity Goals

Functional parity with the ESP device should include:

- pairing/setup state;
- Home Assistant connection state;
- playback now-playing view;
- play/pause, previous, next;
- volume 0-60;
- shuffle toggle;
- repeat triple state: `off`, `track`, `context`;
- output selector;
- queue view;
- playlists/liked proxy start;
- DJ/voice response view if PTT is implemented;
- backend unavailable and version mismatch states.

iOS/macOS-specific UX may add:

- menu bar control on macOS;
- lock screen/live activity on iOS if appropriate;
- media key integration, if it maps cleanly to DJConnect commands;
- widgets/shortcuts later.

## Security And Privacy

Never log:

- DJConnect device bearer token;
- Home Assistant tokens;
- Spotify refresh token;
- OAuth client secret;
- WiFi password;
- temporary TTS/audio URLs.

Diagnostics must redact:

- `Authorization`;
- `device_token`;
- any `token`;
- `audio_url` query strings;
- private HA URLs if the user chooses anonymized export.

## New Repo Suggested Shape

Suggested repository name:

```text
djconnect-apple
```

Suggested top-level structure:

```text
DJConnectApple/
  Package.swift or DJConnectApple.xcodeproj
  Sources/
    DJConnectCore/
      DJConnectClient.swift
      DJConnectModels.swift
      DJConnectPairing.swift
      DJConnectKeychain.swift
      DJConnectVoice.swift
    DJConnectIOS/
    DJConnectMac/
  Tests/
    DJConnectCoreTests/
  README.md
  PRIVACY.md
```

Core module responsibilities:

- build authenticated requests;
- serialize status/command/voice payloads;
- parse playback responses;
- classify errors: backend unavailable, auth stale, version mismatch,
  not configured, network;
- store and clear bearer token via a platform abstraction.

Do not put SwiftUI view logic into the HTTP client.

## Acceptance Criteria

- App pairs with the existing `djconnect` HA integration.
- App status posts include `client_type` as `ios` or `macos`.
- App command posts include `client_type` as `ios` or `macos`.
- App does not send `device_type` for identity.
- HA backend playback commands work without any Spotify credentials in the app.
- Backend unavailable does not reset pairing.
- HTTP 426 version mismatch shows update-required UI and keeps pairing.
- 401/403/404 show stale pairing/setup recovery and keep token until user reset.
- Voice/PTT, if implemented, uploads raw WAV to `/api/djconnect/voice`.
- Apple clients do not consume ESP OTA firmware manifest assets.
- No secrets appear in logs or diagnostics.
- iOS and macOS clients can coexist with ESP32 clients in the same HA backend.

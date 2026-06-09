# Codex Prompt: Synchronize Home Assistant Integration With DJConnect ESP Firmware

Werk in de bestaande Home Assistant custom integration repo voor `djconnect`.

Doel:
Synchroniseer de Home Assistant integration met de actuele DJConnect ESP firmware release `v3.0.10`.


## 0. Repository/Release Hygiene

- ESP source repo is `pcvantol/djconnect-esp32`.
- Public OTA firmware repo is `pcvantol/djconnect-firmware`.
- Firmware binaries/manifests must be consumed from `djconnect-firmware`; the ESP source repo should not be treated as the OTA asset host.
- Current firmware release/tag baseline is `v3.0.10`; do not reference old 2.x firmware assets or tags.

Belangrijke architectuur:

- ESP is geen Spotify Connect speaker/player.
- ESP bewaart geen backend OAuth of playback credentials.
- ESP doet geen directe Spotify Web API calls.
- ESP stuurt generieke playback commands naar Home Assistant.
- Home Assistant is trusted backend voor playback, credentials, Assist/STT/TTS, OTA, native entities en optionele `media_player`.
- ESP speaker is alleen voor local cues en DJ/voice response audio.

## 1. Pairing Contract

Controleer pairing flow:

- Integration domain: `djconnect`.
- ESP device id format: `djconnect-lilygo-XXXXXXXXXXXX`.
- ESP mDNS service: `_djconnect._tcp`.
- ESP local pairing/info endpoints:
  - `GET /api/device/info`
  - `GET /api/device/pairing-info`
  - `POST /api/device/pair`

Belangrijk:

- HA mag een lokaal device_token voorbereiden, maar moet pairingstatus niet als `paired` rapporteren totdat de ESP tokenopslag bevestigt.
- Een aangemaakte HA config entry, device registry entry of set entities betekent nog niet dat de ESP gepaired is. Als het ESP display na de HA flow nog de pairing code toont, is HA pairing hooguit `pending`.
- Bij een 6-cijferige setupcode kent HA de echte ESP device-id nog niet. Resolve eerst de ESP URL via manual URL, `_djconnect._tcp` mDNS of single visible DJConnect mDNS service, roep daarna `GET /api/device/pairing-info` aan, verifieer dat `pair_code` overeenkomt en leer de echte `djconnect-lilygo-XXXXXXXXXXXX` `device_id`.
- Gebruik de echte `device_id` uit `/api/device/pairing-info` in de daaropvolgende `POST /api/device/pair`. Stuur nooit een tijdelijke `djconnect-<6-cijferige-code>` als `device_id` naar de ESP.
- Als `/api/device/pairing-info` niet bereikbaar is of de code niet matcht, rond de config flow niet af als succesvol gepaired; toon/retry als pending/recoverable pairing failure.
- `/api/device/pair` callback naar ESP moet `device_token` plus `ha_local_url` en/of `ha_remote_url` sturen.
- `ha_local_url` is de LAN URL die ESP eerst probeert, bijvoorbeeld `http://homeassistant.local:8123`.
- `ha_remote_url` is de optionele Nabu Casa/cloud URL die ESP gebruikt als local niet bereikbaar is.
- Pairing zonder `ha_local_url` en zonder `ha_remote_url` moet als configuratiefout worden behandeld.
- Treat ESP pairing as `pending` totdat een authenticated ESP status/command/voice post naar HA succesvol verwerkt is met dezelfde bearer token.
- Als HA 401/403/404 teruggeeft op ESP status/command, pairing is stale/invalid.
- Als playback backend tijdelijk niet beschikbaar is, dat is geen pairing failure.

## 2. HA Status Endpoint

ESP post periodiek en bij boot:

`POST /api/djconnect/status`

Headers:

- `Authorization: Bearer <device_token>`
- `X-DJConnect-Device-ID: <device_id>`
- `Content-Type: application/json`

Payload bevat onder andere:

```json
{
  "device_id": "djconnect-lilygo-XXXXXXXXXXXX",
  "state": "online",
  "status": "online",
  "ota_state": "idle",
  "update_state": "idle",
  "battery_percent": 90,
  "battery_mv": 4120,
  "charging": false,
  "wifi_rssi": -55,
  "firmware": "3.0.10",
  "language": "nl",
  "device_language": "nl",
  "theme": "dark",
  "log_level": "info",
  "ha_pairing_status": "paired",
  "local_url": "http://djconnect-lilygo-XXXXXXXXXXXX.local",
  "brightness": 100,
  "screen_brightness": 100,
  "screen_brightness_percent": 100,
  "screen_dim_timeout": 60000,
  "screen_dim_timeout_ms": 60000,
  "screen_off_timeout_ms": 60000,
  "turn_off_after": 300000,
  "turn_off_after_ms": 300000,
  "cue_volume": 100,
  "speaker_volume": 100,
  "speaker_volume_percent": 100,
  "screen_state": "on",
  "screen_brightness_level": 100,
  "led_state": "off",
  "settings": {
    "brightness": 100,
    "screen_dim_timeout_ms": 60000,
    "cue_volume": 100,
    "screen_brightness_percent": 100,
    "screen_off_timeout_ms": 60000,
    "turn_off_after_ms": 300000,
    "speaker_volume_percent": 100,
    "language": "nl",
    "theme": "dark",
    "log_level": "info"
  },
  "screen": {
    "state": "on",
    "brightness_level": 100
  },
  "led": {
    "state": "off"
  },
  "playback_configured": true,
  "free_heap": 123456,
  "uptime_ms": 1234567
}
```

Taken:

- Parse both top-level fields and nested `settings`, `screen`, `led`.
- Use these status fields to update native HA entities, especially:
  - screen brightness
  - screen timeout
  - turn-off timeout
  - speaker cue volume
  - language
  - theme
  - log level
  - screen state
  - LED state
- Zorg dat deze entities niet op min/default blijven staan na ESP reboot.
- OTA/update entity moet `ota_state/update_state=idle` plus firmware version verwerken om `updating` te clearen na reboot.

## 3. Playback Command Proxy

ESP stuurt playback commands naar:

`POST /api/djconnect/command`

Headers:

- `Authorization: Bearer <device_token>`
- `X-DJConnect-Device-ID: <device_id>`
- `Content-Type: application/json`

Payload examples:

```json
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"status"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"devices"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"queue"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"playlists"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"pause"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"play"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"next"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"previous"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_volume","value":35}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_output","value":"iPhone","play":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"start_liked_proxy"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"start_playlist","value":"spotify:playlist:..."}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"play_context_at","value":{"context_uri":"spotify:playlist:...","offset_uri":"spotify:track:..."}}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_shuffle","value":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_repeat","value":"track"}
```

Response contract:

- Auth/pairing failure:
  - HTTP 401/403/404.
  - ESP marks pairing stale/red.
- Playback/backend unavailable:
  - Return HTTP 200, not 503.
  - Return JSON:

```json
{
  "success": false,
  "backend_available": false,
  "error": "backend_unavailable",
  "message": "Playback backend unavailable"
}
```

- Successful status response should include playback state:

```json
{
  "success": true,
  "backend_available": true,
  "playback": {
    "has_playback": true,
    "is_playing": true,
    "track_name": "...",
    "artist_name": "...",
    "album_image_url": "...",
    "progress_ms": 12345,
    "duration_ms": 180000,
    "volume_percent": 35,
    "shuffle": false,
    "repeat_state": "off",
    "device": {
      "id": "...",
      "name": "iPhone",
      "type": "Smartphone",
      "supports_volume": true,
      "volume_percent": 35
    }
  }
}
```

Taken:

- Never return HTTP 503 for normal playback backend unavailable during command/status, because ESP interprets HTTP 5xx as playback connection error/cooldown.
- Keep backend unavailable as JSON failure on 200.
- Keep 401/403/404 only for actual auth/pairing invalid cases.
- `command=status` moet zo snel mogelijk na ESP boot kunnen antwoorden, want ESP v3.0.10 forceert direct een playback status poll na HA setup.
- Queue/devices/playlists should return `success:true` with empty arrays if backend is reachable but no data is available.
- `command=queue` should include a top-level `context_uri`/`contextUri` when playback has a playlist/album/context. ESP uses this for `play_context_at` from Up Next.
- Queue items should include per-item album art where available. Supported ESP field names are `album_image_url`, `albumImageUrl`, `image_url`, `imageUrl` or `thumbnail_url`. Example:

```json
{
  "success": true,
  "context_uri": "spotify:playlist:...",
  "queue": [
    {
      "title": "Track",
      "subtitle": "Artist",
      "uri": "spotify:track:...",
      "album_image_url": "https://..."
    }
  ]
}
```

- The ESP does not download queue thumbnails. It passes image URLs through `/api/queue`; the web browser lazy-loads images only when the Up Next panel is visible.
- Avoid spamming `/api/device/pair` callbacks while normal playback commands are running; use a debounced settings sync path if needed.
- Do not call ESP `POST /api/device/pair` as a generic status/settings synchronization endpoint after the device is already paired. Use it only for initial config-flow pairing, explicit re-pair/token rotation, or recovery. Use `POST /api/device/command` for settings changes and `/api/djconnect/status` responses for state acknowledgement.

## 4. Local ESP Device Command API

HA stuurt device settings naar:

`POST /api/device/command`

Headers:

- `Authorization: Bearer <device_token>`
- `Content-Type: application/json`

Canonical commands:

```json
{"command":"status"}
{"command":"screen_brightness","value":75}
{"command":"screen_dim_timeout","value":60000}
{"command":"turn_off_after","value":300000}
{"command":"speaker_volume","value":50}
{"command":"language","value":"nl"}
{"command":"theme","value":"dark"}
{"command":"log_level","value":"info"}
{"command":"dj_response","text":"Daar gaan we.","audio_url":"http://homeassistant.local:8123/api/djconnect/tts/example.mp3"}
```

Taken:

- HA native entities should call `/api/device/command` for device-local settings.
- After command success, update HA entity state from command response if present, otherwise refresh from next `/api/djconnect/status`.
- Handle ESP 401/403/404 as stale pairing.
- Do not send backend OAuth credentials or playback secrets to ESP.

## 5. PTT / DJ Response

Physical PTT flow:

ESP records WAV
→ `POST /api/djconnect/voice` raw `audio/wav`
→ HA does STT/backend/TTS
→ HA returns or posts DJ text plus optional audio URL
→ ESP displays text and plays WAV/MP3 locally.

Rules:

- Do not require a HA long-lived token on ESP.
- Do not use direct ESP Assist WebSocket auth.
- HA owns Assist/STT/TTS configuration and should return clear JSON errors for missing provider/config.
- `audio_url` may be WAV or MP3.
- ESP speaker is only for cues and DJ response audio, not backend music.

## 6. Optional HA Media Player Entity

If implementing a `media_player`:

- It represents backend playback state controlled by DJConnect, not the ESP speaker as a music sink.
- State: playing/paused/idle/unavailable from HA backend playback state.
- Attributes: title, artist, album art, output/source, volume, supported features.
- Commands:
  - play/pause
  - next/previous
  - volume set
  - source/output selection
  - playlist/media start
- Translate those commands to backend playback actions in HA and/or ESP generic command routes as appropriate.
- Do not imply that backend music audio is played through the ESP speaker.

## 7. Tests To Add/Update

Add or update tests for:

- Pairing remains pending until ESP confirms token storage.
- Six-digit setup-code pairing fetches ESP `/api/device/pairing-info`, verifies the displayed code, learns the real `djconnect-lilygo-XXXXXXXXXXXX` device id and uses that real id in `POST /api/device/pair`.
- If `/api/device/pair` is never observed by the ESP and the ESP display stays on the pairing screen, HA must keep/recover `pending` state and retry explicit pairing instead of claiming success.
- 401/403/404 marks pairing stale.
- HTTP 200 with `success:false, backend_available:false` does not mark pairing stale.
- `/api/djconnect/status` parses nested/top-level settings and updates HA entities away from min/default.
- `command=status` after ESP boot returns promptly.
- OTA status clears from updating after ESP posts `ota_state/update_state=idle` plus firmware.
- No backend playback credentials are sent to or stored on ESP.
- `/api/device/command` canonical setting names work.
- DJ response text + WAV URL and text + MP3 URL both work.
- Queue/devices/playlists empty-but-successful responses do not become HTTP 503.

## 8. Acceptance Criteria

- ESP v3.0.10 can pair with HA integration without repeated stale-pairing loops.
- After a 6-digit setup-code flow, ESP logs `Home Assistant direct pairing stored: device_token=present`, exits the pairing screen and after reboot logs `Home Assistant pairing: paired`.
- After ESP reboot, HA status and playback command flow make the ESP playback music-note indicator green/grey/red correctly before any physical control action.
- HA brightness/speaker volume/timeouts/language/theme/log-level entities reflect ESP state after reboot/status post.
- Playback backend unavailable shows playback music-note error but keeps HA pairing intact.
- Backend credentials remain only in Home Assistant.

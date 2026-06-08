# Codex Prompt: Synchronize Home Assistant Integration With SpotifyDJ ESP Firmware

Werk in de bestaande Home Assistant custom integration repo voor `spotify_dj`.

Doel:
Synchroniseer de Home Assistant integration met de actuele SpotifyDJ ESP firmware release `v2.9.28`.

Belangrijke architectuur:

- ESP is geen Spotify Connect speaker/player.
- ESP bewaart geen Spotify OAuth/client_id/refresh_token, MQTT credentials of backend credentials.
- ESP doet geen directe Spotify Web API calls.
- ESP stuurt generieke playback commands naar Home Assistant.
- Home Assistant is trusted backend voor playback, credentials, Assist/STT/TTS, OTA, native entities en optionele `media_player`.
- ESP speaker is alleen voor local cues en DJ/voice response audio.
- MQTT is volledig verwijderd.

## 1. Pairing Contract

Controleer pairing flow:

- Integration domain: `spotify_dj`.
- ESP device id format: `spotifydj-lilygo-XXXXXXXXXXXX`.
- ESP mDNS service: `_spotifydj._tcp`.
- ESP local pairing/info endpoints:
  - `GET /api/device/info`
  - `GET /api/device/pairing-info`
  - `POST /api/device/pair`

Belangrijk:

- HA mag een lokaal device_token voorbereiden, maar moet pairingstatus niet als `paired` rapporteren totdat de ESP tokenopslag bevestigt.
- `/api/device/pair` callback naar ESP mag `ha_url`, `device_token`, `device_language`/`language` en Assist pipeline settings sturen.
- Treat ESP pairing as `pending` totdat een authenticated ESP status post naar HA succesvol verwerkt is.
- Als HA 401/403/404 teruggeeft op ESP status/command, pairing is stale/invalid.
- Als playback backend tijdelijk niet beschikbaar is, dat is geen pairing failure.

## 2. HA Status Endpoint

ESP post periodiek en bij boot:

`POST /api/spotify_dj/status`

Headers:

- `Authorization: Bearer <device_token>`
- `X-SpotifyDJ-Device-ID: <device_id>`
- `Content-Type: application/json`

Payload bevat onder andere:

```json
{
  "device_id": "spotifydj-lilygo-XXXXXXXXXXXX",
  "state": "online",
  "status": "online",
  "ota_state": "idle",
  "update_state": "idle",
  "battery_percent": 90,
  "battery_mv": 4120,
  "charging": false,
  "wifi_rssi": -55,
  "firmware": "2.9.28",
  "language": "nl",
  "device_language": "nl",
  "theme": "dark",
  "log_level": "info",
  "ha_pairing_status": "paired",
  "local_url": "http://spotifydj-lilygo-XXXXXXXXXXXX.local",
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
  "spotify_configured": true,
  "free_heap": 123456,
  "uptime_ms": 1234567
}
```

Taken:

- Parse both top-level compatibility fields and nested `settings`, `screen`, `led`.
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

`POST /api/spotify_dj/command`

Headers:

- `Authorization: Bearer <device_token>`
- `X-SpotifyDJ-Device-ID: <device_id>`
- `Content-Type: application/json`

Payload examples:

```json
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"status"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"devices"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"queue"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"playlists"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"pause"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"play"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"next"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"previous"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"set_volume","value":35}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"set_output","value":"iPhone","play":true}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"start_liked_proxy"}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"start_playlist","value":"spotify:playlist:..."}
{"device_id":"spotifydj-lilygo-XXXXXXXXXXXX","command":"set_play_mode","value":"shuffle"}
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
- `command=status` moet zo snel mogelijk na ESP boot kunnen antwoorden, want ESP v2.9.28 forceert direct een playback status poll na HA setup.
- Queue/devices/playlists should return `success:true` with empty arrays if backend is reachable but no data is available.
- Avoid spamming `/api/device/pair` callbacks while normal playback commands are running; use a debounced settings sync path if needed.
- Do not call ESP `POST /api/device/pair` as a generic status/settings synchronization endpoint after the device is already paired. Use it only for initial config-flow pairing, explicit re-pair/token rotation, or recovery. Use `POST /api/device/command` for settings changes and `/api/spotify_dj/status` responses for state acknowledgement.

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
{"command":"dj_response","text":"Daar gaan we.","audio_url":"http://homeassistant.local:8123/api/spotify_dj/tts/example.mp3"}
```

Taken:

- HA native entities should call `/api/device/command` for device-local settings.
- After command success, update HA entity state from command response if present, otherwise refresh from next `/api/spotify_dj/status`.
- Handle ESP 401/403/404 as stale pairing.
- Do not send Spotify OAuth, MQTT or backend secrets to ESP.

## 5. PTT / DJ Response

Physical PTT flow:

ESP records WAV
→ `POST /api/spotify_dj/voice` raw `audio/wav`
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

- It represents backend playback state controlled by SpotifyDJ, not the ESP speaker as a music sink.
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
- 401/403/404 marks pairing stale.
- HTTP 200 with `success:false, backend_available:false` does not mark pairing stale.
- `/api/spotify_dj/status` parses nested/top-level settings and updates HA entities away from min/default.
- `command=status` after ESP boot returns promptly.
- OTA status clears from updating after ESP posts `ota_state/update_state=idle` plus firmware.
- No Spotify/MQTT credentials are sent to or stored on ESP.
- `/api/device/command` canonical setting names work.
- DJ response text + WAV URL and text + MP3 URL both work.
- Queue/devices/playlists empty-but-successful responses do not become HTTP 503.

## 8. Acceptance Criteria

- ESP v2.9.28 can pair with HA integration without repeated stale-pairing loops.
- After ESP reboot, HA status and playback command flow make the ESP `S` indicator green/grey/red correctly before any physical control action.
- HA brightness/speaker volume/timeouts/language/theme/log-level entities reflect ESP state after reboot/status post.
- Playback backend unavailable shows playback/S error but keeps HA pairing intact.
- Backend credentials remain only in Home Assistant.
- MQTT is not present anywhere in options, diagnostics, entities or docs.

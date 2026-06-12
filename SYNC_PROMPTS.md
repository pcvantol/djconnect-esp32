# DJConnect Sync Prompts

This is the canonical cross-repo sync prompt bundle for DJConnect. Keep this
file byte-for-byte identical in every DJConnect repo so any repo can be used as
the source for synchronizing the others.

Canonical repo locations:

- Home Assistant integration: `pcvantol/djconnect`
- Apple app: `pcvantol/djconnect-app`
- ESP firmware: `pcvantol/djconnect-esp32`
- Website/docs: `pcvantol/djconnect-website`
- Raspberry Pi client: `pcvantol/djconnect-pi`

## How To Sync This File

From any checked-out DJConnect repo, copy its `SYNC_PROMPTS.md` to the sibling
repos that are present locally:

```sh
cp SYNC_PROMPTS.md ../djconnect/SYNC_PROMPTS.md
cp SYNC_PROMPTS.md ../djconnect-app/SYNC_PROMPTS.md
cp SYNC_PROMPTS.md ../djconnect-esp32/SYNC_PROMPTS.md
cp SYNC_PROMPTS.md ../djconnect-website/SYNC_PROMPTS.md
cp SYNC_PROMPTS.md ../djconnect-pi/SYNC_PROMPTS.md
```

If a sibling repo is not present, or the destination is the current repo, skip
that copy. After syncing, run `git diff --check` in each touched repo and
commit the updated `SYNC_PROMPTS.md` there.

## Current Protocol Line

The current shared protocol/release line is `3.1.x`; this bundle was last
aligned after Apple app release `v3.1.13`. DJConnect clients on the `3.1.x`
line are compatible with Home Assistant integration versions `>=3.1.0` and
`<3.2.0`.

---

## Cross-Repo Quick Prompts

Use these prompts when handing work between the Home Assistant integration,
Apple app, ESP firmware, Raspberry Pi client, and website/docs repos.

Canonical repo locations:

- Home Assistant integration: `pcvantol/djconnect`
- Apple app: `pcvantol/djconnect-app`
- ESP firmware: `pcvantol/djconnect-esp32`
- Website/docs: `pcvantol/djconnect-website`
- Raspberry Pi client: `pcvantol/djconnect-pi`

## Home Assistant Integration

```text
Sync the DJConnect Home Assistant integration with the Apple app and ESP
client contracts.

Requirements:
- Treat iOS/macOS/Raspberry Pi as app-like clients, not ESP hardware devices.
- Pair app-like clients through POST /api/djconnect/pair. For Raspberry Pi, this is
  the primary pairing path; do not try to call a Pi-local /api/device/pair
  endpoint during initial pairing.
- Pair ESP clients through their local /api/device/pair flow after resolving
  /api/device/pairing-info and verifying the visible pair_code.
- Accept stable device_id, device_name, client_type, firmware, app_version,
  platform and optional capabilities. Raspberry Pi status/pairing payloads may
  advertise capabilities such as touch=true, voice=false, local_audio=false and
  local_dj_response_endpoint=false.
- Accept the app-generated code as pair_code, pairing_code, or pairing_token.
- Return a DJConnect bearer token on success. The current compatible field is
  device_token; bearer_token and token may also be returned.
- Return ha_local_url during successful app pairing. Do not return
  device_language/language for iOS, macOS or Raspberry Pi clients; those
  clients determine their UI language locally.
- Keep cloud/remote URLs out of Apple app runtime traffic; cloud URLs are only
  needed by Home Assistant-owned Spotify OAuth config flows.
- When pairing an app-like client, ask for or use the Client API URL shown in
  the client pairing sheet. Do not assume a changing Bonjour hostname remains
  the canonical callback target after pairing.
- Implement full HA-side mDNS autodiscovery for Raspberry Pi clients in the
  pairing config-flow. Browse Bonjour/mDNS service `_djconnect._tcp`, resolve
  each service, validate `client_type=raspberry_pi` against device IDs shaped
  `djconnect-raspberry-pi-XXXXXXXXXXXX`, build the local Client API URL from
  service address/port or `local_url`, then always probe
  `GET /api/device/pairing-info` when the URL is reachable. Pairing-info is
  authoritative for `local_url`, `device_id`, `client_type`, `device_name`,
  `pair_code`, `version/app_version/firmware` and `paired`.
- The HA pairing form must prefill Raspberry Pi `Client API URL`,
  `client_type=raspberry_pi`, `device_name`, stable `device_id` and visible
  `pair_code` from pairing-info. If exactly one Pi is discovered, select it by
  default but still require user confirmation; if multiple clients are found,
  show a discovered-client selector with useful labels. Discovery is
  convenience only and must never mark a device paired by itself.
- If Pi mDNS TXT is visible but `/api/device/pairing-info` fails, show the
  discovered client as reachable-by-mDNS but not verified, keep manual Client
  API URL entry available, and surface a clear pairing error instead of silently
  falling back to `djconnect-{pair_code}`. Do not create a second HA entry when
  the discovered Pi `device_id` is already configured; guide the user to reset
  or re-pair that existing client.
- Add/keep HA tests for Raspberry Pi discovery: service TXT acceptance,
  pairing-info override, config-flow prefill for one Pi, selector behavior for
  multiple clients, duplicate `device_id` handling, pairing-info failure
  fallback, and proof that Pi pairing uses the stable discovered
  `djconnect-raspberry-pi-XXXXXXXXXXXX` instead of `djconnect-{pair_code}`.
- Return ha_version or ha_major_minor on status/command responses so Apple
  clients can enforce the matching major.minor contract.
- Apple clients host local /api/device/* endpoints for HA -> client traffic,
  but must not implement ESP-only reboot or OTA routes. Raspberry Pi display
  clients may be outbound-only and must advertise capabilities so HA does not
  require local voice, audio, or dj_response endpoints.
- Persist client_type as ios, macos, raspberry_pi, or esp32. Do not
  reintroduce device_type.
- Authenticated status/command/voice routes must accept Authorization: Bearer
  plus X-DJConnect-Device-ID.
- Support Apple app current-track seeking through
  `command:"seek_relative"` with integer millisecond offsets. Positive values
  seek forward, negative values seek backward. ESP clients may omit this UI.
- Validate that client_type matches the device_id prefix/model family:
  ios -> djconnect-ios-*, macos -> djconnect-macos-*, raspberry_pi -> djconnect-raspberry-pi-*, esp32 -> ESP
  model-specific ids such as djconnect-lilygo-t-embed-s3-*.
- During app pairing, 401/403 code mismatch responses stop polling, keep the
  visible app code, and do not rotate device_id automatically.
- Create native HA entities for paired app-like clients when status is
  received, including outbound-only Raspberry Pi clients that never expose
  /api/device/* endpoints.
- Create only client/runtime and backend/playback entities for ios, macos, and
  raspberry_pi clients; do not create ESP-only battery, Wi-Fi RSSI, screen
  state, LED state, screen brightness/timeout, speaker volume, device language,
  auto-off, theme/log-level, firmware OTA, or reboot entities for app-like
  clients. Raspberry Pi local settings such as screen blanking, logging and
  update channel are client-owned and should not be modeled as ESP hardware
  entities unless a future Pi-specific HA entity design is explicitly added.
- Support App Store review by allowing Apple clients to enter local Demo Mode
  without HA; Demo Mode must not create HA devices/entities, tokens, or backend
  traffic. Local sample DJ announcement audio/text in Demo Mode is app-local and
  is not proof of HA voice validation.
- Return HTTP 426 version_mismatch when client and HA major.minor protocol
  versions do not match; do not treat this as stale auth.
- Return backend_unavailable as HTTP 200 success:false with
  backend_available:false, not as HTTP 503.
```

## Apple App

```text
Sync the DJConnect Apple app with the Home Assistant integration contract.

Requirements:
- Keep one stable device_id per app installation across normal launches.
- Reset Pairing clears the DJConnect bearer token, rotates the app pairing
  code, and creates a fresh device_id for a new setup.
- Pair by polling POST /api/djconnect/pair with pair_code, pairing_code, and
  pairing_token set to the same app-generated code.
- Store only the returned DJConnect bearer token in Keychain and persist
  ha_local_url, device_id, and client_type.
- Expose local /api/device/info, pairing-info, pair, command, dj_response, and
  forget routes for HA -> app traffic; do not expose ESP-only reboot/OTA.
- Send device_id, client_type, firmware, app_version, device_name, ha_local_url,
  and local_url on status payloads. Send device_id and client_type on command
  payloads. Always use the local Home Assistant URL for app-to-HA traffic.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping the
  token until explicit user reset.
- Treat 401/403 during unauthenticated pairing polling as code/setup mismatch:
  stop polling, keep the visible app code, and ask the user to re-enter it.
- Show first-run onboarding once per installation with the Home Assistant setup
  link and Spotify Premium requirement. Do not request Spotify credentials in
  the app.
- While unpaired, block runtime UI with a pairing sheet that shows the
  DJConnect banner, copyable Client API url, copyable app-generated pairing
  code, progress/status, and a green success state with `Let's Start!`.
- Keep the Client API url shown during pairing pinned locally until explicit
  pairing reset.
- Offer Demo Mode from the unpaired pairing sheet for App Store review and UI
  inspection without a Home Assistant backend. Demo Mode must use local sample
  data and must not store a bearer token.
- Fresh installs should default the Home Assistant URL field to
  `http://homeassistant.local:8123`, while paired runtime traffic must use the
  returned `ha_local_url`.
- Use the shared DJConnect blue/purple gradient canvas across iOS, iPadOS, and
  macOS screens.
- Settings may preflight Microphone and Speech Recognition. Do not fake a Local
  Network request button; Apple prompts when LAN/Bonjour access first occurs.
- Keep permission rows compact on iPhone.
- Local Games are app-only. When focused, game surfaces should consume arrow
  keys and space instead of triggering app navigation.
- Expose current-track seek controls on iOS/macOS by sending
  `command:"seek_relative"` with integer millisecond offsets. Positive values
  seek forward, negative values seek backward. This is optional for ESP.
- Detect likely unclean exits and offer only user-mediated crash reporting:
  copy redacted diagnostics or open a prefilled `pcvantol/djconnect` issue.
- Do not log bearer tokens, HA tokens, Spotify secrets, or audio URLs.
```

## Raspberry Pi Client

```text
Sync the DJConnect Raspberry Pi client with the Home Assistant integration contract.

Requirements:
- Keep one stable device_id per Pi installation across normal launches.
- Use client_type raspberry_pi and device IDs shaped like
  djconnect-raspberry-pi-XXXXXXXXXXXX.
- Treat the Pi as an app-like display remote, not ESP firmware.
- Support both outbound POST /api/djconnect/pair and the app-like local Client
  API URL flow. The Pi exposes GET /api/device/info, GET /api/device/pairing-info,
  POST /api/device/pair, POST /api/device/command, POST /api/device/dj_response
  and POST /api/device/forget.
- Advertise `_djconnect._tcp` mDNS on the local Client API port with TXT records
  including device_id, client_type=raspberry_pi, version, device_name and
  local_url.
- Store only the returned DJConnect bearer token plus ha_local_url.
- Send status to POST /api/djconnect/status with device_id, device_name,
  client_type, version, firmware, ha_pairing_status and display-remote
  capabilities.
- Send playback commands to POST /api/djconnect/command. Supported first
  version commands are status, play, pause, next, previous, set_volume,
  set_shuffle and set_repeat.
- Do not implement PTT, microphone capture, POST /api/djconnect/voice or local
  DJ response audio playback. POST /api/device/dj_response displays text on
  screen and may report audio_played:false when no audio device is configured.
- Do not expose ESP-only reboot, OTA, battery, Wi-Fi RSSI, screen brightness,
  screen timeout, speaker volume, LED, log-level or firmware entities.
- Keep the updater and OS maintenance daemon separate from the touch UI and
  keep the touch UI runnable without root privileges.
- Use unattended GitHub release updates only after verifying release assets with
  SHA256 at minimum; prefer signed manifests when available.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Never log bearer tokens, HA tokens, Spotify secrets, Wi-Fi passwords or
  temporary audio URLs.
```

## ESP Firmware

```text
Sync the DJConnect ESP firmware with the Home Assistant integration contract.

Requirements:
- ESP clients are physical DJConnect devices and must use client_type esp32.
- Use model-specific device_id values, for example
  djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX or
  djconnect-esp32-s3-box-3-XXXXXXXXXXXX.
- Do not accept or generate legacy djconnect-XXXXXXXXXXXX ids.
- Expose local ESP endpoints: GET /api/device/info,
  GET /api/device/pairing-info, POST /api/device/pair,
  POST /api/device/command, POST /api/device/dj_response,
  POST /api/device/forget, plus ESP-only reboot/OTA routes where supported.
- /api/device/pairing-info must return the real device_id, visible pair_code,
  client_type esp32, firmware, device_name, and reachable local_url.
- POST /api/device/pair must require device_token and ha_local_url.
- Persist only the DJConnect device bearer token and ha_local_url. Do not store
  Spotify OAuth/client secrets, Home Assistant long-lived tokens, or playback
  backend credentials.
- Always use ha_local_url for ESP -> HA status, command, and voice traffic.
  Never use Nabu Casa/cloud URLs for device runtime traffic.
- Send device_id, client_type esp32, firmware, ha_pairing_status, local_url,
  language, log_level, and current device settings in status payloads.
- Send raw WAV voice audio to POST /api/djconnect/voice with Authorization:
  Bearer <device_token> and X-DJConnect-Device-ID.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping
  enough diagnostics to recover.
- Never log bearer tokens, HA tokens, Spotify secrets, WiFi passwords, or
  temporary audio URLs.
```

---

## Detailed Home Assistant Sync Prompt

Use this prompt in the DJConnect Home Assistant integration repo when syncing with the ESP firmware.

```md
# Codex Prompt: Sync DJConnect HA Integration With ESP Firmware 3.1.x

Werk in de bestaande Home Assistant custom integration repo voor DJConnect.

## Doel

Synchroniseer de HA integratie met de ESP firmware contracten rond pairing, status, playback commands, sensoren, voice en multi-device OTA.

## Belangrijkste Contracten

### Pairing URL Contract

Bij `POST /api/device/pair` naar de ESP:

```json
{
  "device_id": "djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX",
  "client_type": "esp32",
  "device_token": "...",
  "ha_local_url": "http://192.168.1.x:8123",
  "device_language": "nl"
}
```

Regels:

- `device_id` is model-specifiek: `djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX` voor LilyGO en `djconnect-esp32-s3-box-3-XXXXXXXXXXXX` voor ESP32-S3-BOX-3.
- De ESP mDNS hostname gebruikt exact dezelfde `device_id`, dus bijvoorbeeld `http://djconnect-esp32-s3-box-3-XXXXXXXXXXXX.local`.
- Gebruik het mDNS TXT veld `model` of de status/API `model` om het device model te bepalen; parse niet op de oude `djconnect-lilygo-` prefix.
- `ha_local_url` moet een echte LAN URL zijn.
- `ha_local_url` mag nooit `.ui.nabu.casa` bevatten.
- Stuur geen `ha_remote_url` naar de ESP en gebruik geen cloud/Nabu Casa URL voor ESP runtime verkeer.
- Als geen local URL bekend is, moet pairing pending/falen; zet cloud niet in local.
- Bepaal local via HA network config, internal URL, source IP of fallback `http://<HA LAN IP>:8123`.
- ESP firmware gebruikt uitsluitend `ha_local_url` voor `/api/djconnect/status`, `/api/djconnect/command` en `/api/djconnect/voice`. Cloud URL is alleen relevant voor HA/backend OAuth-configuratie, niet voor ESP verkeer.

### ESP Payload Identity

Alle ESP -> HA JSON payloads naar `/api/djconnect/status` en `/api/djconnect/command` bevatten top-level:

```json
{
  "device_id": "djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX",
  "client_type": "esp32"
}
```

Gebruik nergens meer `device_type`.

### Status Is Authoritative

`POST /api/djconnect/status` is de enige bron voor HA sensoren zoals:

- pairing status
- firmware
- batterij
- WiFi RSSI
- schermstatus/brightness/settings
- LED status
- speaker/cue volume
- taal, theme, log level
- sound output
- OTA/update state

Playback command payloads zijn identity-only en mogen geen gedeeltelijke sensorstatus overschrijven.

### Playback Command Responses

Houd auth en backend availability gescheiden:

- HTTP 401/403/404 = stale pairing/token.
- Backend/player unavailable = HTTP 200 met `success:false`, `backend_available:false`.
- `invalid_client_type` is een firmware/contractfout, geen stale pairing.
- Firmware major.minor moet matchen met integratie major.minor, behalve firmware `0.0.0` dev builds.

### OTA Manifest / Multi Device Firmware

De publieke firmware release gebruikt een multi-device manifest. Gebruik geen
top-level `device`, `asset`, `sha256` of `size` fallback meer. Selecteer altijd
de juiste entry uit `firmwares[]` op basis van het ESP device model.

Manifestvorm:

```json
{
  "version": "3.1.x",
  "version_tag": "v3.1.x",
  "channel": "stable",
  "min_ha_integration": "3.1.0",
  "max_ha_integration": "3.2.0",
  "firmwares": [
    {
      "board": "t_embed_cc1101",
      "device": "lilygo-t-embed-s3",
      "asset": "djconnect-lilygo-t-embed-s3-v3.1.x.bin",
      "url": "https://github.com/pcvantol/djconnect-firmware/releases/download/v3.1.x/djconnect-lilygo-t-embed-s3-v3.1.x.bin",
      "sha256": "...",
      "size": 123
    },
    {
      "board": "esp32_s3_box3",
      "device": "esp32-s3-box-3",
      "asset": "djconnect-esp32-s3-box-3-v3.1.x.bin",
      "url": "https://github.com/pcvantol/djconnect-firmware/releases/download/v3.1.x/djconnect-esp32-s3-box-3-v3.1.x.bin",
      "sha256": "...",
      "size": 123
    }
  ]
}
```

Bij `POST /api/device/ota` naar de ESP:

```json
{
  "version": "3.1.x",
  "url": "https://...",
  "sha256": "...",
  "device": "lilygo-t-embed-s3",
  "asset": "djconnect-lilygo-t-embed-s3-v3.1.x.bin"
}
```

Regels:

- LilyGO gebruikt `device:"lilygo-t-embed-s3"` en asset `djconnect-lilygo-t-embed-s3-vX.Y.Z.bin`.
- ESP32-S3-BOX-3 gebruikt `device:"esp32-s3-box-3"` en asset `djconnect-esp32-s3-box-3-vX.Y.Z.bin`.
- `min_ha_integration` en `max_ha_integration` volgen de firmware major.minor lijn: firmware `X.Y.Z` publiceert standaard `min_ha_integration:"X.Y.0"` en exclusief `max_ha_integration:"X.(Y+1).0"`.
- HA moet firmware alleen aanbieden/accepteren als de integratieversie `>= min_ha_integration` en `< max_ha_integration` is. Voor firmware `3.1.x` betekent dit dus `>=3.1.0` en `<3.2.0`.
- Dev firmware `0.0.0` blijft de uitzondering voor upgrade-aanbod vanaf lokale builds.
- Als er geen matching `firmwares[]` entry is, rapporteer duidelijk dat er geen firmware voor dit device type beschikbaar is.
- Versievergelijking blijft op manifest `version`/`version_tag`; de assetselectie is device-type specifiek.

### Queue / Up Next

Voor `POST /api/djconnect/command` met `command:"queue"`:

```json
{
  "success": true,
  "context_uri": "spotify:playlist:...",
  "queue": [
    {
      "title": "Black",
      "subtitle": "Pearl Jam",
      "uri": "spotify:track:...",
      "album_image_url": "https://..."
    }
  ]
}
```

Regels:

- App-clients mogen `limit:100` meesturen; HA retourneert maximaal 100 echte
  backend queue-items.
- Retourneer de echte backend queue/context, niet dezelfde current track als padding.
- Als er maar 1 queue-item is, retourneer 1 item.
- `context_uri` blijft nodig voor ESP/web per-item play.
- Album art URLs mogen pass-through zijn; de ESP downloadt queue thumbnails niet, de browser lazy-loadt ze wanneer de web queue zichtbaar is.
- Firmware in de huidige `3.1.x` lijn dedupet defensief op `uri` of `title/subtitle`, maar HA moet nog steeds geen kunstmatige duplicaten genereren.

### Voice

ESP physical PTT uploadt WAV naar `/api/djconnect/voice` met bearer token en `X-DJConnect-Device-ID`.
HA doet Assist/STT/TTS en retourneert DJ tekst plus optionele `audio_url`.

Firmware in de huidige `3.1.x` lijn kan de lokale PTT/DJ-aankondiging flow annuleren met de middelste encoderknop tijdens processing of het DJ-aankondiging scherm. HA hoeft hiervoor geen extra endpoint te implementeren; als een request al loopt mag de ESP de latere response lokaal negeren.

### Wake Word

Okay Nabu wake-word detectie draait lokaal op de ESP. HA hoeft geen wake-word audio te verwerken. Na detectie start de ESP dezelfde fysieke PTT flow en uploadt daarna een WAV naar `/api/djconnect/voice`.

Regels:

- HA moet dezelfde `/api/djconnect/voice` response blijven gebruiken voor PTT en wake-word activatie.
- STT/TTS fouten moeten als duidelijke JSON body terugkomen met `success:false`, `error` en `message`.
- Een optionele `audio_url` mag WAV of MP3 zijn.
- De ESP mag een late voice response negeren als de gebruiker de lokale flow heeft geannuleerd.

## Acceptatiecriteria

- Na pairing logt ESP:

```text
Home Assistant local URL: http://192.168.1.x:8123
```

- Playback commands gebruiken local:

```text
url=http://192.168.1.x:8123/api/djconnect/command
```

- De eerste ESP statuspost naar HA accepteert dezelfde `device_id`, `client_type:"esp32"` en `device_token` als de pairing callback. Een `401` op `/api/djconnect/status` terwijl HA nog ESP `/api/device/*` commands kan sturen wijst op een HA-side token/device-id mismatch in de statusroute, niet op een ESP cloud-route fallback.

- Geen HA sensor valt enkele seconden na update terug naar `unknown`.
- `sensor.djconnect_ha_pairing_status` wordt `paired` zodra ESP `ha_pairing_status:"paired"` meldt.
- `queue` response bevat geen padding met herhaalde current-track entries.
- Geen payload gebruikt `device_type`.
- Geen pairing/token reset bij `invalid_client_type` of backend unavailable.
```

---

## Detailed ESP Firmware Sync Prompt

# Codex Prompt: Synchronize DJConnect ESP Firmware With HA Integration
Werk in de bestaande proprietary ESP firmware repo pcvantol/djconnect-esp32.

Doel
Synchroniseer de ESP firmware met de actuele Home Assistant djconnect integration architectuur voor de `3.1.x` protocol lijn.

De HA integration is de trusted backend voor:

pairing;
bearer-token lifecycle;
backend playback;
Spotify OAuth;
Assist/STT/TTS;
OTA offer handling;
native HA entities.
De ESP blijft eigenaar van:

device runtime;
display/UI;
buttons/rotary;
LED-ring;
local speaker cues;
WiFi/setup;
raw WAV capture/upload;
local playback van HA DJ-aankondiging audio.
Belangrijke beslissingen
Eerdere non-HTTP control routes zijn verwijderd. ESP is geen backend music speaker/player.
ESP bewaart geen playback-backend credentials.
ESP stuurt generieke playback commands naar HA.
HA vertaalt playback commands naar Spotify of toekomstige backends.
ESP speaker is alleen voor local cues en DJ/voice response audio.
Okay Nabu wake-word support draait lokaal via TensorFlow Lite Micro en mag geen netwerk-I/O doen in het audio poll pad.
De middelste encoderknop moet een actieve PTT processing/DJ-aankondiging flow kunnen annuleren.
Oude backend-credential provisioning endpoints mogen niet bestaan of gebruikt worden.
Pairing/status/voice/command auth gebruikt alleen het device bearer token.
Device ID formats voor actuele firmware zijn model-specifiek:
- LilyGO T-Embed S3: `djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX`
- ESP32-S3-BOX-3: `djconnect-esp32-s3-box-3-XXXXXXXXXXXX`
Accepteer geen legacy `djconnect-XXXXXXXXXXXX`, `djconnect-lilygo-XXXXXXXXXXXX` of `djconnect-[6-cijferige-code]` device IDs en bouw geen compatibility fallback voor die oude/tijdelijke formaten.
Alle user-facing tekst, filenames, namespaces, logs en provisioning labels gebruiken uitsluitend DJConnect / djconnect.
NVS taal key blijft provision.language.
NVS namespace is djconnect.
Secrets nooit loggen: geen device tokens, HA tokens, Spotify tokens, WiFi wachtwoorden of tijdelijke audio URL tokens.
Assets uit HA repo overnemen
Gebruik de echte DJConnect icon/logo assets uit pcvantol/djconnect; teken het logo niet opnieuw in firmware als er een bitmap/vector-conversie gebruikt kan worden.

Bronbestanden in de HA repo:

assets/djconnect/djconnect-icon.svg
assets/djconnect/djconnect-icon-256.png
assets/djconnect/djconnect-icon-512.png
assets/djconnect/djconnect-icon-1024.png
assets/djconnect/djconnect-logo.svg
assets/djconnect/djconnect-logo-512x256.png
website/assets/djconnect/icon.svg
website/assets/djconnect/icon-192.png
website/assets/djconnect/icon-512.png
website/assets/lilygo-t-embed-djconnect.svg als visuele referentie voor de landscape hero/device mockup.
Acties:

Kopieer of exporteer het echte DJConnect icoon naar het firmware assetformaat dat de LilyGO UI gebruikt.
Houd de paarse/blauwe DJConnect iconstijl intact: vinyl, DJ letters, toonarm/microfoon en gradient arc.
Gebruik het echte icoon op splash/pairing/idle/voice schermen waar nu nog een placeholder of opnieuw getekende benadering staat.
Gebruik firmware-native conversie tooling als assets naar RGB565/LVGL/C-array/binair formaat moeten.
Commit geen gegenereerde build-cache; commit alleen de bronasset en benodigde firmware-runtime asset.
Verwijder oude producticonen en logo’s als ze niet meer gebruikt worden.
Endpoint contract
ESP -> HA
Protected routes:

POST /api/djconnect/status
POST /api/djconnect/command
POST /api/djconnect/voice
POST /api/djconnect/event indien gebruikt
Headers:

Authorization: Bearer <device_token>
X-DJConnect-Device-ID: djconnect-<device-model>-XXXXXXXXXXXX
Content-Type: application/json
Voor PTT:

Authorization: Bearer <device_token>
X-DJConnect-Device-ID: djconnect-<device-model>-XXXXXXXXXXXX
Content-Type: audio/wav
HA -> ESP
Protected local ESP routes:

GET /api/device/info
GET /api/device/pairing-info
POST /api/device/pair
POST /api/device/command
POST /api/device/ota
POST /api/device/reboot
POST /api/device/forget
POST /api/device/dj_response
Header:

Authorization: Bearer <device_token>
Taken
1. Pairing-token synchronisatie
Controleer en fix:

ESP ontvangt device_token via POST /api/device/pair.
ESP ontvangt een echte LAN ha_local_url via POST /api/device/pair.
ESP ontvangt of bewaart geen ha_remote_url voor runtime verkeer.
ESP gebruikt uitsluitend ha_local_url voor status, playback en voice.
ESP accepteert en verwacht geen oud enkelvoudig HA-URL pairingveld meer.
ESP accepteert als persistent device ID alleen de eigen model-specifieke ID.
Een tijdelijke setup/pairing code mag alleen als `pair_code` bestaan; na pairing moet de firmware de echte model-specifieke device ID gebruiken.
ESP slaat exact die token persistent op.
Eerste call naar HA /api/djconnect/command gebruikt exact die token.
Eerste call naar HA /api/djconnect/status gebruikt exact die token.
Eerste call naar HA /api/djconnect/voice gebruikt exact die token.
ESP mag pending pairing niet wissen bij tijdelijke Spotify/backend fouten.
ESP mag pending pairing alleen stale/invalid markeren bij echte HA auth/pairing errors:
401;
403;
404 met duidelijke stale pairing betekenis.
200 JSON met success:false en backend_unavailable betekent niet pairing wissen.
503 backend unavailable mag bij voorkeur ook niet direct NVS pairing wissen; toon pairing degraded/backend unavailable.
Veilige logs:

log device_token=present/missing, nooit de waarde;
log HA response status en error key;
log geen Authorization header.
Verwachte HA -> ESP pair payload:

{
  "pair_code": "123456",
  "device_id": "djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX",
  "client_type": "esp32",
  "device_name": "DJConnect",
  "device_language": "nl",
  "language": "nl",
  "device_token": "<device-token>",
  "ha_local_url": "http://homeassistant.local:8123",
  "assist_pipeline_id": "..."
}
ha_local_url is verplicht en moet een LAN URL zijn. Stuur geen oud enkelvoudig HA-URL veld mee, stuur geen ha_remote_url naar de ESP en zet nooit Nabu Casa/cloud in ha_local_url.

2. Status payload uitbreiden
Zorg dat periodieke HA status payload actuele device settings bevat zodat HA native entities correct updaten.

Stuur minimaal:

{
  "device_id": "djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX",
  "client_type": "esp32",
  "ha_pairing_status": "paired|pending|stale|unpaired",
  "local_url": "http://djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX.local",
  "firmware": "3.1.x",
  "battery_percent": 85,
  "wifi_rssi": -55,
  "uptime": 123456,
  "free_heap": 123456,
  "screen_brightness": 75,
  "brightness": 75,
  "speaker_volume": 50,
  "cue_volume": 50,
  "screen_dim_timeout_ms": 60000,
  "turn_off_after_ms": 300000,
  "language": "nl",
  "theme": "dark",
  "log_level": "info",
  "ota_state": "idle",
  "update_state": "idle"
}
Gebruik aliases waar makkelijk, want de HA integration accepteert meerdere namen:

screen_brightness / brightness;
speaker_volume / cue_volume;
screen_dim_timeout_ms;
turn_off_after_ms;
language;
theme;
log_level.
3. Generic playback command API naar HA
ESP stuurt playback commands naar:

POST /api/djconnect/command
Payload voorbeelden:

{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"status"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"devices"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"queue"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"playlists"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"pause"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"play"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"next"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"previous"}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"set_output","value":"iPhone","play":true}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"set_volume","value":35}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"start_liked_proxy","play":true}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"start_playlist","value":"spotify:playlist:...","play":true}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"play_context_at","value":{"context_uri":"spotify:playlist:...","offset_uri":"spotify:track:..."}}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"set_shuffle","value":true}
{"device_id":"djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX","client_type":"esp32","command":"set_repeat","value":"context"}
Verwachte response shapes:

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
Command responses are transport/command success first, playback-state second.
A command response with `success:true` and `playback.has_playback:false` is not
an error state.

When `playback.has_playback == false`, clients must treat the playback snapshot
as valid but empty. Playback fields may be `null` or empty strings, including
`progress_ms`, `duration_ms`, `volume_percent`, `device.volume_percent`,
`title`, `track_name`, `artist`, `album_name`, `uri`, `context_uri`,
`queue_context` and artwork URLs. Swift/Kotlin/TypeScript models must make
these fields nullable/optional and must not fail decoding because no playback is
active.

Backend unavailable/auth failure:

{
  "success": false,
  "error": "backend_unavailable",
  "message": "Spotify authorization has expired or was revoked. Reauthorize DJConnect.",
  "backend_available": false,
  "playback": {}
}
Belangrijk:

Dit is geen pairing failure.
Toon een backend/playback fout in UI.
Wis pairing niet.

Queue response handling:

{
  "success": true,
  "context_uri": "spotify:playlist:...",
  "queue": [
    {
      "title": "Black",
      "subtitle": "Pearl Jam",
      "uri": "spotify:track:...",
      "album_image_url": "https://..."
    }
  ]
}

Regels:

ESP bewaart `context_uri` voor per-item playback.
ESP deduplicates queue-items defensief op `uri`, of op `title` + `subtitle` wanneer geen URI beschikbaar is.
Als HA maar 1 item teruggeeft, mogen device en web maar 1 item tonen.
Queue thumbnail URLs zijn pass-through voor web lazy-loading; de ESP downloadt deze thumbnails niet alleen omdat de queue wordt opgehaald.
4. Device command API vanaf HA
Controleer POST /api/device/command voor device-instellingen:

{"command":"status"}
{"command":"screen_brightness","value":75}
{"command":"screen_dim_timeout","value":60000}
{"command":"turn_off_after","value":300000}
{"command":"speaker_volume","value":50}
{"command":"language","value":"nl"}
{"command":"theme","value":"dark"}
{"command":"log_level","value":"info"}
{"command":"dj_response","text":"Daar gaan we.","audio_url":"http://..."}
Responses altijd JSON:

{"success":true}
of:

{"success":false,"error":"invalid_command","message":"..."}
5. PTT / voice
Physical PTT:

ESP records WAV
-> POST /api/djconnect/voice raw audio/wav
-> HA does STT/Assist/playback/TTS
-> HA returns DJ text plus optional WAV/MP3 audio_url
-> ESP displays text and plays local response audio
Expected HA response:

{
  "success": true,
  "text": "Daar gaan we.",
  "dj_text": "Daar gaan we.",
  "audio_url": "http://homeassistant.local:8123/api/djconnect/tts/token.mp3",
  "audio_type": "mp3"
}
Fout:

{
  "success": false,
  "error": "stt_failed",
  "message": "No STT provider configured..."
}
Acties:

Directe HA Assist WebSocket auth vanaf ESP niet gebruiken.
ESP uploadt alleen raw WAV.
ESP speelt WAV of MP3 audio URL af indien ondersteund.
Onbekend audioformaat: text-only tonen, niet crashen.
Geen tijdelijke audio URL tokens loggen.

PTT/wake-word runtime gedrag:

Encoder PTT start pas met opnemen na de start cue/settle delay; stop cue speelt pas nadat de WAV is afgesloten.
Wake-word detection start dezelfde lokale PTT WAV-upload flow als encoder PTT.
Wake-word tuning: Okay Nabu model, 10 ms feature step, 3-frame sliding window. LilyGO cutoff is 0.90; ESP32-S3-BOX-3 cutoff is 0.86.
Wake-word-started recording stopt automatisch na stilte en blijft altijd begrensd door de maximale opname-duur.
Tijdens processing of het DJ-aankondiging scherm annuleert een middelste encoderdruk de rest van de PTT flow zo snel mogelijk; lopende HA HTTP responses mogen lokaal genegeerd worden en response audio moet een stop request krijgen.
6. OAuth / Spotify secrets verwijderen
Controleer dat ESP:

geen backend OAuth/client-id/refresh-token secrets opslaat;
geen backend OAuth secrets verwacht in pair/status responses;
playback_configured is hooguit een backend/statushint, niet een request om playback credentials te ontvangen.
Verwijder/neutraliseer oude codepaden die Spotify credentials naar ESP provisionen.

7. OTA
Controleer:

OTA endpoint blijft POST /api/device/ota.
Bearer token verplicht.
Payload accepteert:
{
  "version": "3.1.x",
  "url": "https://...",
  "sha256": "...",
  "device": "lilygo-t-embed-s3",
  "asset": "djconnect-lilygo-t-embed-s3-v3.1.x.bin"
}
device moet matchen met het boardprofiel van de firmware:
- LilyGO productie: `lilygo-t-embed-s3`, asset `djconnect-lilygo-t-embed-s3-v3.1.x.bin`
- ESP32-S3-BOX-3 bring-up: `esp32-s3-box-3`, asset `djconnect-esp32-s3-box-3-v3.1.x.bin`
Het manifest gebruikt alleen een `firmwares` array met board, device, asset,
sha256 en size per firmware. Geen top-level single-device `device`, `asset`,
`sha256` of `size` fallback.
Tijdens OTA:
duidelijke UI status;
paarse snelle LED-ring animatie;
release wake-word/TFLite en actieve voice/audio resources voordat firmware-download/TLS start;
status na reboot terug naar idle;
post-boot status naar HA met firmwareversie en idle state.
8. BLE WiFi provisioning
BLE provisioning doet alleen WiFi credentials.

Service/characteristics:

Service UUID: 7f705000-9f8f-4f1a-9b5f-570071fd0001
WiFi write characteristic: 7f705001-9f8f-4f1a-9b5f-570071fd0001
Status read/notify characteristic: 7f705002-9f8f-4f1a-9b5f-570071fd0001
Geen Spotify credentials, device tokens of andere secrets via BLE.

9. UI/UX
Device blijft koppelcode tonen tot HA pairing echt bevestigd is.
Gebruik het echte DJConnect icoon uit de overgenomen assets op het device-scherm; geen approximatie met eigen SVG/primitive drawing.
Na succesvolle HA direct pair en eerste geaccepteerde HA command/status mag UI naar paired/groen.
Backend unavailable mag niet terug naar pairing-code scherm forceren.
Pairing stale mag duidelijk tonen: reset/re-pair nodig.
Soft reset/reboot moet local cue sound en felle witte LED-ring flash tonen vlak voor reboot.
Bonus games Pong, Asteroids, Fly en Pacman mogen in UI blijven.
10. Tests
Voeg/update host tests waar mogelijk:

Pairing token opgeslagen en hergebruikt voor /status, /command, /voice.
Backend unavailable response wist pairing niet.
401/403/404 markeert pairing stale maar wist NVS niet automatisch.
Status payload bevat settings aliases.
Device command parsing voor brightness/speaker/language/theme/log_level.
PTT upload bouwt correcte headers en content type.
No Spotify OAuth secret keys in status/pair/provision payloads.
OTA payload device target matcht het boardprofiel (`lilygo-t-embed-s3` of `esp32-s3-box-3`).
DJConnect asset conversie test of snapshot/checksum zodat het firmware asset niet per ongeluk terugvalt naar een oud producticoon.
Acceptatiecriteria
ESP pairt met HA en blijft paired na de eerste /api/djconnect/command.
ESP gebruikt uitsluitend de eigen model-specifieke device ID als echte device ID en accepteert geen legacy `djconnect-XXXXXXXXXXXX`, `djconnect-lilygo-XXXXXXXXXXXX` of `djconnect-[6-cijferige-code]`.
ESP wist pairing niet door Spotify OAuth/backend failures.
ESP status houdt HA native entities actueel.
ESP gebruikt alleen de HA-native lokale API.
ESP bewaart geen Spotify credentials.
ESP stuurt generic playback commands naar HA.
ESP PTT uploadt raw WAV naar HA en speelt HA DJ-aankondiging lokaal af.
ESP annuleert PTT/DJ-aankondiging flow op middelste encoderdruk tijdens processing/response.
ESP deduplicates Up Next queue display so one real queue item is not shown repeatedly.
OTA gebruikt `djconnect-lilygo-t-embed-s3-vX.Y.Z.bin` met target `lilygo-t-embed-s3`, en `djconnect-esp32-s3-box-3-vX.Y.Z.bin` met target `esp32-s3-box-3`.
Het device gebruikt de echte DJConnect icon assets uit pcvantol/djconnect in plaats van een opnieuw getekende benadering.
Logs bevatten geen secrets.

---

## Detailed iOS/macOS App Handoff

# DJConnect iOS/macOS App Sync Prompt / Handoff

This handoff is for building a new native iOS/macOS DJConnect client that uses
the same Home Assistant custom integration backend as the ESP32 firmware.

Use this as the sync prompt for a new Apple-client repo. The current ESP
firmware contract line is `3.1.x`; Apple clients should follow the same
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
  "firmware": "3.1.13",
  "app_version": "3.1.13",
  "platform": "ios"
}
```

For macOS:

```json
{
  "device_id": "djconnect-macos-8F3A2C91B45D",
  "device_name": "DJConnect Mac",
  "client_type": "macos",
  "firmware": "3.1.13",
  "app_version": "3.1.13",
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
  "ha_version": "3.1.13",
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
  "firmware": "3.1.13",
  "app_version": "3.1.13",
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
  "ha_local_url": "http://192.168.1.x:8123"
}
```

Rules:

- Accept only this app installation's own `device_id`.
- Accept only the expected `client_type` for the target app (`ios` or `macos`).
- Store only the DJConnect bearer token, HA local URL and lightweight
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
  "firmware": "3.1.13",
  "app_version": "3.1.13",
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
  "backend_available": true,
  "playback": {}
}
```

Home Assistant must not send `device_language` or `language` to iOS, macOS or
Raspberry Pi clients in pairing/status responses. App-like clients own their UI
language setting locally.

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
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"queue","limit":100}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"playlists"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"pause"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"play"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"next"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"previous"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"seek_relative","value":15000}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"seek_relative","value":-15000}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_volume","value":35}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_shuffle","value":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_repeat","value":"context"}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"start_liked_proxy","play":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"start_playlist","value":"spotify:playlist:...","play":true}
{"device_id":"djconnect-ios-8F3A2C91B45D","client_type":"ios","command":"set_output","value":"iPhone","play":true}
```

Playlist command responses should include playlist metadata and artwork when
available:

```json
{
  "success": true,
  "playlists": [
    {
      "id": "spotify:playlist:...",
      "name": "Friday Night",
      "uri": "spotify:playlist:...",
      "image_url": "https://..."
    }
  ]
}
```

For playlist artwork, clients should accept these aliases:

- `image_url`
- `album_image_url`
- `media_image_url`
- `entity_picture`

Home Assistant should prefer `image_url` for playlist artwork, but may also
return one of the aliases above when sharing code with queue/playback image
serializers. Queue items continue to use `album_image_url` as the primary field.

Apple app clients may expose current-track seek controls. Use
`command:"seek_relative"` with an integer `value` in milliseconds. Positive
values seek forward and negative values seek backward. Home Assistant should
clamp the resulting position to the current track and return the usual command
response. ESP clients may skip this UI capability.

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

Command responses are transport/command success first, playback-state second.
A command response with `success:true` and `playback.has_playback:false` is not
an error state.

When `playback.has_playback == false`, clients must treat the playback snapshot
as valid but empty. Playback fields may be `null` or empty strings, including
`progress_ms`, `duration_ms`, `volume_percent`, `device.volume_percent`,
`title`, `track_name`, `artist`, `album_name`, `uri`, `context_uri`,
`queue_context` and artwork URLs. Swift/Kotlin/TypeScript models must make
these fields nullable/optional and must not fail decoding because no playback is
active.

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

---

## Apple App Focused Sync Prompts

# Sync Prompts

Use these prompts when handing work between the Home Assistant integration,
Apple app, ESP firmware, Raspberry Pi client, and website/docs repos.

Canonical repo locations:

- Home Assistant integration: `pcvantol/djconnect`
- Apple app: `pcvantol/djconnect-app`
- ESP firmware: `pcvantol/djconnect-esp32`
- Website/docs: `pcvantol/djconnect-website`
- Raspberry Pi client: `pcvantol/djconnect-pi`

## Home Assistant Integration

```text
Sync the DJConnect Home Assistant integration with the Apple app and ESP
client contracts.

Requirements:
- Treat iOS/macOS/Raspberry Pi as app-like clients, not ESP hardware devices.
- Pair app-like clients through POST /api/djconnect/pair. For Raspberry Pi, this is
  the primary pairing path; do not try to call a Pi-local /api/device/pair
  endpoint during initial pairing.
- Pair ESP clients through their local /api/device/pair flow after resolving
  /api/device/pairing-info and verifying the visible pair_code.
- Accept stable device_id, device_name, client_type, firmware, app_version,
  platform and optional capabilities. Raspberry Pi status/pairing payloads may
  advertise capabilities such as touch=true, voice=false, local_audio=false and
  local_dj_response_endpoint=false.
- Accept the app-generated code as pair_code, pairing_code, or pairing_token.
- Return a DJConnect bearer token on success. The current compatible field is
  device_token; bearer_token and token may also be returned.
- Return ha_local_url during successful app pairing. Do not return
  device_language/language for iOS, macOS or Raspberry Pi clients; those
  clients determine their UI language locally.
- Keep cloud/remote URLs out of Apple app runtime traffic; cloud URLs are only
  needed by Home Assistant-owned Spotify OAuth config flows.
- When pairing an app-like client, ask for or use the Client API URL shown in
  the client pairing sheet. Do not assume a changing Bonjour hostname remains
  the canonical callback target after pairing.
- Implement full HA-side mDNS autodiscovery for Raspberry Pi clients in the
  pairing config-flow. Browse Bonjour/mDNS service `_djconnect._tcp`, resolve
  each service, validate `client_type=raspberry_pi` against device IDs shaped
  `djconnect-raspberry-pi-XXXXXXXXXXXX`, build the local Client API URL from
  service address/port or `local_url`, then always probe
  `GET /api/device/pairing-info` when the URL is reachable. Pairing-info is
  authoritative for `local_url`, `device_id`, `client_type`, `device_name`,
  `pair_code`, `version/app_version/firmware` and `paired`.
- The HA pairing form must prefill Raspberry Pi `Client API URL`,
  `client_type=raspberry_pi`, `device_name`, stable `device_id` and visible
  `pair_code` from pairing-info. If exactly one Pi is discovered, select it by
  default but still require user confirmation; if multiple clients are found,
  show a discovered-client selector with useful labels. Discovery is
  convenience only and must never mark a device paired by itself.
- If Pi mDNS TXT is visible but `/api/device/pairing-info` fails, show the
  discovered client as reachable-by-mDNS but not verified, keep manual Client
  API URL entry available, and surface a clear pairing error instead of silently
  falling back to `djconnect-{pair_code}`. Do not create a second HA entry when
  the discovered Pi `device_id` is already configured; guide the user to reset
  or re-pair that existing client.
- Add/keep HA tests for Raspberry Pi discovery: service TXT acceptance,
  pairing-info override, config-flow prefill for one Pi, selector behavior for
  multiple clients, duplicate `device_id` handling, pairing-info failure
  fallback, and proof that Pi pairing uses the stable discovered
  `djconnect-raspberry-pi-XXXXXXXXXXXX` instead of `djconnect-{pair_code}`.
- Return ha_version or ha_major_minor on status/command responses so Apple
  clients can enforce the matching major.minor contract.
- Apple clients host local /api/device/* endpoints for HA -> client traffic,
  but must not implement ESP-only reboot or OTA routes. Raspberry Pi display
  clients may be outbound-only and must advertise capabilities so HA does not
  require local voice, audio, or dj_response endpoints.
- Persist client_type as ios, macos, raspberry_pi, or esp32. Do not
  reintroduce device_type.
- Authenticated status/command/voice routes must accept Authorization: Bearer
  plus X-DJConnect-Device-ID.
- Validate that client_type matches the device_id prefix/model family:
  ios -> djconnect-ios-*, macos -> djconnect-macos-*, raspberry_pi -> djconnect-raspberry-pi-*, esp32 -> ESP
  model-specific ids such as djconnect-lilygo-t-embed-s3-*.
- During app pairing, 401/403 code mismatch responses stop polling, keep the
  visible app code, and do not rotate device_id automatically.
- Create native HA entities for paired app-like clients when status is
  received, including outbound-only Raspberry Pi clients that never expose
  /api/device/* endpoints.
- Create only client/runtime and backend/playback entities for ios, macos, and
  raspberry_pi clients; do not create ESP-only battery, Wi-Fi RSSI, screen
  state, LED state, screen brightness/timeout, speaker volume, device language,
  auto-off, theme/log-level, firmware OTA, or reboot entities for app-like
  clients. Raspberry Pi local settings such as screen blanking, logging and
  update channel are client-owned and should not be modeled as ESP hardware
  entities unless a future Pi-specific HA entity design is explicitly added.
- Support App Store review by allowing Apple clients to enter local Demo Mode
  without HA; Demo Mode must not create HA devices/entities, tokens, or backend
  traffic. Local sample DJ announcement audio/text in Demo Mode is app-local and
  is not proof of HA voice validation.
- Return HTTP 426 version_mismatch when client and HA major.minor protocol
  versions do not match; do not treat this as stale auth.
- Return backend_unavailable as HTTP 200 success:false with
  backend_available:false, not as HTTP 503.
```

## Apple App

```text
Sync the DJConnect Apple app with the Home Assistant integration contract.

Requirements:
- Keep one stable device_id per app installation across normal launches.
- Reset Pairing clears the DJConnect bearer token, rotates the app pairing
  code, and creates a fresh device_id for a new setup.
- Pair by polling POST /api/djconnect/pair with pair_code, pairing_code, and
  pairing_token set to the same app-generated code.
- Store only the returned DJConnect bearer token in Keychain and persist
  ha_local_url, device_id, and client_type.
- Expose local /api/device/info, pairing-info, pair, command, dj_response, and
  forget routes for HA -> app traffic; do not expose ESP-only reboot/OTA.
- Send device_id, client_type, firmware, app_version, device_name, ha_local_url,
  and local_url on status payloads. Send device_id and client_type on command
  payloads. Always use the local Home Assistant URL for app-to-HA traffic.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping the
  token until explicit user reset.
- Treat 401/403 during unauthenticated pairing polling as code/setup mismatch:
  stop polling, keep the visible app code, and ask the user to re-enter it.
- Show first-run onboarding once per installation with the Home Assistant setup
  link and Spotify Premium requirement. Do not request Spotify credentials in
  the app.
- While unpaired, block runtime UI with a pairing sheet that shows the
  DJConnect banner, copyable Client API url, copyable app-generated pairing
  code, progress/status, and a green success state with `Let's Start!`.
- Keep the Client API url shown during pairing pinned locally until explicit
  pairing reset.
- Offer Demo Mode from the unpaired pairing sheet for App Store review and UI
  inspection without a Home Assistant backend. Demo Mode must use local sample
  data and must not store a bearer token.
- Fresh installs should default the Home Assistant URL field to
  `http://homeassistant.local:8123`, while paired runtime traffic must use the
  returned `ha_local_url`.
- Use the shared DJConnect blue/purple gradient canvas across iOS, iPadOS, and
  macOS screens.
- Settings may preflight Microphone and Speech Recognition. Do not fake a Local
  Network request button; Apple prompts when LAN/Bonjour access first occurs.
- Keep permission rows compact on iPhone.
- Local Games are app-only. When focused, game surfaces should consume arrow
  keys and space instead of triggering app navigation.
- Detect likely unclean exits and offer only user-mediated crash reporting:
  copy redacted diagnostics or open a prefilled `pcvantol/djconnect` issue.
- Do not log bearer tokens, HA tokens, Spotify secrets, or audio URLs.
```

## Raspberry Pi Client

```text
Sync the DJConnect Raspberry Pi client with the Home Assistant integration contract.

Requirements:
- Keep one stable device_id per Pi installation across normal launches.
- Use client_type raspberry_pi and device IDs shaped like
  djconnect-raspberry-pi-XXXXXXXXXXXX.
- Treat the Pi as an app-like display remote, not ESP firmware.
- Support both outbound POST /api/djconnect/pair and the app-like local Client
  API URL flow. The Pi exposes GET /api/device/info, GET /api/device/pairing-info,
  POST /api/device/pair, POST /api/device/command, POST /api/device/dj_response
  and POST /api/device/forget.
- Advertise `_djconnect._tcp` mDNS on the local Client API port with TXT records
  including device_id, client_type=raspberry_pi, version, device_name and
  local_url.
- Store only the returned DJConnect bearer token plus ha_local_url.
- Send status to POST /api/djconnect/status with device_id, device_name,
  client_type, version, firmware, ha_pairing_status and display-remote
  capabilities.
- Send playback commands to POST /api/djconnect/command. Supported first
  version commands are status, play, pause, next, previous, set_volume,
  set_shuffle and set_repeat.
- Do not implement PTT, microphone capture, POST /api/djconnect/voice or local
  DJ response audio playback. POST /api/device/dj_response displays text on
  screen and may report audio_played:false when no audio device is configured.
- Do not expose ESP-only reboot, OTA, battery, Wi-Fi RSSI, screen brightness,
  screen timeout, speaker volume, LED, log-level or firmware entities.
- Keep the updater and OS maintenance daemon separate from the touch UI and
  keep the touch UI runnable without root privileges.
- Use unattended GitHub release updates only after verifying release assets with
  SHA256 at minimum; prefer signed manifests when available.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Never log bearer tokens, HA tokens, Spotify secrets, Wi-Fi passwords or
  temporary audio URLs.
```

## ESP Firmware

```text
Sync the DJConnect ESP firmware with the Home Assistant integration contract.

Requirements:
- ESP clients are physical DJConnect devices and must use client_type esp32.
- Use model-specific device_id values, for example
  djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX or
  djconnect-esp32-s3-box-3-XXXXXXXXXXXX.
- Do not accept or generate legacy djconnect-XXXXXXXXXXXX ids.
- Expose local ESP endpoints: GET /api/device/info,
  GET /api/device/pairing-info, POST /api/device/pair,
  POST /api/device/command, POST /api/device/dj_response,
  POST /api/device/forget, plus ESP-only reboot/OTA routes where supported.
- /api/device/pairing-info must return the real device_id, visible pair_code,
  client_type esp32, firmware, device_name, and reachable local_url.
- POST /api/device/pair must require device_token and ha_local_url.
- Persist only the DJConnect device bearer token and ha_local_url. Do not store
  Spotify OAuth/client secrets, Home Assistant long-lived tokens, or playback
  backend credentials.
- Always use ha_local_url for ESP -> HA status, command, and voice traffic.
  Never use Nabu Casa/cloud URLs for device runtime traffic.
- Send device_id, client_type esp32, firmware, ha_pairing_status, local_url,
  language, log_level, and current device settings in status payloads.
- Send raw WAV voice audio to POST /api/djconnect/voice with Authorization:
  Bearer <device_token> and X-DJConnect-Device-ID.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping
  enough diagnostics to recover.
- Never log bearer tokens, HA tokens, Spotify secrets, WiFi passwords, or
  temporary audio URLs.
```

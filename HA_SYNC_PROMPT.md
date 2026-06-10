# DJConnect Home Assistant Sync Prompt

Use this prompt in the DJConnect Home Assistant integration repo when syncing with the ESP firmware.

```md
# Codex Prompt: Sync DJConnect HA Integration With ESP Firmware v3.1.0

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
- HA moet firmware alleen aanbieden/accepteren als de integratieversie `>= min_ha_integration` en `< max_ha_integration` is. Voor firmware `3.1.0` betekent dit dus `>=3.1.0` en `<3.2.0`.
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

- Retourneer de echte backend queue/context, niet dezelfde current track als padding.
- Als er maar 1 queue-item is, retourneer 1 item.
- `context_uri` blijft nodig voor ESP/web per-item play.
- Album art URLs mogen pass-through zijn; de ESP downloadt queue thumbnails niet, de browser lazy-loadt ze wanneer de web queue zichtbaar is.
- Firmware v3.1.0 dedupet defensief op `uri` of `title/subtitle`, maar HA moet nog steeds geen kunstmatige duplicaten genereren.

### Voice

ESP physical PTT uploadt WAV naar `/api/djconnect/voice` met bearer token en `X-DJConnect-Device-ID`.
HA doet Assist/STT/TTS en retourneert DJ tekst plus optionele `audio_url`.

Firmware v3.1.0 kan de lokale PTT/DJ-response flow annuleren met de middelste encoderknop tijdens processing of het DJ-response scherm. HA hoeft hiervoor geen extra endpoint te implementeren; als een request al loopt mag de ESP de latere response lokaal negeren.

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

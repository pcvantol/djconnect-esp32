# DJConnect Home Assistant Sync Prompt

Use this prompt in the DJConnect Home Assistant integration repo when syncing with the ESP firmware.

```md
# Codex Prompt: Sync DJConnect HA Integration With ESP Firmware v3.0.21

Werk in de bestaande Home Assistant custom integration repo voor DJConnect.

## Doel

Synchroniseer de HA integratie met de ESP firmware contracten rond pairing, status, playback commands, sensoren en voice.

## Belangrijkste Contracten

### Pairing URL Contract

Bij `POST /api/device/pair` naar de ESP:

```json
{
  "device_id": "djconnect-lilygo-XXXXXXXXXXXX",
  "client_type": "esp32",
  "device_token": "...",
  "ha_local_url": "http://192.168.1.x:8123",
  "ha_remote_url": "https://xxxx.ui.nabu.casa",
  "device_language": "nl"
}
```

Regels:

- `ha_local_url` moet een echte LAN URL zijn.
- `ha_local_url` mag nooit `.ui.nabu.casa` bevatten.
- `ha_remote_url` mag wel Nabu Casa/cloud zijn.
- Als geen local URL bekend is, laat `ha_local_url` leeg of weg, maar zet cloud niet in local.
- Bepaal local via HA network config, internal URL, source IP of fallback `http://<HA LAN IP>:8123`.

### ESP Payload Identity

Alle ESP -> HA JSON payloads naar `/api/djconnect/status` en `/api/djconnect/command` bevatten top-level:

```json
{
  "device_id": "djconnect-lilygo-XXXXXXXXXXXX",
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
- Firmware v3.0.21 dedupet defensief op `uri` of `title/subtitle`, maar HA moet nog steeds geen kunstmatige duplicaten genereren.

### Voice

ESP physical PTT uploadt WAV naar `/api/djconnect/voice` met bearer token en `X-DJConnect-Device-ID`.
HA doet Assist/STT/TTS en retourneert DJ tekst plus optionele `audio_url`.

Firmware v3.0.21 kan de lokale PTT/DJ-response flow annuleren met de middelste encoderknop tijdens processing of het DJ-response scherm. HA hoeft hiervoor geen extra endpoint te implementeren; als een request al loopt mag de ESP de latere response lokaal negeren.

## Acceptatiecriteria

- Na pairing logt ESP:

```text
Home Assistant local URL: http://192.168.1.x:8123
Home Assistant remote URL: https://xxxx.ui.nabu.casa
```

- Playback commands gebruiken local:

```text
url=http://192.168.1.x:8123/api/djconnect/command
```

- Geen HA sensor valt enkele seconden na update terug naar `unknown`.
- `sensor.djconnect_ha_pairing_status` wordt `paired` zodra ESP `ha_pairing_status:"paired"` meldt.
- `queue` response bevat geen padding met herhaalde current-track entries.
- Geen payload gebruikt `device_type`.
- Geen pairing/token reset bij `invalid_client_type` of backend unavailable.
```

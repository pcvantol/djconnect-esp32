# Codex Prompt: Synchronize DJConnect ESP Firmware With HA Integration
Werk in de bestaande proprietary ESP firmware repo pcvantol/djconnect-esp32.

Doel
Synchroniseer de ESP firmware met de actuele Home Assistant djconnect integration architectuur voor release 3.0.21.

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
local playback van HA DJ response audio.
Belangrijke beslissingen
Eerdere non-HTTP control routes zijn verwijderd. ESP is geen backend music speaker/player.
ESP bewaart geen playback-backend credentials.
ESP stuurt generieke playback commands naar HA.
HA vertaalt playback commands naar Spotify of toekomstige backends.
ESP speaker is alleen voor local cues en DJ/voice response audio.
Okay Nabu wake-word support draait lokaal via TensorFlow Lite Micro en mag geen netwerk-I/O doen in het audio poll pad.
De middelste encoderknop moet een actieve PTT processing/DJ-response flow kunnen annuleren.
Oude backend-credential provisioning endpoints mogen niet bestaan of gebruikt worden.
Pairing/status/voice/command auth gebruikt alleen het device bearer token.
Device ID format voor actuele firmware is djconnect-lilygo-XXXXXXXXXXXX.
Accepteer geen legacy djconnect-XXXXXXXXXXXX device IDs en bouw geen compatibility fallback voor dat oude formaat.
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
X-DJConnect-Device-ID: djconnect-lilygo-XXXXXXXXXXXX
Content-Type: application/json
Voor PTT:

Authorization: Bearer <device_token>
X-DJConnect-Device-ID: djconnect-lilygo-XXXXXXXXXXXX
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
ESP ontvangt ha_local_url en/of ha_remote_url via POST /api/device/pair.
ESP gebruikt ha_local_url LAN-first en ha_remote_url als cloud fallback.
ESP accepteert en verwacht geen oud enkelvoudig HA-URL pairingveld meer.
ESP accepteert als persistent device ID alleen djconnect-lilygo-XXXXXXXXXXXX.
Een tijdelijke setup-code identiteit mag alleen tijdens captive/setup flow bestaan; na pairing moet de firmware de echte LilyGO device ID gebruiken.
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
  "device_id": "djconnect-lilygo-XXXXXXXXXXXX",
  "device_name": "DJConnect",
  "device_language": "nl",
  "language": "nl",
  "device_token": "<device-token>",
  "ha_local_url": "http://homeassistant.local:8123",
  "ha_remote_url": "https://example.ui.nabu.casa",
  "assist_pipeline_id": "..."
}
Minimaal een van ha_local_url of ha_remote_url moet aanwezig zijn; stuur geen oud enkelvoudig HA-URL veld mee.

2. Status payload uitbreiden
Zorg dat periodieke HA status payload actuele device settings bevat zodat HA native entities correct updaten.

Stuur minimaal:

{
  "device_id": "djconnect-lilygo-XXXXXXXXXXXX",
  "ha_pairing_status": "paired|pending|stale|unpaired",
  "local_url": "http://djconnect-lilygo-XXXXXXXXXXXX.local",
  "firmware": "3.0.x",
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

{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"status"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"devices"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"queue"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"playlists"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"pause"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"play"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"next"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"previous"}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_output","value":"iPhone","play":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_volume","value":35}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"start_liked_proxy","play":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"start_playlist","value":"spotify:playlist:...","play":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_shuffle","value":true}
{"device_id":"djconnect-lilygo-XXXXXXXXXXXX","command":"set_repeat","value":"context"}
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
Wake-word tuning: Okay Nabu model, 10 ms feature step, 3-frame sliding window, cutoff 0.90.
Wake-word-started recording stopt automatisch na stilte en blijft altijd begrensd door de maximale opname-duur.
Tijdens processing of het DJ-response scherm annuleert een middelste encoderdruk de rest van de PTT flow zo snel mogelijk; lopende HA HTTP responses mogen lokaal genegeerd worden en response audio moet een stop request krijgen.
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
  "version": "3.0.x",
  "url": "https://...",
  "sha256": "...",
  "device": "lilygo-t-embed-s3",
  "asset": "djconnect-device-v3.0.x.bin"
}
device moet matchen met het boardprofiel van de firmware:
- LilyGO productie: `lilygo-t-embed-s3`, asset `djconnect-device-v3.0.x.bin`
- ESP32-S3-BOX-3 bring-up: `esp32-s3-box-3`, asset `djconnect-device-esp32-s3-box-3-v3.0.x.bin`
Het manifest houdt top-level LilyGO velden voor compatibility en bevat daarnaast
een `firmwares` array met board, device, asset, sha256 en size per firmware.
Tijdens OTA:
duidelijke UI status;
paarse snelle LED-ring animatie;
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
Bonus games Pong, Asteroids en Fly mogen in UI blijven.
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
ESP gebruikt uitsluitend djconnect-lilygo-XXXXXXXXXXXX als echte device ID en accepteert geen legacy djconnect-XXXXXXXXXXXX.
ESP wist pairing niet door Spotify OAuth/backend failures.
ESP status houdt HA native entities actueel.
ESP gebruikt alleen de HA-native lokale API.
ESP bewaart geen Spotify credentials.
ESP stuurt generic playback commands naar HA.
ESP PTT uploadt raw WAV naar HA en speelt HA DJ response lokaal af.
ESP annuleert PTT/DJ-response flow op middelste encoderdruk tijdens processing/response.
ESP deduplicates Up Next queue display so one real queue item is not shown repeatedly.
OTA blijft werken met `djconnect-device-vX.Y.Z.bin` en target `lilygo-t-embed-s3`, en ondersteunt daarnaast `djconnect-device-esp32-s3-box-3-vX.Y.Z.bin` met target `esp32-s3-box-3`.
Het device gebruikt de echte DJConnect icon assets uit pcvantol/djconnect in plaats van een opnieuw getekende benadering.
Logs bevatten geen secrets.

# Changelog

## v2.1.0

Geconsolideerde release van SpotifyDJ firmware voor de LilyGO T-Embed-CC1101 / ESP32-S3.

### Nieuw

- Spotify Connect afstandsbediening met now-playing scherm, trackvoortgang, volume, pause/resume, next en previous.
- Menu's voor Up Next, Playlists, Sound Outputs, Settings, About en Logs.
- Spotify play mode is instelbaar op device en webportal: geen shuffle, shuffle, repeat once of repeat infinite.
- Playlist-overzicht en playlist-start zijn beschikbaar op device en webportal.
- Current Song scherm met album-art download/cache en scrollende titel/artiest.
- Mobile webportal met now playing, album art, volume slider, sound output selectie, queue, logs, diagnostics, settings, WiFi update en OTA upload.
- Home Assistant device-laag met pairing, mDNS discovery, device-token opslag, status updates, OTA endpoint en Spotify provisioning endpoint.
- BLE WiFi provisioning in setup mode via een writable JSON characteristic.
- MQTT/Home Assistant discovery en periodieke/on-event status publishing.
- MQTT two-way Home Assistant controls voor volume, next/previous, sound output en playlist start.
- MQTT settings provisioning via Home Assistant pair/provision/status responses met opslag in `spotifydj` NVS.
- Captive portal voor WiFi, Spotify en MQTT provisioning.
- OTA releaseflow via GitHub tags en firmware assets.
- WS2812 LED-ring feedback voor volume, status, setup/AP mode, charging en connectivity.
- Speaker cues voor boot, reset, battery warning, factory reset en charging completed.
- Instelbaar built-in speaker volume voor device cues: 25%, 50%, 75% of 100%.
- Battery/charging guard schermen, low-battery deep sleep en charger-aware wake gedrag.
- WiFi-failure boot menu met encoderselectie voor retry connect, hard reset, reset device en turn off.

### Gewijzigd

- Appnaam en technische branding zijn `SpotifyDJ`.
- Release-builds gebruiken `2.1.0` / `v2.1.0`; lokale builds zonder release flags blijven `dev` / `vdev`.
- Geen WiFi-, Spotify- of Home Assistant-secrets meer hardcoded in firmware.
- Spotify credentials worden via setup portal of Home Assistant provisioning opgeslagen in NVS.
- Batterijpercentage wordt altijd voltage-based geschat en zonder tilde getoond.
- Volume is begrensd op `0-60`; LED-ring toont `60` als vol met oranje segmenten.
- Now Playing gebruikt statusindicatoren `H`, `M` en `S` voor Home Assistant, MQTT en Spotify.
- Pairing mode toont SpotifyDJ logo/naam, batterijstatus, instructietekst en grote pairing code.
- Pairing code staat ook in Serial logging en in de webinterface.
- Setup/AP en HA pairing mode houden het scherm 10 minuten op 100% brightness en gaan daarna naar deep sleep.
- Tijdens OTA firmware write toont het display `Firmware update in progress..`.
- Display idle gedrag: scherm blijft op ingestelde brightness en gaat na de ingestelde timeout direct uit.
- Eerste knop/encoderactie bij scherm-uit wekt alleen het scherm en voert geen onderliggende actie uit.
- Webinterface toont H/M/S status bovenin, WiFi signaal als bars en laatste MQTT publish timestamp.
- Webinterface logs kunnen gepauzeerd en gekopieerd worden.
- `Restart device` en `Turn off device` zijn beschikbaar vanuit settings.
- Push-to-talk is omgezet naar Route B: microfoon-audio streamt als raw PCM16 naar de Home Assistant Assist WebSocket pipeline; alleen de herkende tekst gaat naar `/api/spotify_dj/voice`.
- `assist_pipeline_id` kan optioneel in NVS worden opgeslagen; leeg betekent de default Home Assistant Assist pipeline.
- Encoder short press doet weer pause/resume, double press opent Current Song, en long press start push-to-talk tot loslaten.
- Push-to-talk logt de listening stappen naar Serial en het web logs scherm.
- LED-ring animaties: geel bij PTT start, blauw bij PTT stop/verwerken, groen bij geaccepteerde voice command response.
- De oude WAV-upload naar `/api/spotify_dj/voice` is verwijderd uit de voice command client.
- Voice statusmeldingen gebruiken `recording`, `sending_command` en `error`.
- Als er geen muziek speelt, biedt device en webportal een actie om `SpotifyDJ Liked Proxy` te starten.
- Volume control is geblokkeerd zolang er geen actieve playback is.
- Boottekst voor WiFi verbinden is `Connecting to WiFi...`.
- Captive portal MQTT velden zijn optioneel; leeg laten probeert geen MQTT setup en overschrijft HA-provisioned MQTT niet.

### Opgelost

- Flikkerend display door onnodige redraws verminderd.
- Volume-acties veroorzaken geen HTTP 411 melding meer in de UI.
- Encoder-richting voor volume gecorrigeerd.
- Token-refresh en rotated refresh tokens worden in NVS bewaard.
- Scherm blijft bruikbaar in error/pairing/OTA states.
- Settings zoals brightness, screen dim timeout en deep sleep timeout blijven na reboot behouden.
- Speaker volume blijft na reboot behouden en wordt bij factory reset gewist.
- Webinterface leegt invoervelden niet meer tijdens status polling.
- Sound output types worden niet meer achter de outputnaam getoond.
- JPEG album art rendering en current-song tekstscrolling verbeterd.
- Pairing mode blokkeert normale playback/menu input maar houdt reset en lokale API beschikbaar.

### Bekende punten

- OTA download ondersteunt HTTPS, maar SHA256-verificatie tijdens streaming is nog een TODO.
- Spotify Web API vereist Spotify Premium en een beschikbare/actieve Spotify Connect output.
- Sommige Spotify Connect outputs ondersteunen geen volume-control via de Web API.

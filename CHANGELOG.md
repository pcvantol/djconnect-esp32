# Changelog

## v1.6.0

Geconsolideerde release van SpotifyDJ firmware voor de LilyGO T-Embed-CC1101 / ESP32-S3.

### Nieuw

- Spotify Connect afstandsbediening met now-playing scherm, trackvoortgang, volume, pause/resume, next en previous.
- Menu's voor Up Next, Sound Outputs, Settings, About en Logs.
- Current Song scherm met album-art download/cache en scrollende titel/artiest.
- Mobile webportal met now playing, album art, volume slider, sound output selectie, queue, logs, diagnostics, settings, WiFi update en OTA upload.
- Home Assistant device-laag met pairing, mDNS discovery, device-token opslag, status updates, OTA endpoint en Spotify provisioning endpoint.
- MQTT/Home Assistant discovery en periodieke/on-event status publishing.
- Captive portal voor WiFi, Spotify en MQTT provisioning.
- OTA releaseflow via GitHub tags en firmware assets.
- WS2812 LED-ring feedback voor volume, status, setup/AP mode, charging en connectivity.
- Speaker cues voor boot, reset, battery warning, factory reset en charging completed.
- Battery/charging guard schermen, low-battery deep sleep en charger-aware wake gedrag.

### Gewijzigd

- Appnaam en technische branding zijn `SpotifyDJ`.
- Release-builds gebruiken `1.6.0` / `v1.6.0`; lokale builds zonder release flags blijven `dev` / `vdev`.
- Geen WiFi-, Spotify- of Home Assistant-secrets meer hardcoded in firmware.
- Spotify credentials worden via setup portal of Home Assistant provisioning opgeslagen in NVS.
- Batterijpercentage wordt altijd voltage-based geschat en zonder tilde getoond.
- Volume is begrensd op `0-60`; LED-ring toont `60` als vol.
- Now Playing gebruikt statusindicatoren `H`, `M` en `S` voor Home Assistant, MQTT en Spotify.
- Pairing mode toont SpotifyDJ logo/naam, batterijstatus, instructietekst en grote pairing code.
- Pairing code staat ook in Serial logging en in de webinterface.
- Setup/AP en HA pairing mode houden het scherm 10 minuten op 100% brightness en gaan daarna naar deep sleep.
- Tijdens OTA firmware write toont het display `Firmware update in progress..`.
- Display idle gedrag: dimmen na 10 seconden, 50% na 20 seconden, uit na de ingestelde timeout.
- Eerste knop/encoderactie bij scherm-uit wekt alleen het scherm en voert geen onderliggende actie uit.
- Webinterface toont H/M/S status bovenin, WiFi signaal als bars en laatste MQTT publish timestamp.
- Webinterface logs kunnen gepauzeerd en gekopieerd worden.
- `Restart device` en `Turn off device` zijn beschikbaar vanuit settings.

### Opgelost

- Flikkerend display door onnodige redraws verminderd.
- Volume-acties veroorzaken geen HTTP 411 melding meer in de UI.
- Encoder-richting voor volume gecorrigeerd.
- Token-refresh en rotated refresh tokens worden in NVS bewaard.
- Scherm blijft bruikbaar in error/pairing/OTA states.
- Settings zoals brightness, screen dim timeout en deep sleep timeout blijven na reboot behouden.
- Webinterface leegt invoervelden niet meer tijdens status polling.
- Sound output types worden niet meer achter de outputnaam getoond.
- JPEG album art rendering en current-song tekstscrolling verbeterd.
- Pairing mode blokkeert normale playback/menu input maar houdt reset en lokale API beschikbaar.

### Bekende punten

- OTA download ondersteunt HTTPS, maar SHA256-verificatie tijdens streaming is nog een TODO.
- Spotify Web API vereist Spotify Premium en een beschikbare/actieve Spotify Connect output.
- Sommige Spotify Connect outputs ondersteunen geen volume-control via de Web API.

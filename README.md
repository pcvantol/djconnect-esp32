# SpotifyDJ firmware voor LilyGO T-Embed-CC1101

SpotifyDJ is firmware voor de LilyGO T-Embed-CC1101 / ESP32-S3. Het device is een Spotify Connect afstandsbediening met display, encoder, LED-ring, webinterface, MQTT-status en Home Assistant device-integratie.

Dit is geen Spotify Connect speaker/player. De ESP32 bestuurt via de Spotify Web API een bestaande Spotify Connect stream op je telefoon, computer, speaker, receiver of andere Spotify Connect output.

## Features

- Toont actieve Spotify Connect output.
- Toont huidige track/podcast, artiest/show, tracklengte en voortgang.
- Laat lange titel en artiest/show eenmalig scrollen bij trackwissel.
- Volume via encoder en webinterface, begrensd op `0-60`.
- LED-ring toont volume als groene segmenten; `60` is volledig vol.
- Encoder short press: pause/resume.
- Encoder long press: current song / album-art scherm.
- Bovenknop short press: menu terug in menu's, anders next track.
- Bovenknop double press: previous track.
- Bovenknop long press: menu openen.
- Bovenknop 10 seconden: restart/soft reset via reset-monitor.
- Encoderknop + bovenknop 10 seconden: factory reset, afhankelijk van batterijstatus.
- Menu's voor Up Next, Sound Outputs, Settings, About en Logs.
- Webportal met now playing, volume slider, outputs, queue, logs, diagnostics, settings, WiFi update en OTA upload.
- Home Assistant pairing met mDNS discovery en device-token auth.
- MQTT/Home Assistant discovery en periodieke status publishing.
- Battery/charging guards, deep sleep en low-battery schermen.
- OTA endpoint voor Home Assistant-triggered firmware update.

## Hardware

- LilyGO T-Embed-CC1101.
- ESP32-S3 build target via PlatformIO.
- ST7789 display.
- Rotary encoder + encoderknop.
- Bovenknop / board user key.
- BQ27220 battery gauge.
- WS2812 LED-ring.

## Belangrijke beperkingen

- Spotify Premium is nodig voor playback control endpoints.
- Er moet een actieve Spotify Connect context zijn of een beschikbare Spotify Connect output.
- Sommige Spotify Connect devices ondersteunen geen volume control via de Web API.
- OTA via `/api/device/ota` gebruikt `Update.h`; SHA256 validatie is nog een TODO en wordt nu alleen gelogd als waarschuwing wanneer een hash is meegegeven.

## Secrets en provisioning

Er worden geen WiFi- of Spotify-secrets in firmware gecompileerd.

Niet in firmware:

- WiFi SSID
- WiFi password
- Spotify client ID
- Spotify refresh token
- Spotify client secret
- Home Assistant device token

Credentials worden via setup/provisioning in NVS opgeslagen.

`include/Secrets.h` is alleen nog bedoeld voor optionele build-time flags:

```cpp
#pragma once

// No WiFi or Spotify credentials live in firmware. Provision them via the setup portal.
#define SPOTIFY_MARKET "NL"
#define SPOTIFY_ALLOW_INSECURE_TLS 0
```

Laat `SPOTIFY_ALLOW_INSECURE_TLS` normaal op `0`. Zet dit alleen tijdelijk op `1` voor troubleshooting.

## Eerste ingebruikname

1. Flash de firmware.
2. Als er nog geen WiFi in NVS staat, start het device in setup/AP mode.
3. Verbind met het WiFi-netwerk:

   ```text
   SpotifyDJ Setup
   ```

4. Open de captive portal of ga naar het AP-adres.
5. Vul in:
   - WiFi SSID
   - WiFi password
   - Spotify client ID
   - Spotify refresh token
   - Spotify market, standaard `NL`
   - optioneel MQTT host/port/user/password
6. Het device test WiFi en Spotify, slaat credentials lokaal in NVS op en reboot.

Setup/AP mode:

- Scherm blijft op 100% brightness.
- LED-ring toont regenboog-adem animatie.
- Batterij en charging state blijven zichtbaar.
- Na 10 minuten zonder succesvolle setup gaat het device naar deep sleep.
- Bij wake start setup/AP opnieuw zolang provisioning niet klaar is.

## Spotify app en refresh token

Maak een Spotify app op:

```text
https://developer.spotify.com/dashboard
```

Voeg deze redirect URI toe:

```text
http://127.0.0.1:8888/callback
```

Genereer een refresh token met PKCE. Je hebt alleen de client ID nodig; gebruik geen client secret op de ESP32.

```bash
python3 scripts/spotify_pkce_refresh_token.py --client-id YOUR_SPOTIFY_CLIENT_ID
```

De refresh token wordt via de setup portal of Home Assistant provisioning endpoint naar het device gestuurd en in NVS opgeslagen.

Spotify kan tijdens token-refresh een nieuwe refresh token teruggeven. De firmware bewaart zo'n rotated token in de `spotify` NVS namespace.

## Home Assistant integratie

De Home Assistant custom integration gebruikt domain:

```text
spotify_dj
```

### Pairing mode

Als WiFi geconfigureerd is maar Home Assistant nog niet gepaired is:

- Het display toont:

  ```text
  SpotifyDJ
  PAIR 123456
  ```

- Het scherm blijft 10 minuten op 100% brightness.
- Normale input/playback acties worden niet verwerkt.
- Soft reset en factory reset blijven beschikbaar via de reset-monitor.
- De webportal en device API blijven actief.
- Na 10 minuten gaat het device naar deep sleep.

### mDNS discovery

Na WiFi connect adverteert het device:

```text
_spotifydj._tcp
```

Hostname:

```text
spotifydj-XXXXXXXXXXXX
```

Browsable URL:

```text
http://spotifydj-XXXXXXXXXXXX.local
```

TXT records:

- `name=SpotifyDJ`
- `device_id=spotifydj-XXXXXXXXXXXX`
- `version=<firmware_version>`
- `paired=true|false`
- `api=/api/device`
- `model=lilygo-t-embed-s3`

### Device API endpoints

Open endpoints:

- `GET /api/device/info`
- `GET /api/device/pairing-info`

Protected endpoints, met header `Authorization: Bearer <device_token>`:

- `POST /api/device/provision_spotify`
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`

Extra local pairing trigger:

- `POST /api/device/pair`

Payload:

```json
{
  "ha_url": "http://homeassistant.local:8123"
}
```

### HA endpoints die de firmware aanroept

Pairing:

```text
POST <ha_url>/api/spotify_dj/pair
```

Status:

```text
POST <ha_url>/api/spotify_dj/status
```

De status wordt bij boot en daarna elke 60 seconden gestuurd zolang het device gepaired is.

### Spotify provisioning via HA

Endpoint op de ESP:

```text
POST /api/device/provision_spotify
```

Headers:

```text
Authorization: Bearer <device_token>
Content-Type: application/json
```

Payload:

```json
{
  "spotify_client_id": "...",
  "spotify_refresh_token": "...",
  "spotify_market": "NL"
}
```

De firmware slaat deze waarden op in NVS namespace `spotifydj`, herlaadt de Spotify credentials en logt de token niet.

### OTA via HA

Endpoint op de ESP:

```text
POST /api/device/ota
```

Payload:

```json
{
  "url": "https://github.com/pcvantol/spotify-dj-firmware/releases/download/v1.6.0/spotifydj-lilygo-t-embed-s3-v1.6.0.bin",
  "sha256": "...",
  "version": "1.6.0",
  "device": "lilygo-t-embed-s3"
}
```

OTA voorwaarden:

- `device` moet `lilygo-t-embed-s3` zijn.
- Batterij moet boven 40% zijn, of charging/full, wanneer batterijstatus beschikbaar is.
- Partition table is `default_16MB.csv` en bevat `ota_0` en `ota_1`.

## Webportal

De webportal draait op poort 80 zodra WiFi verbonden is.

Belangrijke blokken:

- Now Playing
- Up Next
- Settings
- WiFi
- Home Assistant pairing/mDNS info
- Spotify
- MQTT
- Diagnostics
- Logs met pause/copy-all
- Firmware OTA upload

Bovenin staan statusindicatoren:

- `H`: Home Assistant paired
- `M`: MQTT connected
- `S`: Spotify authorized

Groen betekent OK, rood betekent niet OK.

## MQTT

MQTT is gescheiden van de Home Assistant device API.

MQTT publiceert periodiek en bij events device/playback/status naar de broker, plus Home Assistant discovery voor dashboardstatus.

MQTT instellingen worden opgeslagen in NVS:

- host
- port
- username
- password

MQTT brokerproblemen blokkeren Spotify, display, webportal of HA device API niet.

## Batterij en charging

Het getoonde batterijpercentage is altijd een voltage-based estimate. De BQ27220 SoC/gauge waarde blijft alleen diagnostisch zichtbaar.

Curve:

```cpp
if (mv >= 4150) return 100;
if (mv >= 4100) return 90;
if (mv >= 4000) return 80;
if (mv >= 3900) return 65;
if (mv >= 3800) return 50;
if (mv >= 3700) return 35;
if (mv >= 3600) return 20;
if (mv >= 3500) return 10;
if (mv >= 3300) return 5;
return 0;
```

Low battery behavior:

- Waarschuwingsgeluid wanneer batterij van boven 20% naar 20% of lager zakt.
- Onder 20%: charge scherm, rode LED-ring, normale knoppen geblokkeerd.
- Onder 10%: critical flow en deep sleep.
- Bij laden onder 20%: charging scherm, geen WiFi/Spotify, charging LED-animatie.
- Bij voldoende herstel reboot het device naar de normale flow.

## Display en sleep

Normale mode:

- Idle dim start na 10 seconden.
- 50% brightness na 20 seconden.
- Scherm uit na ingestelde screen dim timeout.
- Deep sleep na ingestelde `Deep sleep after`.

Uitzonderingen:

- Setup/AP mode forceert 100% brightness en gaat na 10 minuten slapen.
- HA pairing mode forceert 100% brightness en gaat na 10 minuten slapen.
- Low battery/charging guards hebben hun eigen brightness/sleep gedrag.

## Bediening

- Encoder draaien: volume omhoog/omlaag in stappen van 5%, max 60.
- Encoder short press: pause/resume.
- Encoder long press: Current Song / album-art.
- Bovenknop short press:
  - in menu: terug naar vorige scherm
  - in now playing: next track
- Bovenknop double press: previous track.
- Bovenknop long press: hoofdmenu.
- Bovenknop 10 seconden: restart/soft reset.
- Encoderknop + bovenknop 10 seconden: factory reset, als batterijstatus dit toestaat.

## Menus

Hoofdmenu volgorde:

1. Up Next
2. Sound outputs
3. Settings
4. About
5. Logs

Settings bevat onder andere:

- Screen brightness
- Screen dim timeout
- Deep sleep after
- Turn off device
- Restart device
- Factory reset

## Build en upload

Build:

```bash
pio run -e t_embed_cc1101
```

Upload:

```bash
pio run -e t_embed_cc1101 -t upload
```

Monitor:

```bash
pio device monitor
```

Als `pio` niet in je PATH staat:

```bash
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101 -t upload
/Users/pcvantol/.platformio/penv/bin/pio device monitor
```

Build flags voor releaseversies kunnen via environment worden meegegeven:

```bash
SPOTIFYDJ_BUILD_FLAGS='-DSPOTIFYDJ_VERSION="1.6.0" -DSPOTIFYDJ_VERSION_TAG="v1.6.0"' \
pio run -e t_embed_cc1101
```

Zonder release build flags gebruikt de firmware `dev` / `vdev`.

## Unit tests

De host-side tests draaien zonder ESP32-hardware en testen pure logica uit `LogicHelpers`.

```bash
c++ -std=c++17 -Iinclude test/native/test_logic.cpp -o /tmp/spotify_remote_unit_tests
/tmp/spotify_remote_unit_tests
```

## Firmware release maken op GitHub

Firmware releases worden gepubliceerd via de GitHub Actions workflow in de firmware-repo:

```text
https://github.com/pcvantol/spotify-dj-firmware
```

De workflow wordt getriggerd door een git tag. Gebruik semver-tags zoals `v1.6.0`.

```bash
git status
git tag v1.6.0
git push origin v1.6.0
```

Na het pushen van de tag bouwt GitHub Actions de firmware en maakt de release asset aan. Controleer daarna:

```text
https://github.com/pcvantol/spotify-dj-firmware/actions
https://github.com/pcvantol/spotify-dj-firmware/releases
```

De OTA URL voor Home Assistant gebruikt de release asset, bijvoorbeeld:

```text
https://github.com/pcvantol/spotify-dj-firmware/releases/download/v1.6.0/spotifydj-lilygo-t-embed-s3-v1.6.0.bin
```

Als je een tag opnieuw wilt gebruiken na een mislukte release, verwijder hem dan lokaal en remote en push hem opnieuw:

```bash
git tag -d v1.6.0
git push origin :refs/tags/v1.6.0
git tag v1.6.0
git push origin v1.6.0
```

## Pin mapping

- Display ST7789: CS 41, MOSI 9, SCLK 11, DC 16, RST 40, BL 21.
- Encoder: A 4, B 5, knop 0.
- Bovenknop / board user key: 6.
- WS2812 LED-ring: IO14, 8 LEDs.
- Battery gauge BQ27220: I2C SDA 8, SCL 18, adres `0x55`.
- Speaker I2S: BCLK 46, LRCLK 40, DATA 7.
- Power enable: 15.
- SD CS 13 en CC1101/Lora CS 12 worden hoog gezet om SPI-busconflicten te vermijden.

## Code opbouw

- `src/main.cpp`: alleen Arduino `setup()` en `loop()`.
- `SpotifyDJApp`: centrale orkestratie van input, display, Spotify, batterij, webserver, MQTT en HA device-layer.
- `SpotifyClient`: Spotify auth, Web API requests, playback control en async volume worker.
- `DisplayManager`: schermweergave, menu-rendering, title/artist scrolling, progress/time en backlight.
- `InputController`: rotary encoder en knoppen als high-level events.
- `BatteryMonitor`: BQ27220 reads en voltage-based estimate.
- `LedRing`: groene WS2812 volume-indicator en setup/charging animaties.
- `WebPortal`: mobile dashboard en bestaande port-80 `WebServer`.
- `MqttPublisher`: MQTT state en Home Assistant MQTT discovery.
- `SpotifyDJDevice`: device identity, NVS keys, pairing code.
- `SpotifyDJDiscovery`: mDNS `_spotifydj._tcp`.
- `SpotifyDJPairing`: HA pairing en status posts.
- `SpotifyDJApiServer`: lokale `/api/device/*` endpoints.
- `SpotifyDJOTA`: OTA streaming via `Update.h`.
- `Config`: pinout, timings, versie en centrale constants.
- `LogicHelpers`: pure helpers met native tests.

## NVS namespaces

- `provision`: bestaande setup/settings zoals WiFi, MQTT en legacy Spotify fields.
- `spotify`: rotated Spotify refresh token cache.
- `spotifydj`: Home Assistant device-layer en Spotify credentials.

Belangrijke `spotifydj` keys:

- `ha_url`
- `device_token`
- `device_name`
- `spotify_client_id`
- `spotify_refresh_token`
- `spotify_market`
- `firmware_channel`

## Troubleshooting

### Geen pairing code zichtbaar

- Zonder WiFi-configuratie start eerst setup/AP mode.
- Na succesvolle WiFi provisioning en reboot verschijnt HA pairing mode als het device nog niet gepaired is.
- Pairing mode houdt het scherm 10 minuten op 100%.

### `Refresh token missing` of `Spotify client id missing`

Provision Spotify credentials opnieuw via:

- setup/AP portal, of
- Home Assistant endpoint `POST /api/device/provision_spotify`.

### MQTT blijft disconnected

- Controleer host, poort, username/password in de webportal.
- MQTT staat los van HA HTTP pairing. Een MQTT-fout maakt `M` rood maar hoeft Spotify/HA pairing niet te blokkeren.

### OTA start niet

- Controleer bearer token.
- Controleer `device` in payload: `lilygo-t-embed-s3`.
- Controleer batterij: boven 40%, charging of full.
- Controleer dat de URL direct naar een `.bin` firmware asset wijst.

### TLS

Bij secure TLS moet de ESP32 klok via NTP gesynchroniseerd zijn voordat certificaatvalidatie betrouwbaar werkt.

Als Spotify in de toekomst van root-CA wisselt, moet de embedded CA in `src/SpotifyClient.cpp` worden bijgewerkt.

## Veiligheidscheck voor commits

Controleer dat er geen secrets in de firmware staan:

```bash
rg -n "SPOTIFY_CLIENT_ID|SPOTIFY_REFRESH_TOKEN|WIFI_SSID|WIFI_PASSWORD|client_secret|wifi144iot|verbindmet|AQB|5ea462" include src test -S
```

Deze scan mag geen echte credentials vinden.

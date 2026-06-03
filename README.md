# Spotify Connect Remote for LilyGO T-Embed-CC1101

Een kleine Spotify Connect afstandsbediening voor de LilyGO T-Embed-CC1101, geinspireerd door Knobby.

Dit is geen Spotify Connect speaker/player. De ESP32 bestuurt via de Spotify Web API een bestaande Spotify Connect stream op je telefoon, computer, speaker of receiver.

## Features

- Toont het actieve Spotify Connect device.
- Toont huidige track/podcast, artiest/show, voortgang en tracklengte.
- Laat lange titel en artiest/show 1x scrollen wanneer ze veranderen.
- Encoder draaien: volume omhoog/omlaag in stappen van 5%.
- LED-ring toont volumepercentage als groene segmenten.
- Encoder indrukken: pause/resume.
- Encoder lang indrukken: menu openen; in menu's terug naar het vorige scherm.
- Bovenknop kort: volgende track.
- Bovenknop 3 seconden vasthouden: soft reset.
- Lange druk op encoder: playback status verversen.
- Toont batterijpercentage en laadstatus via de BQ27220 fuel gauge.
- Dimt het scherm naar 10% na 30 seconden en naar 0% na 60 seconden; knop/encoder maakt weer 100%.

## Nodig

- LilyGO T-Embed-CC1101.
- Spotify Premium account voor playback control endpoints.
- Een actieve Spotify Connect stream. Start eventueel eerst playback in de Spotify app.
- PlatformIO.

## Spotify app maken

1. Maak een app op https://developer.spotify.com/dashboard.
2. Voeg deze Redirect URI toe aan de app:

   ```text
   http://127.0.0.1:8888/callback
   ```

3. Haal je refresh token op. Je hebt hiervoor alleen de client ID nodig; zet je client secret niet op de ESP32.

   ```bash
   cd spotify-connect-remote
   python3 scripts/spotify_pkce_refresh_token.py --client-id YOUR_SPOTIFY_CLIENT_ID
   ```

4. Kopieer `include/Secrets.example.h` naar `include/Secrets.h` en vul WiFi, client ID en refresh token in.

## Refresh token opslag

Spotify kan tijdens een token-refresh een nieuwe refresh token teruggeven. De firmware slaat zo'n nieuwe token op in NVS flash storage en gebruikt bij boot eerst de opgeslagen token. Daardoor blijft de remote werken na een reboot of gewone firmware-upload.

Als je `Token invalid_grant` ziet:

1. Genereer een nieuwe refresh token met `scripts/spotify_pkce_refresh_token.py`.
2. Zet die token in `include/Secrets.h`.
3. Upload de firmware opnieuw.

De firmware probeert bij een opgeslagen maar ongeldige NVS-token automatisch nog een keer de token uit `Secrets.h`. Gebruik geen uploadoptie die de hele flash wist, tenzij je daarna opnieuw een geldige refresh token in `Secrets.h` zet.

## Build en upload

```bash
cd spotify-connect-remote
pio run -e t_embed_cc1101
pio run -e t_embed_cc1101 -t upload
pio device monitor
```

Als `pio` niet in je shell-PATH staat maar PlatformIO via VS Code is geinstalleerd, gebruik dan:

```bash
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101 -t upload
```

## Unit tests

De host-side tests draaien zonder ESP32-hardware en testen pure logica uit `LogicHelpers`.

```bash
c++ -std=c++17 -Iinclude test/native/test_logic.cpp -o /tmp/spotify_remote_unit_tests
/tmp/spotify_remote_unit_tests
```

## Pin mapping

De firmware gebruikt de pinout uit de LilyGO T-Embed-CC1101 documentatie:

- Display ST7789: CS 41, MOSI 9, SCLK 11, DC 16, RST 40, BL 21.
- Encoder: A 4, B 5, knop 0.
- Zijknop: 6.
- WS2812 LED-ring: IO14, 8 LEDs.
- Battery gauge BQ27220: I2C SDA 8, SCL 18, adres 0x55.
- Power enable: 15.
- SD CS 13 en CC1101/Lora CS 12 worden hoog gezet om SPI-busconflicten te vermijden.

## Code opbouw

- `src/main.cpp`: alleen Arduino `setup()` en `loop()`.
- `SpotifyDJApp`: centrale orkestratie van input, display, Spotify, batterij en refresh-timers.
- `SpotifyClient`: Spotify auth, Web API requests, playback control en async volume worker.
- `DisplayManager`: alle schermweergave, menu-rendering, title/artist scrolling, progress/time en backlight dimming.
- `InputController`: rotary encoder en knoppen als high-level events.
- `BatteryMonitor`: BQ27220 fuel-gauge reads.
- `LedRing`: groene WS2812 volume-indicator.
- `Config`: pinout en timingconstanten.

## TLS

De firmware valideert Spotify HTTPS standaard met de DigiCert Global Root G2 root-CA. Laat dit zo staan in `include/Secrets.h`:

```cpp
#define SPOTIFY_ALLOW_INSECURE_TLS 0
```

Alleen voor tijdelijke troubleshooting kun je dit op `1` zetten. Bij secure TLS synchroniseert de ESP32 na WiFi-connect eerst de klok via NTP, omdat certificaatvalidatie een correcte datum/tijd nodig heeft.

## Opmerkingen

- Als Spotify in de toekomst van root-CA wisselt, moet de embedded CA in `src/SpotifyClient.cpp` worden bijgewerkt.
- Sommige Spotify Connect devices ondersteunen geen volume control via de API. De firmware laat dan `Volume not supported` zien.
- Als de Spotify API `PREMIUM_REQUIRED` teruggeeft, gebruik je geen Premium account of probeert Spotify het commando op een device/context waar playback control niet beschikbaar is.

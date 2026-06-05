# AGENTS.md

Guidance for coding agents working on the SpotifyDJ LilyGO firmware.

## Project Overview

SpotifyDJ is an Arduino/PlatformIO firmware for the LilyGO T-Embed-CC1101 / ESP32-S3. It is a Spotify Connect remote with:

- TFT now-playing UI, menus, logs, album art, queue and sound output screens.
- Rotary encoder and top-button controls.
- Battery/charging guards, turn-off sleep and reset handling.
- WS2812 LED ring feedback.
- Local mobile web portal.
- MQTT/Home Assistant state publishing.
- Home Assistant device-layer support: pairing, mDNS discovery, device API, status posting and OTA trigger endpoints.

The current PlatformIO environment is `t_embed_cc1101`.

## Licensing

This firmware repository is proprietary closed-source software under `LICENSE`.
Do not add open-source license headers or wording that grants redistribution,
modification, reverse-engineering, or source-code rights for the firmware unless
the user explicitly changes the licensing model. The Home Assistant integration
may be offered free of charge separately, but that does not change the firmware
license.

Legal/trademark copy must stay consistent across README, web portal, device About
screen and third-party notices:

- `Copyright (c) 2026 Peter van Tol. All rights reserved.`
- `SpotifyDJ firmware is proprietary software.`
- `Spotify is a trademark of Spotify AB. SpotifyDJ is not affiliated with,
  endorsed by, or sponsored by Spotify AB.`
- Open-source dependency notices belong in `THIRD_PARTY_NOTICES.md`; dependency
  licenses remain with their respective authors.

Do not imply Spotify endorsement, sponsorship, certification, or affiliation.

## Important Paths

- `platformio.ini`: PlatformIO board/env/build flags.
- `src/main.cpp`: tiny Arduino entrypoint; keep real behavior in classes.
- `include/SpotifyDJApp.h`, `src/SpotifyDJApp.cpp`: top-level app orchestration.
- `include/Config.h`: shared constants, version, pins and timing.
- `include/AppState.h`: shared state structs.
- `include/LogicHelpers.h`: pure helper functions with native unit tests.
- `include/SpotifyDJMenuModel.h`: pure menu counts/options with native unit tests.
- `include/ProvisioningController.h`, `src/ProvisioningController.cpp`: NVS provisioning keys for WiFi, MQTT, setup mode, display settings, language/theme and cue volume.
- `include/PowerController.h`, `src/PowerController.cpp`: charger policy, deep-sleep wake policy and watchdog setup/feed.
- `include/NetworkActivityLogic.h`: testable network-timeout helper used by `NetworkActivity`.
- `test/native/test_logic.cpp`: host-side unit tests.
- `test/native/test_release.sh`: release-script dry-run and invalid-version checks.
- `include/Secrets.h`: optional non-secret build flags only. Do not put WiFi or Spotify credentials here.
- `include/Secrets.example.h`: template for optional build flags.
- `src/SpotifyDJDevice.*`, `src/SpotifyDJDiscovery.*`, `src/SpotifyDJPairing.*`, `src/SpotifyDJApiServer.*`, `src/SpotifyDJOTA.*`: Home Assistant device-layer modules.
- `src/SpotifyDJAssistClient.*`, `include/VoiceRecorder.h`, `src/VoiceRecorder.cpp`, `include/VoiceHttpClient.h`, `src/VoiceHttpClient.cpp`: push-to-talk Assist STT and recognized-text command flow.
- `src/WebPortal.cpp`: embedded mobile web UI, diagnostics, settings, logs and HA pairing panel.
- `include/SoundManager.h`, `src/SoundManager.cpp`: generated built-in speaker cues and cue volume scaling.
- `README.md`: user-facing English project documentation. Keep it in sync when behavior, setup, endpoints or release flow changes.
- `THIRD_PARTY_NOTICES.md`: third-party dependency notices for firmware libraries and frameworks.

## Build And Test Commands

Run native tests first when changing pure logic:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/spotifydj_unit_tests
/tmp/spotifydj_unit_tests
```

Run release-script checks when touching release tooling:

```sh
bash test/native/test_release.sh
```

Build firmware:

```sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Build with an explicit release version:

```sh
SPOTIFYDJ_BUILD_FLAGS='-DSPOTIFYDJ_VERSION="X.Y.Z" -DSPOTIFYDJ_VERSION_TAG="vX.Y.Z"' \
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

Prepare a tagged firmware release from the repo root:

```sh
./release.sh X.Y.Z
```

Use dry-run before release work:

```sh
./release.sh X.Y.Z --dry-run
```

To also publish assets into the public firmware repo:

```sh
./release.sh X.Y.Z --publish-firmware-repo ../spotify-dj-firmware
```

The release script keeps local dev defaults as `dev` / `vdev` and injects the
release version through PlatformIO build flags. Do not hardcode release versions
into `include/Config.h`.

Expected recurring third-party warning:

- `esp32-hal-uart.c: warning: 'return' with no value...`

This warning comes from the Arduino ESP32 framework package and is not normally actionable in this project.

## Architecture Rules

Keep concerns separated:

- `SpotifyDJApp` orchestrates modules and owns the main loop flow.
- `SpotifyClient` owns Spotify Web API auth/playback/control calls.
- `DisplayManager` owns drawing only. Do not put network or NVS logic in it.
- `WebPortal` owns the existing mobile dashboard and shared port-80 `WebServer`.
- Home Assistant device-layer code belongs in the `SpotifyDJ*` modules under `src/`.
- `LogicHelpers.h` is for pure, host-testable calculations.
- `ProvisioningController` owns provisioning/NVS storage details. Do not add new WiFi, MQTT, setup-mode, display-setting, language/theme or cue-volume NVS reads/writes directly in `SpotifyDJApp`.
- `PowerController` owns charger/wake/watchdog policy. Keep low-battery rendering in the app/display layer, but keep wake masks, timer-wake decisions and watchdog setup/feed out of `SpotifyDJApp`.
- `SpotifyDJMenuModel` is the host-testable source for menu counts and option values. Keep display labels in `SpotifyDJMenu`/i18n and pure counts/options in the model.
- `NetworkActivityLogic` is the host-testable timeout/reuse helper for long HTTP flows.
- Long/blocking HTTP flows should use `NetworkActivity` or a documented equivalent guard with explicit connect/read timeouts, progress logging where useful, and loop/watchdog yielding for large transfers.
- `BatteryMonitor` reads raw battery data and applies the voltage-based battery estimate.
- `LedRing` owns LED-ring presentation. Keep display brightness policy and LED power behavior coordinated through existing app/display methods.
- User-facing display, captive portal, and webportal strings should go through the language/i18n path where practical. Supported languages are English (`en`) and Dutch (`nl`); unknown values fall back to English. Logs intentionally remain English and must not be translated.
- App logs should use the timestamp prefix only. Do not add `[SpotifyDJ]` to new log messages; the central logger strips that legacy prefix when it appears at the start of older callsites.

Prefer extending existing modules over introducing new global state. Keep `src/main.cpp` small.

## Home Assistant Device Layer

The HA custom integration uses:

- Domain: `spotify_dj`
- HA pairing endpoint: `POST /api/spotify_dj/pair`
- HA status endpoint: `POST /api/spotify_dj/status`
- HA voice endpoint: `POST /api/spotify_dj/voice`
- ESP OTA endpoint: `POST /api/device/ota`
- ESP Spotify provisioning endpoint: `POST /api/device/provision_spotify`
- ESP DJ response endpoint: `POST /api/device/dj_response`

Local ESP endpoints currently include:

- `GET /api/device/info`
- `GET /api/device/pairing-info`
- `POST /api/device/pair`
- `POST /api/device/provision_spotify`
- `POST /api/device/dj_response`

DJ response playback rules:

- Required JSON field: `text`.
- Optional JSON field: `audio_url`.
- `audio_url` must point to PCM WAV audio if the ESP should speak it through `SoundManager::playWavStream()`.
- Do not add MP3/Opus decoding unless explicitly requested; let Home Assistant generate a compatible WAV.
- If no playable audio is supplied, return success with `spoken:false` and still display the text.
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`

The local device API registers routes on the existing `WebPortal` `WebServer` instance. Do not start a second port-80 server.

Protected local endpoints must require `Authorization: Bearer <device_token>`. Open endpoints are limited to device info and pairing info unless the user explicitly changes the pairing flow.

HA may provision UI language through `device_language` with `language` as fallback. Accept only `en` and `nl`, save valid values to `provision.language`, and apply them at runtime when possible. Ignore unknown language values without changing local menu-selected language.

HA may provision Spotify OAuth credentials during pairing, status responses, or `POST /api/device/provision_spotify`. Use `SpotifyProvisioning::parseCredentials()` for every path. Support top-level `spotify_client_id`/`spotify_refresh_token`/`spotify_market`, compat top-level `client_id`/`refresh_token`/`market`, and nested `spotify` equivalents. Prefer nested `spotify.spotify_refresh_token`, then `spotify.refresh_token`, then top-level `spotify_refresh_token`, then top-level `refresh_token`; client id follows the same priority. If either client id or refresh token is missing, leave existing stored credentials untouched. Never log refresh tokens.

The web portal may repair Spotify OAuth credentials manually. Keep the refresh token write-only in the UI: do not render existing tokens, clear submitted fields after the request, store credentials in the `spotifydj` NVS namespace only, and immediately run an authorization test. Do not reintroduce the old `spotify/refresh` NVS namespace.

mDNS:

- Hostname: `spotifydj-lilygo-XXXXXXXXXXXX`
- Service: `_spotifydj._tcp`
- URL: `http://spotifydj-lilygo-XXXXXXXXXXXX.local`
- TXT records include `name`, `device_id`, `version`, `paired`, `api`, and `model`.

## Secrets And Credential Policy

Do not compile user secrets into firmware.

Forbidden in firmware source:

- WiFi SSID
- WiFi password
- Spotify client id
- Spotify refresh token
- Spotify client secret
- Home Assistant device token

Credentials are provisioned and stored in NVS.

NVS namespaces:

- `provision`: existing device provisioning/settings, including display timeouts/brightness, speaker cue volume, WiFi and MQTT.
- `spotify`: rotated Spotify token cache.
- `spotifydj`: Home Assistant device-layer storage.

`spotifydj` keys:

- `ha_url`
- `device_token`
- `device_name`
- `sp_client`
- `sp_refresh`
- `sp_market`
- `fw_channel`
- `assist_pipe`

Keep ESP32 Preferences keys at 15 characters or less. External Home Assistant JSON payload field names may remain long, for example `spotify_refresh_token`; only the internal NVS key must be shortened.

The public ESP API Postman collection lives at `postman/SpotifyDJ ESP API.postman_collection.json`. Keep all credentials as variables/placeholders; never commit real device tokens, Spotify refresh tokens, MQTT passwords or Home Assistant URLs that identify a private instance.

## Architecture Decisions

- Treat Home Assistant as the trusted backend for pairing, Spotify command interpretation, Assist STT/TTS, OTA offer handling and optional MQTT provisioning.
- Keep the ESP focused on local edge behavior: display, buttons/encoder, LED ring, battery/power policy, speaker cues, microphone capture and playback of HA-provided DJ response audio.
- Keep PTT on Route B: ESP streams raw PCM16 to HA Assist, sends recognized text to the SpotifyDJ integration, then displays/plays the returned DJ response. Do not add direct OpenAI calls or browser microphone uploads.
- Pairing validity is runtime state. On HA 401/403/404, mark Home Assistant as stale/red and disable PTT, but do not erase NVS pairing automatically.
- Keep external JSON payload names compatible with HA, but keep internal ESP32 Preferences keys at 15 characters or less.
- Route slow network operations through explicit timeout/backoff policy and avoid making input/display responsiveness depend on an unbounded HTTP call.
- Keep release binaries/manifests separate from the closed-source firmware source repo workflow.
- Device stress/monkey testing must stay local and non-destructive: render/navigation only, no OTA, factory reset, WiFi changes, Spotify playback mutations or credential changes.

Never log:

- Spotify refresh token
- HA device token
- WiFi password
- Spotify client secret

It is OK to log the six-digit Home Assistant pairing code. It is intentionally short-lived and user-visible on the device.

Do not introduce a Spotify client secret. The firmware should use PKCE/public-client style credentials only.

## Voice / Assist Rules

Push-to-talk uses Route B:

- The ESP streams raw PCM16 mono 16 kHz microphone chunks to Home Assistant Assist over `/api/websocket`.
- Start the HA pipeline with `assist_pipeline/run`, `start_stage=stt`, `end_stage=stt`.
- Binary audio frames must be prefixed with the `stt_binary_handler_id` from the HA `run-start` event.
- End audio by sending a binary frame containing only the `stt_binary_handler_id`.
- Do not upload WAV audio to `/api/spotify_dj/voice` for STT.
- `/api/spotify_dj/voice` receives recognized text only, via `X-SpotifyDJ-Text` and JSON body `{ "text": "..." }`.
- The HA integration is responsible for Spotify command handling, `dj_text`, and optional backend TTS generation.
- HA pushes DJ response text plus optional PCM WAV `audio_url` back to the ESP with `POST /api/device/dj_response`.
- The ESP displays the DJ text, plays compatible PCM WAV through `SoundManager::playWavStream()`, and then returns to the normal UI.
- Web portal PTT is a compact simulation button: it sends a fixed localized test command to the ESP `/api/voice-text` proxy. It requires WiFi plus successful Home Assistant pairing/device token, but must not depend on Spotify credentials or active playback. Do not upload browser WAV audio to the ESP.
- If `/api/spotify_dj/voice` returns 404, treat it as a missing/removed Home Assistant integration route or stale ESP pairing. Surface a reset-pairing/setup-again message instead of implying a Spotify credential problem.
- Treat HA endpoint 401, 403 and 404 responses as runtime-invalid pairing for status/PTT flows. Mark indicators stale/red and instruct reset pairing, but do not automatically erase stored pairing from NVS.
- Optional wake-word support must remain inert without a linked model hook. A trained model may expose `extern "C" bool spotifydj_micro_wake_word_detect(const int16_t *samples, size_t sampleCount);`; the detector must be fast, local-only, and must not perform network I/O from the audio poll path.
- Do not call OpenAI directly from ESP firmware.
- Keep `SPOTIFYDJ_DEBUG_TEXT_COMMAND` available as a compile-time fixed-text fallback only.

## Pairing Mode Rules

When WiFi is configured but Home Assistant is not paired:

- Show pairing code on the display with SpotifyDJ logo/name, battery indicator, instruction text, and a large readable code.
- Keep display brightness at 100%.
- Keep pairing mode active for 10 minutes.
- After 10 minutes, turn off through ESP32 deep sleep.
- Keep local web/API handling alive.
- Do not process normal playback/menu button actions.
- Soft reset and hard reset must remain available through the reset monitor.
- The pairing code should also be available in Serial logging and the web pairing panel.

If WiFi is not configured, the device starts in setup/AP provisioning mode before HA pairing can happen.

If configured WiFi cannot connect during boot, keep the screen at 100% and show the WiFi-failure menu. Rotary selects between retry connect, hard reset, reset device, and turn off; center press executes the selected action. Do not start normal playback/menu handling while this recovery menu is active.

BLE setup mode is active only while the captive portal is active. iOS cannot automatically expose the currently connected WiFi password to the ESP32; BLE provisioning requires an app/flow to write JSON WiFi credentials to the SpotifyDJ BLE characteristic. Home Assistant Bluetooth Proxy flows should write to `7f705001-9f8f-4f1a-9b5f-570071fd0001` and read/subscribe to status characteristic `7f705002-9f8f-4f1a-9b5f-570071fd0001`. Keep BLE provisioning WiFi-only; Spotify and MQTT stay in Home Assistant provisioning, captive portal, or web settings. Do not claim automatic iPhone credential extraction is possible.

## Display And UI Rules

Do not redesign the UI casually. Changes should be targeted.

Display is 320x170 landscape. Before changing text placement, consider the physical bezel and keep important content well inside the edges.

Important current UI details:

- App name is `SpotifyDJ`.
- Default device/web theme is `dark`. `Light` uses TFT inversion/high contrast on the device. `Auto` is mainly useful for the web portal, where it follows browser/device preference.
- Changing the theme from the device or web settings intentionally saves and restarts the device so the display theme is applied cleanly from boot.
- Now-playing title color is bright yellow.
- Artist/show text is light grey.
- Track progress bar is green.
- Playing time text and volume bar use the purple accent.
- Battery percentages are shown without a leading tilde, even when voltage-estimated.

Current statusbar badges:

- `H`: Home Assistant paired
- `M`: MQTT connected
- `S`: Spotify authorized

Green means OK; red means not OK.

Brightness/power behavior is intentionally nuanced:

- Normal idle dim/off policy is handled by `DisplayManager` and `SpotifyDJApp::updateVisualPower()`.
- Default idle policy: dim after 10 seconds, reach 50% after 20 seconds, screen off after selected timeout.
- Setup/AP and HA pairing modes force their own brightness behavior.
- Low-battery and charging guards override normal UI behavior.
- When screen-off wake handling changes, first button/encoder action should wake the screen only and not execute the underlying playback/menu action.

## OTA Rules

OTA uses the `default_16MB.csv` partition table, which includes `ota_0` and `ota_1`.

Device OTA endpoint:

- `POST /api/device/ota`
- Requires `Authorization: Bearer <device_token>`.
- Expects JSON with `url`, `sha256`, `version`, and `device`.
- `device` must be `lilygo-t-embed-s3`.
- Battery must be above 40%, or charging/full, if battery state is available.

Current OTA implementation streams via `Update.h`.

SHA256 verification is currently marked as TODO/log warning if a hash is supplied. Do not claim cryptographic validation is complete until implemented.

During OTA firmware write, show `Firmware update in progress..` on the display at 100% brightness. The HTTP response is sent before the blocking write/reboot flow.

## MQTT Rules

MQTT is separate from the HA device API. Do not conflate the two.

MQTT publishes retained state and Home Assistant discovery for dashboard status. HA pairing/status uses HTTP endpoints and device token auth.

MQTT connection failures should not block Spotify controls or local web UI.

MQTT is two-way:

- Device status publishes retained to `spotifydj/<device_id>/status`.
- Commands are subscribed on `spotifydj/<device_id>/command` plus specific command topics under `spotifydj/<device_id>/command/`.
- Do not perform blocking Spotify HTTPS work inside the MQTT callback; queue an `MqttCommand` and let `SpotifyDJApp::loop()` execute it.
- Keep Home Assistant MQTT discovery aligned with command topics for buttons, number and selects.
- Current two-way MQTT scope includes Spotify-facing controls, status, next/previous, volume, sound output, playlist start, status command, OTA command placeholder, DJ response command, and settings commands for brightness, dim timeout, turn-off timeout, speaker cue volume, language, and theme.
- HA-provisioned MQTT settings live in `spotifydj` NVS keys `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass` and take precedence over legacy `provision` MQTT settings.
- After three consecutive MQTT auth failures (`rc=4` or `rc=5`), stop reconnect attempts and report `Auth failed; update MQTT credentials`. Reset that lock only when MQTT host/port/username/password changes or after restart.
- Captive portal MQTT settings are optional. Empty host means keep existing config and do not attempt MQTT setup.
- Never log MQTT passwords.

MQTT/HA/Spotify status indicators:

- Device statusbar: `H`, `M`, `S`.
- Web header mirrors the same indicators.
- LED ring may be red when critical connectivity is unhealthy; preserve existing priority rules with low-battery/setup animations.

## Volume Rules

Spotify volume is intentionally capped:

- User-facing volume range: `0` to `60`.
- Encoder volume commands clamp to `Config::MaxSpotifyVolumePercent`.
- Web slider max is `60`.
- LED ring treats `60` as full.
- Disable Spotify volume controls when there is no active playback or the active output does not support volume.

Spotify play mode is controlled through `SpotifyClient::setPlayMode()`:

- `normal`: shuffle off, repeat off.
- `shuffle`: shuffle on, repeat off.
- `repeat_once`: shuffle off, repeat track.
- `repeat_infinite`: shuffle off, repeat context.
- Keep device menu and web settings labels aligned when changing these modes.

Built-in speaker cue volume is separate from Spotify volume:

- Settings expose 25%, 50%, 75% and 100%.
- The value is stored in `provision` and cleared by factory reset.
- Apply it to all generated speaker cues, including startup after settings have loaded.

Do not restore `0-100` behavior unless the user explicitly asks.

## Battery And Sleep Rules

Respect existing low-battery behavior:

- Warning sound when battery crosses from above 20% to 20%.
- Below 20%: show charge screen and block normal inputs.
- Below 10%: critical charge flow and deep sleep.
- Charging guard blocks normal WiFi/Spotify behavior until battery recovery threshold.
- Deep sleep uses button wake plus a periodic charger probe timer. Without a known VBUS RTC GPIO this is near-auto USB-C wake, not instant hardware wake.

Battery percentage is always voltage-estimated using `LogicHelpers::batteryPercentFromVoltage`. Do not reintroduce BQ27220 state-of-charge as the primary displayed percentage unless the user explicitly asks.

Hard reset is allowed only above the configured battery threshold. Soft reset has its own lower threshold. Check `SoftResetMonitor` before changing reset behavior.

## Web Portal Rules

The web portal is intended to be usable on mobile. Keep controls responsive and avoid layout shifts while polling status.

Current web expectations:

- Pairing info panel shows device ID, code, mDNS URL, service, firmware, model and HA URL/status.
- The Home Assistant pairing banner setup link must open in a new tab/window so the local ESP web portal remains loaded.
- The top web status bar is right-aligned and follows the device order: H, M, S, WiFi signal bars, CSS battery icon. Keep the IP address in the WiFi block, not in the top status bar.
- The Home Assistant URL label in the web portal is `URL`, not `HA URL`.
- Album art is shown when available.
- Volume slider range is `0-60`.
- Volume slider is disabled when no track/playback is active.
- Web Now Playing includes previous, next, play and pause controls with compact CSS icons.
- Sound output selection uses a combobox/list without device type suffixes. Keep `None`/`Geen` and `iPhone` as fixed first entries before live Spotify Connect devices on both device and web UI.
- Logs support pause/resume and copy/select-all behavior. Logs should poll only while the logs panel is visible and not paused.
- Queue, playlist and sound-output list requests should be visibility-aware so hidden panels do not keep polling.
- Highlight error-like web status messages with the alert/error status styling so long HA pairing/voice endpoint failures are obvious.
- Root/status/log API responses should stay uncached; embedded static icon and manifest assets should keep explicit browser cache headers.
- WiFi credentials can be updated from the web UI without hard reset.
- Do not show a WiFi password placeholder field with masked stored password.
- When language is Dutch, the new WiFi password placeholder should be Dutch (`leeg laten om huidige te behouden`).
- Context help for screen-off timeout belongs directly under the `Screen dim timeout` / `Scherm uit na` select.
- A compact legal block should remain visible in the web portal, covering copyright, Spotify trademark/non-affiliation and open-source component notices.

## Legal And Third-Party Notices

Keep these surfaces aligned:

- `LICENSE`: proprietary firmware license.
- `README.md`: legal/trademark section and security checklist.
- `THIRD_PARTY_NOTICES.md`: dependency notices.
- Device About screen: compact copyright/proprietary/trademark/not-affiliated/OSS notice rows.
- Web portal Legal panel: copyright, Spotify trademark/non-affiliation and OSS components.

When adding libraries, update `platformio.ini` and `THIRD_PARTY_NOTICES.md`
together. Use factual dependency names and avoid guessing license terms beyond
what the package declares. If uncertain, write a conservative notice and mark
the exact license as needing verification.

## Coding Style

- Use `rg` for search.
- Use `apply_patch` for manual edits.
- Keep changes scoped.
- Prefer existing classes and patterns.
- Keep comments useful and concise.
- Avoid logging secrets.
- Use Arduino/ESP32 style APIs already present in the project.
- Do not rewrite Spotify/audio/display pipelines when implementing HA-device features.
- Do not add unrelated refactors.

## Documentation Rules

Keep `README.md` and `AGENTS.md` aligned with implemented behavior.

README should document:

- First-run AP setup and HA pairing.
- No hardcoded WiFi/Spotify secrets.
- Spotify refresh token generation via PKCE.
- Web portal capabilities.
- MQTT/HA behavior.
- OTA release flow through the GitHub repo `pcvantol/spotify-dj-firmware` using a git tag.
- Current build/upload/test commands.
- Legal/trademark notice and reference to `THIRD_PARTY_NOTICES.md`.

## Verification Checklist

Before finalizing firmware changes:

1. Run native tests when logic helpers changed.
2. Run `bash test/native/test_release.sh` when release tooling changed.
3. Run PlatformIO build.
4. Scan for accidental secrets when credential/provisioning code changed:

```sh
rg -n "SPOTIFY_CLIENT_ID|SPOTIFY_REFRESH_TOKEN|WIFI_SSID|WIFI_PASSWORD|client_secret|wifi144iot|verbindmet|AQB|5ea462" include src test -S
```

5. Mention any remaining known TODOs, especially OTA SHA256 verification.

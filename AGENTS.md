# AGENTS.md

Guidance for coding agents working on the DJConnect LilyGO firmware.

## Project Overview

DJConnect is an Arduino/PlatformIO firmware for the LilyGO T-Embed-CC1101 / ESP32-S3. It is a Home Assistant paired playback remote with:

- TFT now-playing UI, menus, logs, album art, queue and sound output screens.
- Rotary encoder and top-button controls.
- Battery/charging guards, turn-off sleep and reset handling.
- WS2812 LED ring feedback.
- Local mobile web portal.
- Home Assistant status publishing and native device command handling.
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
- `DJConnect firmware is proprietary software.`
- `Spotify is a trademark of Spotify AB. DJConnect is not affiliated with,
  endorsed by, or sponsored by Spotify AB.`
- Open-source dependency notices belong in `THIRD_PARTY_NOTICES.md`; dependency
  licenses remain with their respective authors.

Do not imply Spotify endorsement, sponsorship, certification, or affiliation.

## Important Paths

- `platformio.ini`: PlatformIO board/env/build flags.
- `src/main.cpp`: tiny Arduino entrypoint; keep real behavior in classes.
- `include/DJConnectApp.h`, `src/DJConnectApp.cpp`: top-level app orchestration.
- `include/Config.h`: shared constants, version, pins and timing.
- `include/AppState.h`: shared state structs.
- `include/LogicHelpers.h`: pure helper functions with native unit tests.
- `include/DJConnectMenuModel.h`: pure menu counts/options with native unit tests.
- `include/ProvisioningController.h`, `src/ProvisioningController.cpp`: NVS provisioning keys for WiFi, setup mode, display settings, language/theme/log-level and cue volume.
- `include/DeviceCommandParser.h`: host-testable parser for Home Assistant native `/api/device/command` payloads.
- `include/PowerController.h`, `src/PowerController.cpp`: charger policy, deep-sleep wake policy and watchdog setup/feed.
- `include/NetworkActivityLogic.h`: testable network-timeout helper used by `NetworkActivity`.
- `test/native/test_logic.cpp`: host-side unit tests.
- `test/native/test_release.sh`: release-script dry-run and invalid-version checks.
- `include/Secrets.h`: optional non-secret build flags only. Do not put WiFi, Home Assistant or playback-backend credentials here.
- `include/Secrets.example.h`: template for optional build flags.
- `src/DJConnectDevice.*`, `src/DJConnectDiscovery.*`, `src/DJConnectPairing.*`, `src/DJConnectApiServer.*`, `src/DJConnectOTA.*`: Home Assistant device-layer modules.
- `include/VoiceRecorder.h`, `src/VoiceRecorder.cpp`, `include/VoiceHttpClient.h`, `src/VoiceHttpClient.cpp`: push-to-talk WAV recording and upload to the Home Assistant integration.
- Direct ESP Assist websocket code is intentionally absent; physical PTT must go through the HA integration voice endpoint.
- `src/WebPortal.cpp`: embedded mobile web UI, diagnostics, settings, logs and HA pairing panel.
- `include/SoundManager.h`, `src/SoundManager.cpp`: generated built-in speaker cues and cue volume scaling.
- `README.md`: user-facing English project documentation. Keep it in sync when behavior, setup, endpoints or release flow changes.
- `THIRD_PARTY_NOTICES.md`: third-party dependency notices for firmware libraries and frameworks.

## Build And Test Commands

Run native tests first when changing pure logic:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests
/tmp/djconnect_unit_tests
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
DJCONNECT_BUILD_FLAGS='-DDJCONNECT_VERSION=X.Y.Z -DDJCONNECT_VERSION_TAG=vX.Y.Z' \
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
./release.sh X.Y.Z --publish-firmware-repo ../djconnect-firmware
```

The release script keeps local dev defaults as `dev` / `vdev` and injects the
release version through PlatformIO build flags. Do not hardcode release versions
into `include/Config.h`.

Expected recurring third-party warning:

- `esp32-hal-uart.c: warning: 'return' with no value...`

This warning comes from the Arduino ESP32 framework package and is not normally actionable in this project.

## Architecture Rules

Keep concerns separated:

- `DJConnectApp` orchestrates modules and owns the main loop flow.
- `SpotifyClient` owns backend-agnostic playback proxy calls to Home Assistant.
- `DisplayManager` owns drawing only. Do not put network or NVS logic in it.
- `WebPortal` owns the existing mobile dashboard and shared port-80 `WebServer`.
- Home Assistant device-layer code belongs in the `DJConnect*` modules under `src/`.
- `LogicHelpers.h` is for pure, host-testable calculations.
- `ProvisioningController` owns provisioning/NVS storage details. Do not add new WiFi, setup-mode, display-setting, language/theme/log-level or cue-volume NVS reads/writes directly in `DJConnectApp`.
- `PowerController` owns charger/wake/watchdog policy. Keep low-battery rendering in the app/display layer, but keep wake masks, timer-wake decisions and watchdog setup/feed out of `DJConnectApp`.
- `DJConnectMenuModel` is the host-testable source for menu counts and option values. Keep display labels in `DJConnectMenu`/i18n and pure counts/options in the model.
- `NetworkActivityLogic` is the host-testable timeout/reuse helper for long HTTP flows.
- Long/blocking HTTP flows should use `NetworkActivity` or a documented equivalent guard with explicit connect/read timeouts, progress logging where useful, and loop/watchdog yielding for large transfers.
- `BatteryMonitor` reads raw battery data and applies the voltage-based battery estimate.
- `LedRing` owns LED-ring presentation. Keep display brightness policy and LED power behavior coordinated through existing app/display methods.
- User-facing display, captive portal, and webportal strings should go through the language/i18n path where practical. Supported languages are English (`en`) and Dutch (`nl`); unknown values fall back to English. Logs intentionally remain English and must not be translated. Loglevel UI labels still need translated strings.
- App logs are centrally formatted as `HH:mm INF ...`, `HH:mm WRN ...`, `HH:mm ERR ...` or `HH:mm DBG ...`. Do not manually add timestamps, severity labels, or `[DJConnect]` to new log messages; the central logger strips that prefix when it appears at the start of older callsites.

Prefer extending existing modules over introducing new global state. Keep `src/main.cpp` small.

## Home Assistant Device Layer

The HA custom integration uses:

- Domain: `djconnect`
- HA pairing endpoint: `POST /api/djconnect/pair`
- HA status endpoint: `POST /api/djconnect/status`
- HA voice endpoint: `POST /api/djconnect/voice`
- ESP OTA endpoint: `POST /api/device/ota`
- ESP DJ response endpoint: `POST /api/device/dj_response`

Periodic HA status payloads must carry the ESP device settings that native HA entities mirror: pairing status, local URL, screen brightness, screen timeout, turn-off timeout, speaker cue volume, language, theme, log level, OTA/update state, screen state and LED state. Keep the top-level fields and the nested `settings`, `screen` and `led` objects synchronized with the HA integration contract. Required names include `ha_pairing_status`, `local_url`, `ha_local_url`, `ha_remote_url`, `ha_active_url`, `screen_brightness`/`brightness`, `screen_dim_timeout_ms`, `turn_off_after_ms`, `speaker_volume`/`cue_volume`, `language`, `theme`, `log_level`, `ota_state` and `update_state`.

For `/api/djconnect/command`, keep auth failures distinct from playback-backend failures. HTTP 401/403/404 means stale pairing. Backend/player unavailability should be represented as HTTP 200 with `success:false` and `backend_available:false`; the ESP will show a red playback indicator without clearing pairing.

Local ESP endpoints currently include:

- `GET /api/device/info`
- `GET /api/device/pairing-info`
- `POST /api/device/pair`
- `POST /api/device/dj_response`

DJ response playback rules:

- Required JSON field: `text`.
- Optional JSON field: `audio_url`.
- `audio_url` may point to PCM WAV or MP3 audio. Detect by `Content-Type` first and magic bytes as fallback.
- WAV stays on `SoundManager::playWavStream()`; MP3 stays on `SoundManager::playMp3Stream()`. Do not rewrite the WAV pipeline when changing MP3 support.
- If no playable audio is supplied, return success with `spoken:false` and still display the text.
- `POST /api/device/ota`
- `POST /api/device/reboot`
- `POST /api/device/forget`

The local device API registers routes on the existing `WebPortal` `WebServer` instance. Do not start a second port-80 server.

Protected local endpoints must require `Authorization: Bearer <device_token>`. Open endpoints are limited to device info and pairing info unless the user explicitly changes the pairing flow.

HA may provision UI language through `device_language` with `language` as fallback. Accept only `en` and `nl`, save valid values to `provision.language`, and apply them at runtime when possible. Ignore unknown language values without changing local menu-selected language.

Do not store Spotify OAuth credentials, Sonos credentials or any playback-backend secrets on the ESP. Home Assistant owns backend credentials and translates generic ESP playback commands into backend-specific actions. The web portal and captive portal must not expose refresh-token/client-id forms.

mDNS:

- Hostname: `djconnect-lilygo-XXXXXXXXXXXX`
- Service: `_djconnect._tcp`
- URL: `http://djconnect-lilygo-XXXXXXXXXXXX.local`
- TXT records include `name`, `device_id`, `version`, `paired`, `api`, and `model`.

## Secrets And Credential Policy

Do not compile user secrets into firmware.

Forbidden in firmware source:

- WiFi SSID
- WiFi password
- Playback-backend client id
- Playback-backend refresh token
- Playback-backend client secret
- Home Assistant device token

Credentials are provisioned and stored in NVS.

NVS encryption is not active in the current Arduino/PlatformIO firmware build.
Do not present credential storage as encrypted unless all of these are true:

- The build enables `CONFIG_NVS_ENCRYPTION=y`.
- The active partition table contains an `nvs_keys` partition.
- The device was factory/serial flashed with that partition table and keys; OTA alone does not update the partition layout.
- Existing credentials were re-provisioned or migrated so old plaintext NVS entries are not kept.

If encrypted NVS becomes a release requirement, treat it as a factory-image
migration project rather than a small OTA firmware patch.

NVS namespaces:

- `provision`: existing device provisioning/settings, including display timeouts/brightness, speaker cue volume and WiFi.
- `djconnect`: Home Assistant device-layer storage.

`djconnect` keys:

- `ha_local_url`
- `ha_remote_url`
- `device_token`
- `device_name`
- `fw_channel`
- `assist_pipe`

Keep ESP32 Preferences keys at 15 characters or less. External Home Assistant JSON payload field names may remain longer, but playback-backend secret fields must not be accepted or persisted on the ESP.

The public ESP API Postman collection lives at `postman/DJConnect ESP API.postman_collection.json`. Keep all credentials as variables/placeholders; never commit real device tokens, playback-backend refresh tokens or Home Assistant URLs that identify a private instance.

## Architecture Decisions

- Treat Home Assistant as the trusted backend for pairing, generic playback command interpretation, backend credentials, Assist STT/TTS, OTA offer handling and native entity commands.
- Keep the ESP focused on local edge behavior: display, buttons/encoder, LED ring, battery/power policy, speaker cues, microphone capture and playback of HA-provided DJ response audio.
- Keep PTT on the integration-backed WAV-upload route: ESP records WAV, uploads it to `/api/djconnect/voice`, then displays/plays the returned DJ response. Do not add direct ESP Assist websocket auth, direct OpenAI calls or browser microphone uploads.
- Pairing validity is runtime state. On HA 401/403/404, mark Home Assistant as stale/red and disable PTT, but do not erase NVS pairing automatically.
- Keep external JSON payload names compatible with HA, but keep internal ESP32 Preferences keys at 15 characters or less.
- Route slow network operations through explicit timeout/backoff policy and avoid making input/display responsiveness depend on an unbounded HTTP call.
- Keep release binaries/manifests separate from the closed-source firmware source repo workflow.
- Device stress/monkey testing must stay local and non-destructive: render/navigation only, no OTA, factory reset, WiFi changes, playback mutations or credential changes.

Never log:

- Playback-backend refresh token
- HA device token
- WiFi password
- Playback-backend client secret

It is OK to log the six-digit Home Assistant pairing code. It is intentionally short-lived and user-visible on the device.

Do not introduce playback-backend secrets on the ESP. OAuth/PKCE, Sonos or future backend credentials belong in Home Assistant.

## Voice / Assist Rules

Physical push-to-talk from Now Playing uses the Home Assistant integration as the backend boundary:

- The ESP records mono 16 kHz PCM WAV to LittleFS while the encoder button is held on Now Playing.
- On release, the ESP uploads the WAV as raw request body to `/api/djconnect/voice` with `Content-Type: audio/wav`, `Authorization: Bearer <device_token>` and `X-DJConnect-Device-ID`.
- The Home Assistant integration/backend owns any Home Assistant core auth needed for Assist, STT or TTS. If Assist requires `/api/websocket`, that websocket connection belongs in the HA integration, not on the ESP.
- `/api/djconnect/voice` returns DJ text plus optional `audio_url`; the ESP displays the text and plays WAV/MP3 response audio when possible.
- Do not add direct ESP Assist websocket auth; the DJConnect device token is for the integration API, not Home Assistant core.
- Do not start physical PTT from Current Song/AlbumArt. Current Song is a read-only detail screen and uses the same top-button back behavior as menu screens.
- The web portal PTT simulation may still send a fixed localized text command to the ESP `/api/voice-text` proxy. It requires WiFi plus successful Home Assistant pairing/device token, but must not depend on backend credentials stored on the ESP or active playback. Do not upload browser WAV audio to the ESP.
- If `/api/djconnect/voice` returns 404, treat it as a missing/removed Home Assistant integration route or stale ESP pairing. Surface a reset-pairing/setup-again message instead of implying a Spotify credential problem.
- Treat HA endpoint 401, 403 and 404 responses as runtime-invalid pairing for status/PTT flows. Mark indicators stale/red and instruct reset pairing, but do not automatically erase stored pairing from NVS.
- Optional wake-word support must remain inert without a linked model hook. A trained `oke nabu` model may expose `extern "C" bool djconnect_oke_nabu_wake_word_detect(const int16_t *samples, size_t sampleCount);`; the legacy `djconnect_micro_wake_word_detect` hook remains supported. The detector must be fast, local-only, and must not perform network I/O from the audio poll path.
- Do not call OpenAI directly from ESP firmware.
- Keep `DJCONNECT_DEBUG_TEXT_COMMAND` available as a compile-time fixed-text fallback only.

## Pairing Mode Rules

When WiFi is configured but Home Assistant is not paired:

- Show pairing code on the display with DJConnect logo/name, battery indicator, instruction text, and a large readable code.
- Keep display brightness at 100%.
- Keep the blue Home Assistant pairing LED-ring breath active so the device visibly waits for pairing.
- Keep pairing mode active for 10 minutes.
- After 10 minutes, turn off through ESP32 deep sleep.
- Keep local web/API handling alive.
- Do not process normal playback/menu button actions.
- Show the center-button turn-off hint and allow center-button turn off.
- Consume top-button press/hold/long-click UI events in pairing mode so holding the top button for the 10-second soft reset never flashes the normal menu first.
- Soft reset and hard reset must remain available through the reset monitor.
- The pairing code should also be available in Serial logging and the web pairing panel.
- Home Assistant must not report the ESP as paired just because the integration generated a token locally. The ESP is paired only after it stores a device token. `/api/device/pair` may receive a direct HA callback with `device_token` plus `ha_local_url` and/or `ha_remote_url`; keep that route lightweight and only store token/settings there. The app loop confirms the pairing through `/api/djconnect/status`; playback proxy commands must stay disabled until that authenticated status call succeeds.
- HA calls from ESP use a local-first/cloud-fallback resolver: probe `ha_local_url`, then use `ha_remote_url` when local HA is not reachable. This is for ESP→HA outbound requests only; do not assume Nabu Casa can reach ESP local endpoints directly.

If WiFi is not configured, the device starts in setup/AP provisioning mode before HA pairing can happen.

If configured WiFi cannot connect during boot, keep the screen at 100% and show the WiFi-failure menu. Rotary selects between retry connect, restart device, turn off, and factory reset. Factory reset must stay at the bottom and require an explicit confirmation screen before wiping setup. Center press executes the selected action. Do not start normal playback/menu handling while this recovery menu is active.

BLE advertising is active while setup/AP mode is active and while Home Assistant pairing mode is active. iOS cannot automatically expose the currently connected WiFi password to the ESP32; BLE provisioning requires an app/flow to write JSON WiFi credentials to the DJConnect BLE characteristic. Home Assistant Bluetooth Proxy flows should write WiFi JSON to `7f705001-9f8f-4f1a-9b5f-570071fd0001` during setup/AP mode and read/subscribe to status characteristic `7f705002-9f8f-4f1a-9b5f-570071fd0001` for setup or pairing status. Keep BLE provisioning WiFi-only; playback credentials stay in Home Assistant. Do not claim automatic iPhone credential extraction is possible.

## Display And UI Rules

Do not redesign the UI casually. Changes should be targeted.

Display is 320x170 landscape. Before changing text placement, consider the physical bezel and keep important content well inside the edges.

Important current UI details:

- App name is `DJConnect`.
- Default device/web theme is `dark`. `Light` uses TFT inversion/high contrast on the device. `Auto` is mainly useful for the web portal, where it follows browser/device preference.
- Changing brightness, timeouts, cue volume, language, theme or log level from the device, web settings or HA commands applies live and saves to NVS. Do not restart for these settings; restart only for flows that explicitly require it such as WiFi changes, reset, OTA or user-requested reboot.
- Now-playing title color is bright yellow.
- Artist/show text is light grey.
- Track progress bar is green.
- Playing time text and volume bar use the purple accent.
- Battery percentages are shown without a leading tilde, even when voltage-estimated.

Current statusbar badges:

- `H`: Home Assistant paired
- `S`: Spotify authorized

Green means OK; red means not OK.

Brightness/power behavior is intentionally nuanced:

- Normal idle dim/off policy is handled by `DisplayManager` and `DJConnectApp::updateVisualPower()`.
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

SHA256 verification is mandatory for OTA. The endpoint rejects missing/invalid hashes, streams the image through `Update.h`, computes SHA256 while writing, and aborts before reboot if the manifest hash does not match.

During OTA firmware write, show `Firmware update in progress..` on the display at 100% brightness for both HA-triggered OTA and manual web upload. Keep the LED ring in the fast purple firmware-update animation and play OTA start/progress/complete/failure cues through `SoundManager`. OTA streaming must keep feeding the watchdog/LED animation and tolerate slow GitHub/CDN byte bursts before declaring a controlled stream timeout.

Local development builds may display `vdev`, but Home Assistant/device API version reporting must expose them as OTA-comparable `0.0.0` so every published `X.Y.Z` release is treated as an upgrade from a local flash.

Pre-pairing bootstrap OTA:

- Runs only after WiFi connects and before Home Assistant pairing setup when the device is not paired yet.
- Checks the latest public firmware release through the GitHub Releases API for `pcvantol/djconnect-firmware`.
- Must never auto-update local `dev` / `vdev` firmware. Dev builds are intentionally local and should only be replaced by explicit serial/web/HA update actions.
- Must use TLS verification for the GitHub release/manifest check. Do not use `setInsecure()` for deciding whether an update exists.
- Reuse `DJConnectOTA::performUpdate()` for the actual firmware write so display text, 100% brightness, purple LED-ring animation, speaker cues, SHA256 validation and reboot behavior remain consistent.
- If the bootstrap check fails, log the reason and continue gracefully into normal pairing/setup flow without notifying or blocking the user.

## Home Assistant Native Command Rules

Device status and commands use the Home Assistant integration HTTP boundary. Keep control paths on the authenticated DJConnect HTTP API.

Protected local ESP routes require `Authorization: Bearer <device_token>`.

The generic command endpoint is `POST /api/device/command`. Keep its parser in `DeviceCommandParser` so command behavior stays host-testable.

Current native command scope includes:

- status request;
- next/previous;
- volume;
- sound output transfer;
- playlist start;
- DJ response text;
- brightness;
- screen timeout;
- turn-off timeout;
- speaker cue volume;
- language;
- theme;
- log level.

Do not perform unbounded blocking Spotify HTTPS work inside the HTTP route itself. Parse and validate in `DJConnectApiServer`, then route through `DJConnectApp::handleDeviceCommand()` with existing timeout-aware Spotify helpers.

HA/Spotify status indicators:

- Device statusbar: `H`, `S`.
- Web header mirrors the same indicators.
- LED ring may be red when critical connectivity is unhealthy; preserve existing priority rules with low-battery/setup/pairing animations.
- Boot uses a calm startup rainbow lap. WiFi connect uses a green chase. Setup/AP uses a deeply fading rainbow breath. Home Assistant pairing uses a deeply fading blue breath. Turn off/deep sleep always plays a rainbow fade-out. Top-button soft reset plays a dedicated sound and bright white LED flashes before reboot.

## Volume Rules

Spotify volume is intentionally capped:

- User-facing volume range: `0` to `60`.
- Encoder volume commands clamp to `Config::MaxSpotifyVolumePercent`.
- Web slider max is `60`.
- LED ring treats `60` as full.
- Disable Spotify volume controls when there is no active playback or the active output does not support volume.

Shuffle and repeat are separate controls:

- Device menu and web portal expose separate Shuffle and Repeat controls.
- Home Assistant should use `set_shuffle` with boolean `value`.
- Home Assistant should use `set_repeat` with `value` `off`, `track` or `context`.
- Do not merge shuffle and repeat into one command and do not reintroduce Smart Shuffle; regular shuffle is the only supported shuffle command today.
- Keep device menu, web labels and HA command names aligned when changing these modes.

Local games are intentionally local-only:

- `Games` in the root menu opens Pong, Asteroids and Flyer.
- Game input/rendering belongs in `DJConnectApp` and `DisplayManager`; do not route game state through Home Assistant.

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

Hard reset is allowed only above the configured battery threshold. Soft reset has its own lower threshold. Check `SoftResetMonitor` before changing reset behavior. The top-button 10-second soft reset runs from the reset monitor task and should play the soft-reset cue plus white LED flashes before `ESP.restart()`.

## Web Portal Rules

The web portal is intended to be usable on mobile. Keep controls responsive and avoid layout shifts while polling status.

Current web expectations:

- Pairing info panel shows device ID, code, mDNS URL, service, firmware, model and active HA URL/status.
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
- Home Assistant owned playback backend provisioning.
- Web portal capabilities.
- Home Assistant command/status behavior.
- OTA release flow through the GitHub repo `pcvantol/djconnect-firmware` using a git tag.
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

5. If release cleanup tooling changed, run `bash -n scripts/cleanup_old_releases.sh`.

# Changelog

## v2.9.18

Consolidated SpotifyDJ firmware release for the LilyGO T-Embed-CC1101 / ESP32-S3.

### Added

- Home Assistant backed playback remote control with Now Playing, track progress, volume, pause/resume, next and previous controls.
- Device menus for Up Next, Playlists, Sound Outputs, Settings, About and Logs.
- Pong mini game from the device main menu.
- Play mode control on the device main menu and Now Playing web portal: normal, shuffle, repeat once and repeat infinite.
- Language setting for the device UI, web portal and captive portal with English and Dutch support.
- Theme setting for the device and web portal: Auto, Dark and Light.
- Playlist browsing and playlist start from both the device and web portal.
- Current Song screen with on-demand album art download/cache and scrolling title/artist text.
- Mobile web portal with Now Playing, DJ-response simulation, album art, volume slider, sound output selection, queue, logs, diagnostics, settings, WiFi update and OTA upload.
- Home Assistant device layer with pairing, mDNS discovery, device-token storage, status updates and OTA endpoint.
- Pre-pairing bootstrap firmware update check for freshly provisioned release devices, using the public GitHub firmware release manifest before Home Assistant pairing.
- Device API endpoint `/api/device/dj_response` for DJ text responses from the Home Assistant integration, with optional backend-generated WAV/MP3 playback on the ESP speaker.
- BLE WiFi provisioning in setup mode through a writable JSON characteristic.
- Home Assistant discovery, periodic status publishing and native two-way device commands.
- Native Home Assistant controls for volume, next/previous, sound output, playlist start, DJ response and settings through `/api/device/command`.
- Captive portal for WiFi provisioning.
- OTA release flow through GitHub tags and board-specific firmware assets.
- Proprietary firmware license for closed-source distribution on sold devices.
- WS2812 LED-ring feedback for volume, status, setup/AP mode, charging, connectivity and firmware update state.
- Speaker cues for boot, reset, battery warning, factory reset, charging completed, menu/back and push-to-talk start/stop.
- Built-in speaker cue volume setting: 25%, 50%, 75% or 100%.
- Battery/charging guard screens, low-battery turn-off sleep and charger-aware wake behavior.
- WiFi-failure boot menu with encoder selection for retry connect, restart device, turn off and confirmed factory reset.
- Optional local micro wake word hook for a trained `Spotify DJ` detector.
- Watchdog, slow-loop diagnostics and periodic heap diagnostics for long-running device stability.
- Timestamped log severity classification with `[inf]`, `[wrn]`, `[err]` and `[dbg]` markers for serial and web logs.
- Log level setting on the device, web portal and Home Assistant command API with translated UI labels and `info` as the default.
- `ProvisioningController` for centralized NVS provisioning storage and reduced `SpotifyDJApp` responsibility.
- `PowerController` for charger/wake/watchdog policy.
- Host-testable menu and network helper models plus release-script shell tests.
- `release.sh` helper for local firmware release preparation, dry-run validation, manifest generation, tagging and optional public firmware repo publishing.
- Postman collection for the public/local ESP device API.
- GitHub release cleanup helper to dry-run and delete older releases, tags and workflow runs while keeping the newest semver release/run.

### Changed

- Application name and technical branding are now `SpotifyDJ`.
- Release builds use `2.9.18` / `v2.9.18`; local builds without release flags remain `dev` / `vdev`.
- Boot logs now include the SpotifyDJ app name and active firmware version.
- Local `dev` / `vdev` firmware reports OTA-comparable version `0.0.0` to Home Assistant/device API so any published `X.Y.Z` firmware is treated as an upgrade.
- Local `dev` / `vdev` firmware is excluded from automatic pre-pairing bootstrap updates so development flashes stay local until explicitly updated.
- WiFi, playback-backend and Home Assistant secrets are no longer hardcoded in firmware.
- Playback control has moved to a backend-agnostic Home Assistant proxy. The ESP sends generic playback commands to HA and no longer stores Spotify OAuth credentials.
- Captive portal and web portal no longer include Spotify client-id or refresh-token fields.
- Legacy `sp_client`, `sp_refresh` and `sp_market` NVS keys are cleared at boot.
- Legacy `POST /api/device/provision_spotify` has been removed; playback-backend credentials are managed only in Home Assistant.
- Battery percentage is always voltage-estimated and displayed without a tilde.
- Playback volume is limited to `0-60`; the LED ring treats `60` as full scale and uses orange segments.
- Now Playing shows `H` and `S` status indicators for Home Assistant and playback.
- Pairing mode shows the SpotifyDJ logo/name, battery state, instruction text and a large pairing code.
- Pairing code is also visible in serial logs and the web interface.
- Setup/AP mode and Home Assistant pairing mode keep the screen at 100% brightness for 10 minutes, then turn off. Pairing mode also keeps BLE advertising active, shows a center-button turn-off hint and uses a deeply fading blue LED-ring breath.
- `/api/device/pair` now also accepts a direct Home Assistant callback with `ha_url`, `device_token`, Assist pipeline id and device language, while keeping the handler lightweight to avoid watchdog stalls during pairing.
- Playback proxy commands now wait for a successful authenticated Home Assistant status confirmation after boot/pairing, preventing stale or pending tokens from repeatedly sending playback 401 requests.
- Home Assistant playback proxy HTTP failures such as `HA playback HTTP -1` now make the device connectivity LED state red without erasing pairing data.
- OTA firmware write shows `Firmware update in progress..` on the display for both Home Assistant OTA and manual web upload, runs a fast purple LED-ring animation, and plays start/progress/complete/failure speaker cues.
- OTA download and manual firmware upload now explicitly service the ESP task watchdog while hashing and writing firmware chunks.
- Home Assistant status payloads now publish `state/status=online` plus `ota_state/update_state=idle` after boot so integrations can clear a stale OTA `updating` state.
- MP3 DJ-response playback now temporarily pauses the loop-task watchdog around the blocking decoder loop and restores it afterward.
- Normal boot LED-ring feedback is now a calm rainbow startup lap that fades back to off before setup/AP, WiFi or playback states take over.
- Turn-off/deep-sleep always plays a rainbow LED-ring fade-out; top-button soft reset plays a dedicated speaker cue and bright white LED-ring flashes before reboot.
- Display idle behavior keeps the configured brightness until the selected timeout, then turns the screen fully off.
- The first button/encoder action while the screen is off only wakes the screen and does not execute the underlying action.
- Device menu selected rows leave clear space for the right-side scrollbar.
- Web interface shows H/S status indicators, WiFi signal bars and a CSS battery icon with percentage/charging flash.
- Web statusbar order now matches the device: H, S, WiFi signal bars, battery.
- Web Now Playing includes previous, next, play and pause controls with compact CSS icons.
- Web interface keeps the status bar compact by showing the IP address in the WiFi details block instead of the header.
- Sound output lists on device and web always include `None`/`Geen` and `iPhone` before live backend outputs.
- Liked Proxy lookup now searches multiple pages of the user's own playlists before falling back to public playlist search.
- Up Next falls back to the current playlist tracks when Spotify's queue endpoint returns no upcoming tracks for playlist playback.
- The Home Assistant pairing banner setup link opens in a new browser tab.
- Firmware manifests now target `lilygo-t-embed-s3` while keeping the distributable binary asset name `spotifydj-device-vX.Y.Z.bin`, matching the ESP OTA endpoint validation.
- GitHub Actions now injects the release version into the PlatformIO build, verifies the compiled firmware contains `vX.Y.Z`, and publishes the single OTA asset `spotifydj-device-vX.Y.Z.bin`.
- The embedded web portal no longer contains a static `vdev` app-version placeholder in release binaries, and the release workflow rejects firmware assets that still contain that dev marker.
- `release.sh` now lets GitHub Actions publish the public firmware release by default; local GitHub release creation is an explicit `--gh-release` fallback.
- Web portal firmware upload no longer shows a temporary `Uploading firmware...` / `Firmware uploaden...` status label before the final upload response.
- Web interface logs can be paused and selected/copied.
- `Restart device` and `Turn off device` are available from settings.
- Push-to-talk now records a WAV file on the ESP and uploads it as raw `audio/wav` to the Home Assistant integration endpoint `/api/spotify_dj/voice`.
- The PTT flow is documented: ESP WAV upload to the HA integration, backend Assist/STT/TTS in Home Assistant, then DJ text plus optional WAV/MP3 URL back to the ESP device.
- Direct Home Assistant Assist WebSocket authentication has been removed from the physical PTT path; the websocket, if required, belongs on the Home Assistant integration backend.
- Encoder short press performs pause/resume and long press starts push-to-talk until release from Now Playing.
- Current Song moved into the first root menu item, keeps the same top-button back behavior as other menu screens, and no longer starts push-to-talk from encoder long press.
- Turn-off sleep periodically probes for USB-C charger attach; with a charger detected the device continues booting, otherwise it returns to sleep.
- Push-to-talk logs listening steps to serial and the web logs screen.
- LED-ring animations are yellow on PTT start, blue on PTT stop/processing, and green on accepted voice command response.
- The old recognized-text-only physical PTT path was replaced by raw WAV upload to `/api/spotify_dj/voice`; the web portal still keeps a compact text-based DJ-response simulation path.
- Voice status messages use `recording`, `sending_command` and `error`.
- DJ responses are displayed locally, optionally played and published as `last_dj_text` in runtime state.
- DJ response audio now supports MP3 streams in addition to PCM WAV, with content-type and magic-byte detection plus text-only fallback for unsupported audio.
- Web portal PTT is now a compact DJ-response simulation button that sends a fixed localized test command through the ESP to Home Assistant; it requires HA pairing but not playback-backend credentials on the ESP, active playback or browser microphone access.
- Stale Home Assistant pairing is reported clearly when the HA voice endpoint returns 404.
- Home Assistant status and push-to-talk calls now mark runtime pairing as stale on HA 401/403/404 responses without automatically deleting stored pairing data.
- Home Assistant voice endpoint stale-pairing messages are translated through the firmware language setting.
- The web portal no longer exposes backend credential repair because backend credentials live in Home Assistant.
- Architecture decisions are documented explicitly for Home Assistant, ESP edge behavior, integration-backed PTT, runtime pairing validity, NVS key limits, network timeout policy and release separation.
- When no music is playing, the device and web portal offer an action to start the `SpotifyDJ Liked Proxy` playlist.
- Volume control is disabled when there is no active playback.
- WiFi boot label is `Connecting to WiFi...`.
- WiFi boot connection timeout is 30 seconds and the LED ring shows a green connecting animation while the device attempts to join WiFi.
- WiFi-failure recovery now keeps factory reset at the bottom of the menu and requires an explicit confirmation screen before wiping setup.
- Setup/AP mode display now shows that the portal is active for 10 minutes and exposes a center-button turn-off action.
- The language setting is stored in NVS, can be provisioned by Home Assistant through `device_language`/`language`, and resets to English on factory reset; logs remain English.
- Theme is stored in NVS and exposed on the device and web portal. Device `Light` uses TFT inversion/high contrast; web `Auto` follows browser/device preference.
- Native Home Assistant commands cover playback controls, status refresh and settings commands for language, theme, log level, brightness, dim timeout, turn-off timeout and speaker cue volume.
- Device Settings menu includes a safe local Stress test / monkey mode for render/navigation diagnostics.
- Web portal polling is visibility-aware for logs, queue, playlists and sound-output lists, and embedded icon/manifest assets use cache headers.
- Error-like web status messages are visually highlighted so stale HA pairing/voice endpoint failures stand out.
- App logs are stored in fixed-size buffers to reduce heap fragmentation during long runs.
- Menu counts/options including log-level choices, network timeout behavior and release dry-run validation have stronger automated test coverage.
- Web battery header state has host-side test coverage for low/medium/high and charging classes.
- Provisioning, power policy and long-network-call responsibilities are documented as separate refactor boundaries to keep future production fixes easier to isolate.
- Credential storage security notes now explicitly document that true encrypted NVS requires an ESP-IDF/Arduino-as-component build with `CONFIG_NVS_ENCRYPTION=y` plus an `nvs_keys` partition and cannot be enabled safely as an OTA-only change.

### Fixed

- Reduced display flicker caused by unnecessary redraws.
- Volume actions no longer show HTTP 411 in the UI.
- Corrected encoder direction for volume.
- Legacy local OAuth token storage paths are disabled; playback credentials stay in Home Assistant.
- Display remains usable in error, pairing and OTA states.
- Settings such as brightness, screen dim timeout, speaker volume and turn-off timeout persist after reboot.
- Web interface no longer clears input fields during status polling.
- Web interface sound-output and playlist comboboxes no longer stay on loading text when playback is not connected.
- Liked Proxy playlist missing errors are translated through the firmware language setting.
- Web playback command responses such as playlist start and output switch are localized.
- Playlist lookup scope and backend playlist discovery are handled by the Home Assistant integration.
- Sound output types are no longer appended to output names.
- Improved JPEG album art rendering and Current Song text scrolling.
- OTA now requires HTTPS plus a valid manifest SHA256 and verifies the streamed firmware before rebooting.
- DJ response audio playback is split into a focused WAV/MP3 dispatcher, reducing `SpotifyDJApp` complexity.
- Speaker audio uses guarded I2S ownership so UI cues do not collide with streamed DJ response audio.
- Captive portal no longer contains playback-backend credential fields.
- Pairing mode blocks normal playback/menu input while keeping reset controls and the local API available.
- `SPOTIFY_ALLOW_INSECURE_TLS` defaults to secure TLS in local `Secrets.h`.
- Legacy PKCE helper documentation no longer suggests putting backend credentials in firmware headers.

### Known Issues

- OTA transport still uses the ESP secure client in manifest-hash enforced mode; GitHub CA pinning can be added later if required.
- Backend playback support depends on the configured Home Assistant integration and selected output.
- Some outputs may not support volume control, queue details or playlist start through the backend.

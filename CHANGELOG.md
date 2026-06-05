# Changelog

## v2.5.1

Consolidated SpotifyDJ firmware release for the LilyGO T-Embed-CC1101 / ESP32-S3.

### Added

- Spotify Connect remote control with Now Playing, track progress, volume, pause/resume, next and previous controls.
- Device menus for Up Next, Playlists, Sound Outputs, Settings, About and Logs.
- Spotify play mode settings on the device and web portal: normal, shuffle, repeat once and repeat infinite.
- Language setting for the device UI, web portal and captive portal with English and Dutch support.
- Theme setting for the device, web portal and MQTT: Auto, Dark and Light.
- Playlist browsing and playlist start from both the device and web portal.
- Current Song screen with on-demand album art download/cache and scrolling title/artist text.
- Mobile web portal with Now Playing, browser push-to-talk, album art, volume slider, sound output selection, queue, logs, diagnostics, settings, WiFi update and OTA upload.
- Home Assistant device layer with pairing, mDNS discovery, device-token storage, status updates, OTA endpoint and Spotify provisioning endpoint.
- Device API endpoint `/api/device/dj_response` for DJ text responses from the Home Assistant integration, with optional backend-generated PCM WAV playback on the ESP speaker.
- BLE WiFi provisioning in setup mode through a writable JSON characteristic.
- MQTT/Home Assistant discovery and periodic/on-event status publishing.
- Two-way MQTT Home Assistant controls for volume, next/previous, sound output, playlist start and settings.
- MQTT settings provisioning via Home Assistant pair/provision/status responses, stored in the `spotifydj` NVS namespace.
- Captive portal for WiFi, Spotify and MQTT provisioning.
- OTA release flow through GitHub tags and board-specific firmware assets.
- Proprietary firmware license for closed-source distribution on sold devices.
- WS2812 LED-ring feedback for volume, status, setup/AP mode, charging, connectivity and firmware update state.
- Speaker cues for boot, reset, battery warning, factory reset, charging completed, menu/back and push-to-talk start/stop.
- Built-in speaker cue volume setting: 25%, 50%, 75% or 100%.
- Battery/charging guard screens, low-battery turn-off sleep and charger-aware wake behavior.
- WiFi-failure boot menu with encoder selection for retry connect, factory reset, restart device and turn off.
- Optional local micro wake word hook for a trained `Spotify DJ` detector.
- Watchdog, slow-loop diagnostics and periodic heap diagnostics for long-running device stability.
- `ProvisioningController` for centralized NVS provisioning storage and reduced `SpotifyDJApp` responsibility.
- `PowerController` for charger/wake/watchdog policy.
- Host-testable menu and network helper models plus release-script shell tests.
- `release.sh` helper for local firmware release preparation, dry-run validation, manifest generation, tagging and optional public firmware repo publishing.
- Postman collection for the public/local ESP device API.

### Changed

- Application name and technical branding are now `SpotifyDJ`.
- Release builds use `2.5.1` / `v2.5.1`; local builds without release flags remain `dev` / `vdev`.
- WiFi, Spotify and Home Assistant secrets are no longer hardcoded in firmware.
- Spotify credentials are provisioned through the setup portal or Home Assistant and stored in NVS.
- The web portal can manually repair Spotify OAuth credentials with a one-shot refresh-token submit field, immediately testing authorization and clearing the submitted fields from the page.
- Spotify refresh tokens now live only in the `spotifydj` NVS namespace; the old `spotify/refresh` legacy namespace is no longer read or written.
- Spotify OAuth credentials can be parsed from Home Assistant pairing/status/provision payloads, both top-level and nested under `spotify`, without logging refresh tokens.
- Battery percentage is always voltage-estimated and displayed without a tilde.
- Spotify volume is limited to `0-60`; the LED ring treats `60` as full scale and uses orange segments.
- Now Playing shows `H`, `M` and `S` status indicators for Home Assistant, MQTT and Spotify.
- Pairing mode shows the SpotifyDJ logo/name, battery state, instruction text and a large pairing code.
- Pairing code is also visible in serial logs and the web interface.
- Setup/AP mode and Home Assistant pairing mode keep the screen at 100% brightness for 10 minutes, then turn off.
- OTA firmware write shows `Firmware update in progress..` on the display and sets the LED ring to purple.
- Display idle behavior keeps the configured brightness until the selected timeout, then turns the screen fully off.
- The first button/encoder action while the screen is off only wakes the screen and does not execute the underlying action.
- Web interface shows H/M/S status indicators, WiFi signal bars and the last MQTT publish timestamp.
- Web interface logs can be paused and selected/copied.
- `Restart device` and `Turn off device` are available from settings.
- Push-to-talk uses Route B: microphone audio streams as raw PCM16 to the Home Assistant Assist WebSocket pipeline; only recognized text is sent to `/api/spotify_dj/voice`.
- The PTT flow is documented: Assist STT, text to the HA integration, then DJ text plus optional PCM WAV URL back to the ESP device.
- `assist_pipeline_id` can optionally be stored in NVS; an empty value uses the default Home Assistant Assist pipeline.
- Encoder short press performs pause/resume, double press opens Current Song and long press starts push-to-talk until release.
- Current Song is blocked from Now Playing when there is no active playback.
- Turn-off sleep periodically probes for USB-C charger attach; with a charger detected the device continues booting, otherwise it returns to sleep.
- Push-to-talk logs listening steps to serial and the web logs screen.
- LED-ring animations are yellow on PTT start, blue on PTT stop/processing, and green on accepted voice command response.
- The old WAV upload path to `/api/spotify_dj/voice` was removed from the voice command client.
- Voice status messages use `recording`, `sending_command` and `error`.
- DJ responses are displayed locally, optionally played, published as `last_dj_text` in runtime state and emitted as MQTT events.
- Web portal PTT is now a compact DJ-response simulation button that sends a fixed localized test command through the ESP to Home Assistant; it requires HA pairing but not Spotify credentials, active playback or browser microphone access.
- Stale Home Assistant pairing is reported clearly when the HA voice endpoint returns 404.
- Home Assistant status and push-to-talk calls now mark runtime pairing as stale on HA 401/403/404 responses without automatically deleting stored pairing data.
- Home Assistant voice endpoint stale-pairing messages are translated through the firmware language setting.
- Web portal Spotify refresh-token repair accepts JSON, form-encoded and compatibility field names so a broken token can be replaced without factory reset.
- Spotify OAuth credentials are stored with short ESP32 Preferences keys so NVS writes no longer silently fail because of the 15-character key limit.
- Architecture decisions are documented explicitly for Home Assistant, ESP edge behavior, PTT Route B, runtime pairing validity, NVS key limits, network timeout policy and release separation.
- When no music is playing, the device and web portal offer an action to start the `SpotifyDJ Liked Proxy` playlist.
- Volume control is disabled when there is no active playback.
- WiFi boot label is `Connecting to WiFi...`.
- Captive portal MQTT fields are optional; leaving them empty does not attempt MQTT setup and does not overwrite Home Assistant-provisioned MQTT settings.
- The language setting is stored in NVS, can be provisioned by Home Assistant through `device_language`/`language`, and resets to English on factory reset; logs remain English.
- Theme is stored in NVS and exposed on the device, web portal and MQTT. Device `Light` uses TFT inversion/high contrast; web `Auto` follows browser/device preference.
- MQTT two-way support covers Spotify controls, status/discovery and settings commands for language, theme, brightness, dim timeout, turn-off timeout and speaker cue volume.
- App logs are stored in fixed-size buffers to reduce heap fragmentation during long runs.
- Menu counts/options, network timeout behavior and release dry-run validation have stronger automated test coverage.
- Provisioning, power policy and long-network-call responsibilities are documented as separate refactor boundaries to keep future production fixes easier to isolate.

### Fixed

- Reduced display flicker caused by unnecessary redraws.
- Volume actions no longer show HTTP 411 in the UI.
- Corrected encoder direction for volume.
- Token refresh and rotated refresh tokens are stored in NVS.
- Display remains usable in error, pairing and OTA states.
- Settings such as brightness, screen dim timeout, speaker volume and turn-off timeout persist after reboot.
- Web interface no longer clears input fields during status polling.
- Web interface no longer overwrites MQTT settings while the user is typing.
- Web interface sound-output and playlist comboboxes no longer stay on loading text when Spotify is not connected.
- Sound output types are no longer appended to output names.
- Improved JPEG album art rendering and Current Song text scrolling.
- Pairing mode blocks normal playback/menu input while keeping reset controls and the local API available.
- `SPOTIFY_ALLOW_INSECURE_TLS` defaults to secure TLS in local `Secrets.h`.
- The Spotify PKCE helper no longer suggests putting credentials in firmware headers.

### Known Issues

- OTA download supports HTTPS, but streaming SHA256 verification is still a TODO.
- Spotify Web API playback control requires Spotify Premium and an available/active Spotify Connect output.
- Some Spotify Connect outputs do not support volume control through the Web API.

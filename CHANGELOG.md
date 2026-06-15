# Changelog

## Unreleased

## v3.1.21

Stability and web portal polish release for LilyGO playback polling, web DJ-announcement tests and queue/playlist layout.

### Fixed

- Prevented vertical arrow keys from scrolling the browser while Maze Chase is active in the web portal.
- Removed the current sound-output label from the web portal playback panel, leaving the output combobox as the single control.
- Made the web portal queue and playlist panels internally scrollable with shorter fixed list heights.
- Blocked wake-word reactivation while the DJ announcement overlay is still visible and cleared stale encoder input around DJ-response audio playback.
- Made DJ-response MP3 playback use a preallocated decoder arena and log heap details if decoder startup still fails.
- Delayed automatic playback polling after boot/pairing and guarded the periodic playback status refresh so slow HA playback responses do not trip the loop-task watchdog.
- Moved web DJ-announcement tests out of the synchronous HTTP request path, suppressing background playback/status polling while the test announcement is active, and guarded large HA queue/playlist response processing against loop-task watchdog resets.
- Removed the fixed iPhone sound-output shortcut from device and web output selectors; `None`/`Geen` remains available.
- Added explicit deep-sleep reason, uptime, idle and timeout values to the shutdown log so normal sleep is distinguishable from crashes in serial monitor output.

## v3.1.20

Sync-prompt maintenance release for the shared DJConnect cross-repo contracts.

### Added

- Added committed device screenshot baselines under `docs/screenshots/device/`; web screenshot baselines are documented for future releases when the live portal is reachable.

### Changed

- Updated `SYNC_PROMPTS.md` with the Home Assistant playlist command response contract, localized UI review requirement, Spotify preflight pairing guard and guarded STT fuzzy-correction notes.
- Restored release-cycle guidance to revalidate embedded HTTPS CA/certificate bundles for firmware/device repos before publishing.

## v3.1.19

Feature, UI and release-hygiene update for the ESP firmware and web portal.

### Added

- Added Maze Chase as a fourth local game on device and web, with device/browser-local high score storage.
- Added `DESIGN_DECISIONS.md` with reverse-engineered firmware design decisions, code-level patterns, coding conventions and a full framework/library dependency inventory.
- Added canonical cross-repo `PRODUCT_ROADMAP.md` with new feature ideas, killer features, production-release must-haves and premium feature candidates.
- Added release-test coverage to require the technical design decisions document and scan it in release hygiene checks.

### Changed

- The Home Assistant pairing screen now shows the default LAN Home Assistant URL hint `http://homeassistant.local:8123` above the pairing code.
- ESP queue rendering is capped at 20 items for device/web responsiveness.
- ESP playlist fetching/rendering is capped at 20 items for device/web responsiveness while other clients may keep larger backend limits.
- Web playlists now have an explicit refresh button, matching the queue panel refresh flow.
- Web playlists now render as artwork rows with per-playlist play buttons; the old playlist combobox and `Start playlist` button were removed.
- Web game tabs now fit all five game choices on one row on wider screens.
- Web games now focus the game canvas after selecting or pressing game controls so keyboard input works consistently, including Maze Chase.
- Web playback diagnostics now use polished Music/Muziek labels and localized refresh button text.
- Web previous/next playback controls now use single-track skip icons (`|<` and `>|`) instead of double-skip icons.
- Web shuffle/repeat playback controls now show an explicit grey off state and purple active states.
- Web now-playing status pill now uses a green background for the active playing state.
- OTA downloads now trust the Let’s Encrypt R12 chain currently used by GitHub release asset redirects.
- Release hygiene now explicitly requires revalidating the embedded OTA TLS CA/certificate bundle before publishing firmware.
- Renamed the visible game labels to match the iOS app: Paddle Rally, Meteor Run, Sky Dash and Maze Chase.
- Slowed the Maze Chase ghost and made the white pellet turn the ghost temporarily edible with a blinking vulnerable state.
- Screen wake now shows the DJConnect splash briefly before restoring the active UI.
- Local `dev` / `vdev` firmware now allows unauthenticated `/api/device/screenshot.bmp` captures for development screenshots; release firmware keeps the endpoint bearer-token protected.
- Added `scripts/capture_device_screens.sh` and a debug screen-open endpoint so local/SSH development flows can capture all known device screens after deploy.
- Up Next now supports up to 20 queue items from Home Assistant before truncating on the ESP.
- Renamed the visible queue screen title from `Up Next` / `Volgende nummers` to `Queue` / `Wachtrij`.
- Boot and About screens now show the canonical website URL `https://djconnect.dev`.
- Web portal title/status bar now includes a compact link to `https://djconnect.dev`.
- mDNS discovery now advertises `client_type=esp32` in TXT records so Home Assistant can detect ESP clients without legacy prefix parsing.
- Release and GitHub Actions builds now update/upgrade PlatformIO Core, build tools, frameworks and project libraries before compiling firmware, with a dependency diff that triggers third-party notices and dependency inventory review.
- Documented that future releases must keep `DESIGN_DECISIONS.md` and `PRODUCT_ROADMAP.md` current together with README, changelog, handoff, third-party notices, sync prompts, Postman collections and tests.

### Fixed

- Fixed a loop-task stack canary crash when the web portal fetched larger queue snapshots.

### Maintenance

- Hygiene release for documentation, tests, handoff, backlog, issues and cross-repo sync prompts.

### Changed

- Refreshed README, handoff, backlog/issues, changelog and sync prompt references for the current `3.1.x` dual-board firmware line.
- Revalidated the release hygiene tests, native host tests and dual-firmware publication path.
- Kept the public firmware release contract aligned around the LilyGO T-Embed S3 and ESP32-S3-BOX-3 assets plus `firmware_manifest.json`.
- Documented that release cleanup should leave only the latest stable release, tag and workflow run after publication.

### Verified

- Native logic tests pass.
- Release tooling tests pass.

## v3.1.9

Stable release for DJConnect proposition copy, boot/web tagline and DJ announcement naming.

### Fixed

- OTA firmware downloads now follow GitHub release redirects explicitly and log the download host/final URL on failures, making CDN/transport failures visible instead of a bare `HTTP -1`.

### Changed

- Refreshed handoff, sync prompts, backlog, Postman OTA examples and release hygiene tests for the v1.3.1-beta dual-firmware release contract.

## v1.3.1-beta

Patch release for board abstraction and dual firmware publishing.

### Added

- Added a board profile abstraction for pinout, device model and hardware capability differences.
- Added an experimental `esp32_s3_box3` PlatformIO environment for ESP32-S3-BOX-3 display bring-up.
- GitHub Actions and `release.sh` now build and publish both the LilyGO and ESP32-S3-BOX-3 firmware binaries.

### Changed

- OTA target validation now uses the active board profile device model instead of a hardcoded LilyGO model.
- Release manifests now use a board-specific `firmwares` array only, with dedicated LilyGO and ESP32-S3-BOX-3 firmware asset names.
- The ESP32-S3-BOX-3 profile disables unverified battery gauge, LED ring, speaker and microphone paths until hardware mapping is validated.

## v3.0.22

Patch release for HA local-route pairing, wake-word/PTT stability, queue rendering, playback control feedback and About-screen copy.

### Added

- Device top-button next/previous track actions now play simple directional local audio feedback when feedback is enabled.
- Okay Nabu wake-word detection now runs locally through TensorFlow Lite Micro and the TensorFlow micro_speech frontend when the device is in normal paired playback mode.
- Middle encoder press now cancels the active PTT/DJ-response flow while the device is processing or showing a DJ response.

### Changed

- Playback command payloads are now identity-only and no longer include partial device-status snapshots; `/api/djconnect/status` remains the authoritative source for HA sensor values.
- App logs now support atomic complete-line writes for async playback/volume worker messages so volume changes no longer interleave into broken serial/web log fragments.
- Home Assistant startup state is now logged once when the local device API starts instead of repeating paired/URL lines during boot setup.
- Wake-word listening now remains enabled when playback is idle or paused after Home Assistant is paired.
- Wake-word recordings now stop on silence after the minimum listening window and remain capped by the maximum recording duration.
- Postman pairing callback examples now include the canonical LilyGO `device_id`, pairing code and `client_type:"esp32"`.
- Volume up/down logs now use the same `Playback: ... requested/accepted/failed` style as play/pause and next/previous.
- HA playback command response logs are emitted as single atomic lines to avoid interleaved serial/web log fragments during async volume worker activity.
- The device About screen now labels the playback/backend status as `Music` / `Muziek` instead of `Spotify`.
- Up Next queue parsing now removes duplicate items by track URI, or by title/artist fallback when no URI is available, so single-item queues no longer render the same track repeatedly.

### Fixed

- Prevented Nabu Casa cloud URLs from being stored as `ha_local_url`; cloud URLs are normalized to `ha_remote_url` and the incorrect local NVS key is cleared on re-pairing.
- Playback proxy commands are now local-URL only, avoiding broken cloud fallback attempts when local playback control is expected.
- Wake-word runtime allocation no longer starts during the pairing/BLE transition; direct pairing now exits pairing mode first so BLE can shut down before wake-word inference starts.
- HA status and playback transport-error paths no longer read HTTP response bodies after a negative transport code, reducing watchdog risk after `HTTP -1`.
- Startup logs now show the stored Home Assistant local and remote URLs for pairing diagnostics.
- Stopping wake-word recordings now pauses the watchdog around the blocking recorder join path, reducing watchdog resets after maximum-duration auto-stop.

## v3.0.12

Patch release for DJ-response, Home Assistant status stability, playback logging and device/web polish.

### Added

- Device LED-ring now shows a purple chase while the DJ-response screen is visible.
- Device logs now include play/pause, next-track and previous-track requests, accepted responses and failures.

### Changed

- Dutch Up Next label changed from `Volgende nummer` to `Volgende nummers` on device and web.
- Playback command payloads now include device identity/status fields defensively so partial command payload handling cannot easily clear Home Assistant sensor values.
- PlatformIO upload+monitor helper now waits on the monitor target and probes the USB CDC port before opening the monitor.

### Fixed

- Fixed Home Assistant sensor resets caused by voice-only status posts to `/api/djconnect/status`; voice status no longer posts partial device-status payloads.
- Fixed task-watchdog `task not found` noise during volume/network/audio/OTA paths by guarding watchdog pause/reset calls.
- Fixed web DJ-response test reporting failure despite successful ESP/HA flow by making the browser response handling more tolerant and explicit.
- Fixed DJ-response web/device flow crash risk after skipped web-test audio by guarding long HA HTTP calls.
- Fixed PTT recording sometimes ending with `No audio recorded` by flushing microphone chunks before stopping the recorder and giving mic reads a short timeout.
- Fixed interleaved volume log lines from the UI task and volume worker.
- Removed misleading `Voice: encoder released without active PTT` logs during normal play/pause clicks.

## v3.0.11

Hygiene release for the ESP-IDF 5.3 / Arduino ESP32 3.x migration, web portal polish and DJ-response stability.

### Added

- Web album-art popover: clicking/tapping the Now Playing album art opens a large focused view with backdrop, close button and Escape support.
- Web DJ-response test now shows an explicit route description: browser -> ESP `/api/voice-text` -> Home Assistant `/api/djconnect/voice` -> DJ-response text on the device.
- PlatformIO upload+monitor stabilization for ESP32-S3 USB CDC after IDF 5 reset: monitor RTS/DTR are held low and combined upload/monitor runs wait briefly before opening the monitor.

### Changed

- PlatformIO now targets the pinned pioarduino ESP-IDF 5.3 / Arduino ESP32 3.x toolchain, with old Arduino 2.x / ESP-IDF 4.x compatibility branches removed.
- TFT backlight, OTA SHA256 and watchdog code now use the Arduino 3.x / IDF 5 / mbedTLS 3 APIs directly.
- Web Now Playing volume slider is placed directly under the compact playback button row.
- Web DJ-response test is text-only: it displays returned DJ text and intentionally skips returned TTS audio so it cannot block the physical encoder PTT/audio path.
- DJ-response MP3 playback now uses the existing mono speaker I2S driver through a local output adapter instead of letting ESP8266Audio install its own stereo I2S driver.

### Fixed

- Fixed TFT boot crash after the IDF 5 upgrade by forcing TFT_eSPI to use the HSPI port on the ESP32-S3 build.
- Fixed BLE provisioning payload handling across Arduino BLE API return-type changes.
- Fixed watchdog log noise during audio playback when the loop task is temporarily removed from the task watchdog.
- Fixed initial Home Assistant pairing/playback races by delaying playback proxy use until authenticated status validation succeeds and by allowing playback `status` commands through transient cooldown.
- Fixed Home Assistant route retry after playback transport errors by invalidating and recomputing the active local/cloud route before retry.
- Fixed web DJ-response test watchdog hangs by removing blocking voice-status posts from the web simulation path.
- Fixed web DJ-response test still attempting to play audio when Home Assistant later posts `/api/device/dj_response`; the next callback after a web test is consumed as text-only.

## v3.0.10

Focused UI, playback, web portal and release hygiene update.

### Added

- Web portal Games panel with local browser-side Paddle Rally, Meteor Run and Sky Dash, including browser-local highscores.
- Device game highscores for Paddle Rally, Meteor Run and Sky Dash stored in the `provision` NVS namespace and cleared by factory reset.
- Web Up Next per-item play buttons and a compact refresh button.
- Web Up Next album art thumbnails when Home Assistant supplies queue image URLs. The ESP only passes URLs through; the browser lazy-loads thumbnails when the queue panel is visible.
- Queue context propagation for Up Next item playback so `play_context_at` can use the queue/context URI returned by Home Assistant.
- DJ-response test timeout handling in the web portal so the button always resets after a slow/failing request.

### Changed

- Playback status indicator is now a music-note icon on device and web instead of `S`, while keeping the same green/grey/red status colors.
- `Current Song` label is now `Current song`; Dutch remains `Huidig nummer`.
- Up Next on device refreshes automatically after selecting a queue item.
- Web game controls are game-specific: Meteor Run uses left/right arrows and Paddle Rally hides the fire button.
- Paddle Rally colors were swapped to orange title/paddle/LED-ring and green ball.
- Meteor Run visuals were refined with a smaller ship and asteroid; the device LED-ring uses blue for Meteor Run and light blue for Sky Dash.
- Menu and no-playback LED-ring handling now hard-clears pixels so stale volume LEDs do not remain visible.
- Web portal playback/queue/game controls use compact SVG/icon buttons and avoid page scrolling while game keyboard controls are active.
- Voice/DJ-response error messages from Home Assistant are surfaced on the device DJ response screen.

### Fixed

- Fixed Up Next queue context loss that could duplicate the current track or fail with `Queue context unavailable`.
- Fixed Home Assistant next/previous, shuffle and repeat state visibility issues by routing native entity actions through the ESP/device command path and refreshing menu state.
- Fixed settings comboboxes in the web portal being overwritten while the user is editing them.
- Fixed pairing-screen rendering flicker when web/HA settings updates arrive.
- Fixed no-playback and menu LED-ring stale-state cases.
- Fixed OTA/network long-flow responsiveness and crash-prone paths by tightening long-operation handling.

## v3.0.9

Consolidated DJConnect firmware release for the LilyGO T-Embed-CC1101 / ESP32-S3.

### Added

- Home Assistant backed playback remote control with Now Playing, track progress, volume, pause/resume, next and previous controls.
- Device menus for Up Next, Playlists, Sound Outputs, Settings, About and Logs.
- Local Games menu with Paddle Rally, Meteor Run and Sky Dash mini-games.
- Help screen with button/encoder controls, shown once after initial Home Assistant pairing and available from the main menu.
- Separate shuffle and repeat controls on the device main menu, Now Playing web portal and Home Assistant command API.
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
- Home Assistant status payloads mirror screen brightness, screen timeout, turn-off timeout, speaker cue volume, language, theme, log level, screen state and LED state so native HA entities can update from device state.
- Captive portal for WiFi provisioning.
- OTA release flow through GitHub tags and board-specific firmware assets.
- Proprietary firmware license for closed-source distribution on sold devices.
- WS2812 LED-ring feedback for volume, status, setup/AP mode, charging, connectivity and firmware update state.
- Speaker cues for boot, reset, battery warning, factory reset, charging completed, menu/back and push-to-talk start/stop.
- Paddle Rally score display, restart-on-encoder-long-press, subtle wall-bounce cues, miss feedback sound/red border and dedicated bright-orange paddle LED-ring feedback that pauses when the screen turns off.
- Direct Home Assistant pairing now treats the first token as pending; if HA rejects it with 401/403/404 during first validation, the device returns to pairing mode instead of staying half-paired.
- Home Assistant playback backend unavailability can be reported as HTTP 200 with `backend_available:false`; the ESP shows playback status red without treating it as a pairing/auth failure.
- After WiFi/Home Assistant setup at boot, the first playback status poll is forced immediately so the playback music-note indicator reflects the backend state without waiting for a button or volume action.
- Built-in speaker cue volume setting: 25%, 50%, 75% or 100%.
- Battery/charging guard screens, low-battery turn-off sleep and charger-aware wake behavior.
- WiFi-failure boot menu with encoder selection for retry connect, restart device, turn off and confirmed factory reset.
- Optional local micro wake word hook for a trained `DJConnect` detector.
- Watchdog, slow-loop diagnostics and periodic heap diagnostics for long-running device stability.
- Timestamped log severity classification with `[inf]`, `[wrn]`, `[err]` and `[dbg]` markers for serial and web logs.
- Compact serial/web log prefix format: `HH:mm INF ...`, `HH:mm WRN ...`, `HH:mm ERR ...` or `HH:mm DBG ...`.
- Device Logs screen is scrollable with the encoder, keeping the newest tail as the default live position.
- Settings now includes a confirmed Change WiFi action that opens the setup/captive portal without clearing the Home Assistant pairing.
- Log level setting on the device, web portal and Home Assistant command API with translated UI labels and `info` as the default.
- `ProvisioningController` for centralized NVS provisioning storage and reduced `DJConnectApp` responsibility.
- `PowerController` for charger/wake/watchdog policy.
- Host-testable menu and network helper models plus release-script shell tests.
- `release.sh` helper for local firmware release preparation, dry-run validation, manifest generation, tagging and optional public firmware repo publishing.
- Postman collection for the public/local ESP device API.
- GitHub release cleanup helper to dry-run and delete older releases, tags and workflow runs while keeping the newest semver release/run.

### Changed

- Application name and technical branding are now `DJConnect`.
- Release builds use `3.0.9` / `v3.0.9`; local builds without release flags remain `dev` / `vdev`.
- Boot logs now include the DJConnect app name and active firmware version.
- Local `dev` / `vdev` firmware reports OTA-comparable version `0.0.0` to Home Assistant/device API so any published `X.Y.Z` firmware is treated as an upgrade.
- Local `dev` / `vdev` firmware is excluded from automatic pre-pairing bootstrap updates so development flashes stay local until explicitly updated.
- WiFi, playback-backend and Home Assistant secrets are no longer hardcoded in firmware.
- Playback control has moved to a backend-agnostic Home Assistant proxy. The ESP sends generic playback commands to HA and no longer stores Spotify OAuth credentials.
- Captive portal and web portal no longer include Spotify client-id or refresh-token fields.
- Playback-backend credentials are managed only in Home Assistant.
- Battery percentage is always voltage-estimated and displayed without a tilde.
- Playback volume is limited to `0-60`; the LED ring treats `60` as full scale and uses orange segments.
- Now Playing shows `H` and `S` status indicators for Home Assistant and playback.
- Pairing mode shows the DJConnect logo/name, battery state, instruction text and a large pairing code.
- Pairing code is also visible in serial logs and the web interface.
- Setup/AP mode and Home Assistant pairing mode keep the screen at 100% brightness for 10 minutes, then turn off. Pairing mode also keeps BLE advertising active, shows a center-button turn-off hint and uses a deeply fading blue LED-ring breath.
- `/api/device/pair` now accepts the direct Home Assistant callback with `device_token`, `ha_local_url` and/or `ha_remote_url`, Assist pipeline id and device language, while keeping the handler lightweight to avoid watchdog stalls during pairing.
- Playback proxy commands now wait for a successful authenticated Home Assistant status confirmation after boot/pairing, preventing stale or pending tokens from repeatedly sending playback 401 requests.
- Direct Home Assistant pairing now schedules immediate status and playback polls on the next loop so the `H`/playback music-note indicators update right after pairing or reboot instead of waiting for a normal polling interval.
- Repeated direct Home Assistant pair callbacks with the same local/remote URLs and `device_token` are now idempotent, preventing normal status/playback synchronization from resetting pairing validation and playback polling.
- Home Assistant routing is now local-first with cloud fallback: the ESP probes `ha_local_url` briefly and uses `ha_remote_url` when the local URL is not reachable.
- Home Assistant playback proxy HTTP failures such as `HA playback HTTP -1` now make the device connectivity LED state red without erasing pairing data.
- Home Assistant playback proxy calls use shorter request waits and a transient-failure cooldown so repeated HA 5xx/-1 responses do not stack blocking commands or trip the watchdog.
- The `S` playback status indicator is now tri-state: green for active usable playback, grey for a reachable backend with no active playback, and red for playback proxy errors such as HA 5xx/-1 responses.
- Reset Home Assistant pairing from the device now shows a clear reset screen, plays the soft-reset cue and flashes the LED ring before restarting into pairing mode.
- Direct Home Assistant pairing callbacks are now treated as pending until `/api/djconnect/status` accepts the token. If HA returns 401/403/404, the ESP clears that pending token and stays on the pairing-code screen.
- OTA firmware write shows `Firmware update in progress..` on the display for both Home Assistant OTA and manual web upload, runs a fast purple LED-ring animation, and plays start/progress/complete/failure speaker cues.
- OTA download and manual firmware upload now explicitly service the ESP task watchdog while hashing and writing firmware chunks.
- OTA streaming is more tolerant of slow GitHub/CDN bursts, with a longer idle window and larger write chunks before treating the stream as stalled.
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
- Firmware manifests now target board-specific device models and use board-specific distributable binary asset names, matching the ESP OTA endpoint validation.
- GitHub Actions now injects the release version into the PlatformIO build, verifies the compiled firmware contains `vX.Y.Z`, and publishes board-specific OTA assets.
- The embedded web portal no longer contains a static `vdev` app-version placeholder in release binaries, and the release workflow rejects firmware assets that still contain that dev marker.
- `release.sh` now lets GitHub Actions publish the public firmware release by default; local GitHub release creation is an explicit `--gh-release` fallback.
- Web portal firmware upload no longer shows a temporary `Uploading firmware...` / `Firmware uploaden...` status label before the final upload response.
- Web interface logs can be paused and selected/copied.
- `Restart device` and `Turn off device` are available from settings.
- Push-to-talk now records a WAV file on the ESP and uploads it as raw `audio/wav` to the Home Assistant integration endpoint `/api/djconnect/voice`.
- The PTT flow is documented: ESP WAV upload to the HA integration, backend Assist/STT/TTS in Home Assistant, then DJ text plus optional WAV/MP3 URL back to the ESP device.
- Direct Home Assistant Assist WebSocket authentication has been removed from the physical PTT path; the websocket, if required, belongs on the Home Assistant integration backend.
- Encoder short press performs pause/resume and long press starts push-to-talk until release from Now Playing.
- Current Song moved into the first root menu item, keeps the same top-button back behavior as other menu screens, and no longer starts push-to-talk from encoder long press.
- Turn-off sleep periodically probes for USB-C charger attach; with a charger detected the device continues booting, otherwise it returns to sleep.
- Push-to-talk logs listening steps to serial and the web logs screen.
- LED-ring animations are yellow on PTT start, blue on PTT stop/processing, and green on accepted voice command response.
- The old recognized-text-only physical PTT path was replaced by raw WAV upload to `/api/djconnect/voice`; the web portal still keeps a compact text-based DJ-response simulation path.
- Voice status messages use `recording`, `sending_command` and `error`.
- DJ responses are displayed locally, optionally played and published as `last_dj_text` in runtime state.
- DJ response audio now supports MP3 streams in addition to PCM WAV, with content-type and magic-byte detection plus text-only fallback for unsupported audio.
- Web portal PTT is now a compact DJ-response simulation button that sends a fixed localized test command through the ESP to Home Assistant; it requires HA pairing but not playback-backend credentials on the ESP, active playback or browser microphone access.
- Stale Home Assistant pairing is reported clearly when the HA voice endpoint returns 404.
- Home Assistant status and push-to-talk calls now mark runtime pairing as stale on HA 401/403/404 responses without automatically deleting stored pairing data.
- Home Assistant voice endpoint stale-pairing messages are translated through the firmware language setting.
- The web portal no longer exposes backend credential repair because backend credentials live in Home Assistant.
- Architecture decisions are documented explicitly for Home Assistant, ESP edge behavior, integration-backed PTT, runtime pairing validity, NVS key limits, network timeout policy and release separation.
- When no music is playing, the device and web portal offer an action to start the `DJConnect Liked Proxy` playlist.
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
- Local OAuth token storage paths are removed; playback credentials stay in Home Assistant.
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
- DJ response audio playback is split into a focused WAV/MP3 dispatcher, reducing `DJConnectApp` complexity.
- Speaker audio uses guarded I2S ownership so UI cues do not collide with streamed DJ response audio.
- Captive portal no longer contains playback-backend credential fields.
- Pairing mode blocks normal playback/menu input while keeping reset controls and the local API available.
- `SPOTIFY_ALLOW_INSECURE_TLS` defaults to secure TLS in local `Secrets.h`.
- PKCE helper documentation no longer suggests putting backend credentials in firmware headers.

### Known Issues

- OTA transport still uses the ESP secure client in manifest-hash enforced mode; GitHub CA pinning can be added later if required.
- Backend playback support depends on the configured Home Assistant integration and selected output.
- Some outputs may not support volume control, queue details or playlist start through the backend.

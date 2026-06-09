# DJConnect Firmware Handoff

## Current State

DJConnect is proprietary ESP32-S3 firmware for the LilyGO T-Embed-CC1101. It is a Home Assistant paired playback remote with local display UI, rotary/top-button controls, LED-ring feedback, speaker cues, push-to-talk voice upload, web portal and Home Assistant pairing/OTA support.

Current repo state includes:

- Latest firmware release prepared from this repo: `v3.0.10`. Source repo `pcvantol/djconnect-esp32` and public firmware repo `pcvantol/djconnect-firmware` should keep the newest semver release/tag after cleanup; generated `release/` artifacts are ignored in source.
- Firmware version flow based on git tag/build flags; local builds remain `dev` / `vdev`.
- Home Assistant device layer with pairing, mDNS discovery, device-token auth, OTA, DJ response and status updates.
- Playback commands are proxied from the ESP to Home Assistant as generic commands. Spotify OAuth, Sonos credentials or other backend credentials live in Home Assistant, not on the ESP.
- Physical PTT records WAV on the ESP and uploads to the Home Assistant integration; Home Assistant owns Assist/STT/TTS backend work and returns DJ text plus optional WAV/MP3 audio URL.
- Web portal includes Now Playing, DJ-response simulation, outputs, playlists, Up Next with per-item play, refresh and lazy browser-loaded album-art thumbnails, local browser games, logs, diagnostics, OTA upload, WiFi update, settings and status indicators. Device logs are scrollable with the encoder and use compact `HH:mm INF` prefixes.
- Playback proxy control requests use short waits and transient-failure cooldown; OTA writes tolerate slow GitHub/CDN stream bursts while continuing to feed the watchdog and firmware-update LED animation.
- Device main menu includes a local Games submenu with Pong, Asteroids and Fly. Games are local-only, use encoder movement/center-button fire where applicable, persist highscores in NVS and are not exposed through Home Assistant.
- Device Help screen lists top button, encoder button and rotary actions. It appears once after initial Home Assistant pairing, then remains available from the main menu.
- Home Assistant native device commands support two-way playback/settings control through `/api/device/command`.
- Home Assistant should expose the proxied playback as a native `media_player` entity if user-facing HA media controls are desired. That entity represents the backend playback session, not the ESP speaker.
- WiFi boot connection timeout is 30 seconds. During WiFi connect, the LED ring shows a green chase animation.
- If WiFi cannot connect, the device shows a 100%-brightness recovery menu: retry connect, restart device, turn off, and confirmed factory reset.
- Setup/AP mode keeps brightness at 100%, shows a deeply fading rainbow breath, shows portal active for 10 minutes, allows center-button turn off, and then deep-sleeps if setup is not completed.
- The device Settings menu has a confirmed Change WiFi action that reboots into setup/AP mode while preserving Home Assistant pairing; only Factory reset or Reset Home Assistant pairing clears HA state.
- Home Assistant pairing mode keeps brightness at 100%, keeps BLE advertising active, shows the pair code plus center-button turn-off hint, breathes blue on the LED ring, and then deep-sleeps after 10 minutes if pairing is not completed.
- HA should treat pairing as pending until the ESP confirms token storage. The ESP `/api/device/pair` route accepts a direct HA callback with `device_token` plus `ha_local_url` and/or `ha_remote_url`, stores it with minimal in-route work, and lets the next main-loop pass confirm the pairing through `/api/djconnect/status` plus an immediate playback status poll.
- The ESP resolves Home Assistant local-first/cloud-fallback: it briefly probes `ha_local_url` and falls back to `ha_remote_url` when local HA is unreachable. This supports Nabu Casa remote access for ESP→HA calls without exposing ESP local endpoints through the cloud.
- After WiFi/Home Assistant setup, the ESP forces an immediate `/api/djconnect/command` status poll so the device playback music-note indicator does not remain grey until the first physical control action.
- `/api/djconnect/command` should distinguish auth from backend availability. 401/403/404 means stale pairing. Playback/backend unavailability should be HTTP 200 with `success:false` and `backend_available:false`, not HTTP 503 during normal pairing/status flow.
- Periodic `/api/djconnect/status` now mirrors device settings/entities: `ha_pairing_status`, `local_url`, screen brightness aliases, screen timeout aliases, turn-off timeout, speaker/cue volume aliases, language, theme, log level, OTA/update state, screen state and LED state.
- Backend credentials are never accepted by ESP firmware.
- Top-button soft reset plays a dedicated cue and bright white LED-ring flashes before reboot. Turn-off/deep-sleep always plays a rainbow LED fade-out.
- Freshly provisioned unpaired release firmware performs a graceful pre-pairing bootstrap update check after WiFi connects. It skips local `dev`/`vdev` builds and continues to pairing silently if the check fails.

Latest validated commands:

```sh
clang++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_logic_test && /tmp/djconnect_logic_test
bash test/native/test_release.sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

All passed after the v3.0.10 UI/playback/web portal consolidation. The v3.0.10 release should publish `djconnect-device-v3.0.10.bin` plus manifest assets to `pcvantol/djconnect-firmware`.

## Architecture

Main module boundaries:

- `DJConnectApp`: top-level orchestration, setup flow, loop routing, input/screen transitions.
- `DisplayManager`: display drawing only.
- `SpotifyClient`: Home Assistant playback proxy for backend-agnostic playback state and control.
- `WebPortal`: embedded mobile web UI and local web actions.
- `ProvisioningController`: NVS provisioning/settings storage for WiFi, language/theme/log level, display/power settings and cue volume.
- `PowerController`: charger/wake/deep-sleep/watchdog policy.
- `DJConnectApiServer`: authenticated local ESP device API for Home Assistant commands, OTA, provisioning and DJ responses.
- `DJConnectDevice`, `DJConnectPairing`, `DJConnectDiscovery`, `DJConnectApiServer`, `DJConnectOTA`: Home Assistant device layer.
- `VoiceRecorder` and `VoiceHttpClient`: physical PTT WAV capture/upload.
- `DjResponseAudioPlayer` and `SoundManager`: DJ response audio dispatch/playback and generated speaker cues.
- `LogicHelpers`, `DJConnectMenuModel`, `NetworkActivityLogic`: host-testable pure logic.

Core data/security boundaries:

- Device API uses `Authorization: Bearer <device_token>` for protected endpoints.
- Home Assistant pairing validity is runtime state. HA 401/403/404 marks pairing stale/red but does not erase stored pairing automatically. Playback proxy commands are disabled until a successful authenticated HA status post confirms the stored token.
- Playback proxy backend availability is separate from pairing validity. A command response with `success:false` and `backend_available:false` marks the playback music-note indicator red but must not clear pairing or rotate tokens.
- Playback-backend refresh tokens are never stored on the ESP and therefore are never logged or shown back to users.
- The ESP is not a Spotify Connect speaker/player and should not be modeled as the actual music sink. It mirrors and controls the playback state supplied by Home Assistant.
- Internal ESP32 Preferences keys must be 15 chars or less.

## Decisions Made

- Home Assistant is the trusted backend for pairing, playback command interpretation, backend credentials, Assist/STT/TTS orchestration, OTA offer handling and native command entities.
- The ESP stays focused on local edge behavior: display, controls, LED ring, battery/power policy, mic capture and playback of HA-provided DJ response audio.
- A Home Assistant `media_player` entity may be implemented in the integration for the playback proxy. HA `media_player.pause`, `media_player.play_media`, `media_player.volume_set`, source selection and next/previous should be translated by the integration into backend playback actions and/or ESP `/api/device/command` updates. Do not make this entity imply that Spotify/Sonos music audio is played by the ESP.
- The ESP speaker remains reserved for local cues and DJ/voice response audio. Treat that as device-local feedback, separate from the backend music `media_player`.
- Physical PTT must stay on the WAV-upload route to `/api/djconnect/voice`; do not reintroduce direct ESP Home Assistant Assist WebSocket authentication.
- Web PTT is a compact DJ-response simulation path. It requires HA pairing but not playback-backend credentials on the ESP, active playback or browser microphone permission.
- Smart Shuffle was removed because Spotify does not expose a useful public Web API control for it.
- Current song is a menu screen and uses top-button back; it does not start PTT.
- Up Next item playback requires a valid context URI. The ESP preserves queue `context_uri` from Home Assistant and uses it as a fallback when the latest playback snapshot has no context.
- Web Up Next album art is URL pass-through only. The ESP does not download queue thumbnails; the browser lazy-loads them when the queue panel is visible.
- Release binaries/manifests are generated locally under `release/` but ignored by git; published OTA assets live only in `pcvantol/djconnect-firmware`, not in the closed-source source repo.
- Local `dev` / `vdev` builds report OTA-comparable version `0.0.0` to Home Assistant/device API so published releases are seen as upgrades.
- Bootstrap OTA is separate from normal HA OTA: it only runs before pairing, checks the public firmware release API, skips dev firmware, and reuses the normal OTA write/display/LED/sound path when a newer release is found.
- True NVS Encryption is not active in the current Arduino/PlatformIO build. It requires an ESP-IDF or Arduino-as-component build with `CONFIG_NVS_ENCRYPTION=y` plus an `nvs_keys` partition and factory/serial flashing. OTA alone cannot safely enable it.

## Known Issues

- NVS credentials are currently stored in ESP32 NVS but not encrypted by this Arduino/PlatformIO build.
- OTA status clearing in Home Assistant depends on the integration processing the post-boot status payload correctly.
- HA native entities for brightness, speaker volume, timeouts, language, theme and log level should read the mirrored `/api/djconnect/status` settings fields; otherwise they may show minimum/default values until changed from HA.
- If HA reposts direct pairing/settings callbacks too often during normal playback commands, debounce or route those updates so they do not spam `/api/device/pair`. The ESP now treats identical direct-pair callbacks as idempotent, but the integration should still reserve `/api/device/pair` for initial pairing, explicit re-pair/token rotation or recovery.
- Keep the Home Assistant integration and firmware contract synchronized around native device entities, optional playback `media_player`, OTA status clearing, stale pairing behavior and command/status payloads.
- Queue, playlist and output metadata now come from Home Assistant. Backend-specific fallbacks belong in the integration.
- MP3 DJ-response playback can still be sensitive to decoder/runtime blocking; watchdog handling has been improved but should be stress-tested with varied MP3 lengths/bitrates.
- Home Assistant STT/TTS failures are backend/integration dependent. Firmware surfaces error bodies but cannot fix missing HA STT provider configuration.
- Web portal performance depends heavily on local WiFi quality. Polling has been reduced/visibility-aware, but poor WiFi can still make the UI feel slow.
- Flash Encryption and NVS Encryption are not production-enabled yet.

## Next Tasks

Recommended next work:

1. Decide production security path for encrypted credential storage:
   - migrate to ESP-IDF/Arduino-as-component or another build path that enables `CONFIG_NVS_ENCRYPTION`;
   - add an `nvs_keys` partition;
   - define factory flash and re-provision/migration process.
2. Add automated tests for WiFi-failure menu ordering and factory-reset confirmation behavior.
3. Add a host-testable model for setup/AP screen state and timeout labels.
4. Add integration-side verification for OTA status clearing after successful firmware boot.
5. Implement/verify native Home Assistant entities for every command previously handled locally, using `/api/device/command`, and refresh their state from the ESP `/api/djconnect/status` settings payload.
6. Add a Home Assistant `media_player` entity for proxied playback state/control:
   - state: playing, paused, idle/unavailable from integration playback state;
   - attributes: title, artist, album art, output/source, volume and supported features;
   - commands: play/pause, next/previous, volume, source/output selection and playlist/media start;
   - keep actual backend credentials and playback API calls in Home Assistant.
7. Stress-test DJ-response MP3 playback with several short and long files.
8. Continue reducing `DJConnectApp` size by moving setup/captive/BLE flow into a dedicated `ProvisioningController` runtime flow or `SetupController`.
9. Add release checklist item for confirming public GitHub Action firmware contains the expected `vX.Y.Z` marker.
10. Add product security review for local HTTP device API, bearer-token lifetime, replay behavior and factory reset behavior.

## Home Assistant Sync Prompt

A copy/paste prompt for syncing the Home Assistant integration with this firmware is maintained in `HA_SYNC_PROMPT.md`.

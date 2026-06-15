# DJConnect Firmware Handoff

## Current State

DJConnect is proprietary ESP32-S3 firmware for the LilyGO T-Embed-CC1101. It is a Home Assistant paired playback remote with local display UI, rotary/top-button controls, LED-ring feedback, speaker cues, push-to-talk voice upload, web portal and Home Assistant pairing/OTA support.

Current repo state includes:

- Latest firmware release target from this repo: `v3.1.22`. Source repo `pcvantol/djconnect-esp32` and public firmware repo `pcvantol/djconnect-firmware` should be cleaned after release so the remote stable semver tag/release set keeps only the latest stable release; generated `release/` artifacts are ignored in source.
- Firmware version flow based on git tag/build flags; local builds remain `dev` / `vdev`.
- Home Assistant device layer with pairing, mDNS discovery, device-token auth, board-specific OTA, DJ response and status updates.
- Board profiles are split through `BoardProfile.h`. The production LilyGO build is `t_embed_cc1101` / `lilygo-t-embed-s3`; ESP32-S3-BOX-3 bring-up is `esp32_s3_box3` / `esp32-s3-box-3`.
- The ESP32-S3-BOX-3 PlatformIO env uses the `esp32s3box` board definition with PSRAM enabled. The LilyGO env remains on the existing no-PSRAM `esp32-s3-devkitc-1` definition until a specific PSRAM-equipped T-Embed-CC1101 variant is verified.
- Playback commands are proxied from the ESP to Home Assistant as generic commands. Spotify OAuth, Sonos credentials or other backend credentials live in Home Assistant, not on the ESP.
- Physical PTT records WAV on the ESP and uploads to the Home Assistant integration; Home Assistant owns Assist/STT/TTS backend work and returns DJ text plus optional WAV/MP3 audio URL. Middle encoder press cancels the active processing/DJ-announcement flow and requests response-audio stream stop as soon as possible.
- Web portal includes Now Playing, DJ-announcement simulation, outputs, playlists, queue with per-item play, refresh and lazy browser-loaded album-art thumbnails, local browser games, logs, diagnostics, OTA upload, WiFi update, settings and status indicators. It uses the DJConnect blue/purple brand styling, shows `Muziekbediening met karakter`, firmware version plus board device model in the title bar, uses lila/magenta playback controls and volume slider, and hides the battery icon on boards without a battery gauge such as ESP32-S3-BOX-3. Queue and playlist panels use compact internal scroll areas. Queue supports up to 20 items from Home Assistant and is de-duplicated by URI or title/subtitle fallback so single-item queues do not render repeated tracks. Device logs are scrollable with the encoder and use compact `HH:mm INF` prefixes.
- Playback proxy control requests use short waits and transient-failure cooldown; OTA writes release wake-word/TFLite and active voice/audio resources before GitHub TLS, tolerate slow GitHub/CDN stream bursts, manually follow GitHub release redirects, log the download host/final URL on transport failures and continue to feed the watchdog and firmware-update LED animation.
- Device main menu includes a local Games submenu with Paddle Rally, Meteor Run, Sky Dash and Maze Chase. Games are local-only, use encoder movement/center-button fire where applicable, persist highscores in NVS and are not exposed through Home Assistant.
- Device Help screen lists top button, encoder button and rotary actions. It appears once after initial Home Assistant pairing, then remains available from the main menu.
- Home Assistant native device commands support two-way playback/settings control through `/api/device/command`.
- Home Assistant should expose the proxied playback as a native `media_player` entity if user-facing HA media controls are desired. That entity represents the backend playback session, not the ESP speaker.
- The boot screen shows `Muziekbediening met karakter` and `https://djconnect.dev` for at least three seconds; the About screen shows the website URL and no longer includes a separate `Firmware / Proprietary` row. WiFi boot connection timeout is 15 seconds. During WiFi connect, the LED ring shows a green chase animation.
- If WiFi cannot connect, the device shows a 100%-brightness recovery menu: retry connect, restart device, turn off, and confirmed factory reset.
- Setup/AP mode keeps brightness at 100%, shows a deeply fading rainbow breath, shows portal active for 10 minutes, allows center-button turn off, and then deep-sleeps if setup is not completed. The captive portal mirrors the blue/purple DJConnect web style and includes the board device model in browser title/header.
- The device Settings menu has a confirmed Change WiFi action that reboots into setup/AP mode while preserving Home Assistant pairing; only Factory reset or Reset Home Assistant pairing clears HA state.
- Home Assistant pairing mode keeps brightness at 100%, keeps BLE advertising active, shows the pair code plus center-button turn-off hint, breathes blue on the LED ring, and then deep-sleeps after 10 minutes if pairing is not completed.
- HA should treat pairing as pending until the ESP confirms token storage and a successful LAN status post. The ESP `/api/device/pair` route accepts a direct HA callback with `device_token`, required LAN `ha_local_url`, and lightweight settings, stores it with minimal in-route work, and lets the next main-loop pass confirm the pairing through `/api/djconnect/status`. Automatic playback polling is delayed briefly after boot/pairing to avoid stacking HA status, playback proxy and wake-word startup work on no-PSRAM LilyGO hardware.
- Device IDs and mDNS hostnames are board-model specific. LilyGO uses `djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX`; ESP32-S3-BOX-3 uses `djconnect-esp32-s3-box-3-XXXXXXXXXXXX`. Home Assistant should use the `model` field/TXT record for device-type routing instead of parsing the old `djconnect-lilygo-` prefix. ESP mDNS discovery is setup-only: unpaired devices advertise `_djconnect._tcp` with `client_type=esp32` alongside `name`, `device_id`, `version`, `paired`, `api` and `model`, and paired devices stop advertising until pairing is reset.
- The ESP rejects persistent legacy IDs such as `djconnect-XXXXXXXXXXXX`, `djconnect-lilygo-XXXXXXXXXXXX` and `djconnect-[six-digit-code]`; the six-digit value is only `pair_code` in pairing-info/pairing UI.
- The ESP requires a real LAN `ha_local_url` for normal status, playback proxy commands and voice calls. If Home Assistant sends a Nabu Casa `.ui.nabu.casa` URL as `ha_local_url`, firmware rejects the pairing callback instead of entering a half-paired state. Playback fails clearly with `HA playback command unavailable: local HA URL missing` when local is absent. Cloud/Nabu Casa URLs are not accepted, stored, reported, or used by the ESP runtime.
- Direct pairing during the pairing screen leaves pairing mode first so BLE can shut down before the Okay Nabu TensorFlow runtime allocates its arena.
- Okay Nabu wake word runs locally through TensorFlow Lite Micro plus the TensorFlow micro_speech frontend. Current tuning uses a 10 ms feature step and 3-frame sliding window. LilyGO uses a `0.90` cutoff; ESP32-S3-BOX-3 uses a roomier `0.86` cutoff. Wake-word recordings auto-stop after 1.2 seconds of silence and are hard-capped at 15 seconds.
- After WiFi/Home Assistant setup, the ESP posts HA status immediately and delays the first automatic playback status poll by a short boot grace period. Physical playback controls can still send commands immediately.
- `/api/djconnect/command` should distinguish auth from backend availability. 401/403/404 means stale pairing. Playback/backend unavailability should be HTTP 200 with `success:false` and `backend_available:false`, not HTTP 503 during normal pairing/status flow. Command payloads are identity-only (`device_id`, `client_type:"esp32"`, `payload_type:"command"` and `firmware`) and must not be treated as authoritative device-status snapshots.
- Periodic `/api/djconnect/status` is the authoritative source for Home Assistant ESP sensors and mirrors device settings/entities: `ha_pairing_status`, `local_url`, screen brightness aliases, screen timeout aliases, turn-off timeout, speaker/cue volume aliases, language, theme, log level, OTA/update state, screen state and LED state.
- HA integration and firmware must share the same major/minor protocol version. `3.0.z` firmware should talk to `3.0.z` HA integration; patch versions may differ. HTTP 426 `version_mismatch` is an update-required protocol block, not a pairing-token failure.
- Backend credentials are never accepted by ESP firmware.
- Top-button soft reset plays a dedicated cue and bright white LED-ring flashes before reboot. Turn-off/deep-sleep always plays a rainbow LED fade-out.
- Freshly provisioned unpaired release firmware performs a graceful pre-pairing bootstrap update check after WiFi connects. It skips local `dev`/`vdev` builds and continues to pairing silently if the check fails.
- Release tooling runs `scripts/update_build_dependencies.sh` before compiling firmware. It upgrades PlatformIO Core, updates global/project PlatformIO packages for both board environments, writes a dependency diff so `THIRD_PARTY_NOTICES.md` plus `DESIGN_DECISIONS.md` can be refreshed whenever frameworks, libraries or tools changed, and publishes GitHub release notes from the matching `CHANGELOG.md` version section.

Latest validated commands:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/djconnect_unit_tests && /tmp/djconnect_unit_tests
bash test/native/test_release.sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
/Users/pcvantol/.platformio/penv/bin/pio run -e esp32_s3_box3
```

The native logic test, release-script test and local LilyGO/BOX-3 builds passed during `v3.1.22` preparation. The release flow builds both `djconnect-lilygo-t-embed-s3-v3.1.22.bin` and `djconnect-esp32-s3-box-3-v3.1.22.bin`, both `.sha256` files and `firmware_manifest.json` for `pcvantol/djconnect-firmware`.

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
- `BoardProfile`: compile-time board identity, pinout and hardware capability abstraction for LilyGO and ESP32-S3-BOX-3 builds.

Core data/security boundaries:

- Device API uses `Authorization: Bearer <device_token>` for protected endpoints.
- Home Assistant pairing validity is runtime state. HA 401/403/404 marks pairing stale/red but does not erase established stored pairing automatically. Playback proxy commands are disabled until a successful authenticated HA status post confirms the stored token.
- Playback proxy backend availability is separate from pairing validity. A command response with `success:false` and `backend_available:false` marks the playback music-note indicator red but must not clear pairing or rotate tokens.
- Device status belongs on `/api/djconnect/status`. Do not reintroduce battery, firmware, RSSI, pairing, screen, LED, settings or sound-output fields onto playback command payloads as partial snapshots.
- Playback-backend refresh tokens are never stored on the ESP and therefore are never logged or shown back to users.
- The ESP is not a Spotify Connect speaker/player and should not be modeled as the actual music sink. It mirrors and controls the playback state supplied by Home Assistant.
- Internal ESP32 Preferences keys must be 15 chars or less.

## Decisions Made

- Home Assistant is the trusted backend for pairing, playback command interpretation, backend credentials, Assist/STT/TTS orchestration, OTA offer handling and native command entities.
- The ESP stays focused on local edge behavior: display, controls, LED ring, battery/power policy, mic capture and playback of HA-provided DJ response audio.
- A Home Assistant `media_player` entity may be implemented in the integration for the playback proxy. HA `media_player.pause`, `media_player.play_media`, `media_player.volume_set`, source selection and next/previous should be translated by the integration into backend playback actions and/or ESP `/api/device/command` updates. Do not make this entity imply that Spotify/Sonos music audio is played by the ESP.
- The ESP speaker remains reserved for local cues and DJ/voice response audio. Treat that as device-local feedback, separate from the backend music `media_player`.
- Physical PTT must stay on the WAV-upload route to `/api/djconnect/voice`; do not reintroduce direct ESP Home Assistant Assist WebSocket authentication. Keep the processing/DJ-announcement cancel path responsive from the middle encoder button.
- Web PTT is a compact DJ-announcement simulation path. It requires HA pairing but not playback-backend credentials on the ESP, active playback or browser microphone permission.
- Smart Shuffle was removed because Spotify does not expose a useful public Web API control for it.
- Current song is a menu screen and uses top-button back; it does not start PTT.
- Up Next item playback requires a valid context URI. The ESP preserves queue `context_uri` from Home Assistant and uses it as a fallback when the latest playback snapshot has no context. Queue display state accepts up to 20 items and is de-duplicated on the ESP before device/web rendering.
- Web Up Next album art is URL pass-through only. The ESP does not download queue thumbnails; the browser lazy-loads them when the queue panel is visible.
- Release binaries/manifests are generated locally under `release/` but ignored by git; published OTA assets live only in `pcvantol/djconnect-firmware`, not in the closed-source source repo.
- Release manifests use a `firmwares` array only. There is no legacy single-device asset compatibility: LilyGO uses `djconnect-lilygo-t-embed-s3-vX.Y.Z.bin`, ESP32-S3-BOX-3 uses `djconnect-esp32-s3-box-3-vX.Y.Z.bin`, and HA compatibility is derived from firmware version `X.Y.Z` as `min_ha_integration:"X.Y.0"` plus exclusive `max_ha_integration:"X.(Y+1).0"`.
- Local `dev` / `vdev` builds report OTA-comparable version `0.0.0` to Home Assistant/device API so published releases are seen as upgrades.
- Bootstrap OTA is separate from normal HA OTA: it only runs before pairing, checks the public firmware release API, skips dev firmware, and reuses the normal OTA write/display/LED/sound path when a newer release is found.
- True NVS Encryption is not active in the current Arduino/PlatformIO build. It requires an ESP-IDF or Arduino-as-component build with `CONFIG_NVS_ENCRYPTION=y` plus an `nvs_keys` partition and factory/serial flashing. OTA alone cannot safely enable it.

## Known Issues

- NVS credentials are currently stored in ESP32 NVS but not encrypted by this Arduino/PlatformIO build.
- ESP32-S3-BOX-3 display bring-up needs physical validation. Logs can show backlight/display initialization while the panel still displays white/no text on hardware.
- OTA status clearing in Home Assistant depends on the integration processing the post-boot status payload correctly.
- Home Assistant sensor reset behavior must be verified against the current integration. If sensors briefly populate and then become unknown/pending, the fix belongs in the integration entity/coordinator refresh path.
- HA native entities for brightness, speaker volume, timeouts, language, theme and log level should read the mirrored `/api/djconnect/status` settings fields; otherwise they may show minimum/default values until changed from HA.
- If HA reposts direct pairing/settings callbacks too often during normal playback commands, debounce or route those updates so they do not spam `/api/device/pair`. The ESP now treats identical direct-pair callbacks as idempotent, but the integration should still reserve `/api/device/pair` for initial pairing, explicit re-pair/token rotation or recovery.
- Keep the Home Assistant integration and firmware contract synchronized around native device entities, optional playback `media_player`, OTA status clearing, stale pairing behavior and command/status payloads.
- Home Assistant OTA selection must use the matching `firmwares[]` entry for the paired device model. Do not fall back to the removed generic firmware asset naming or top-level single-device manifest fields.
- Queue, playlist and output metadata now come from Home Assistant. Backend-specific fallbacks belong in the integration.
- If Home Assistant returns duplicate queue entries for single-track queues, firmware de-duplicates them for display; the integration should still prefer returning the actual backend queue shape rather than padding with the current item.
- The Home Assistant integration must send a real LAN `ha_local_url` during pairing. Sending Nabu Casa as local is rejected by firmware; sending a stale token gives a local `/api/djconnect/status` 401 and keeps H red.
- MP3 DJ-announcement playback can still be sensitive to decoder/runtime blocking; watchdog handling has been improved but should be stress-tested with varied MP3 lengths/bitrates.
- Okay Nabu detection works locally through TFLite Micro but still needs real-room tuning for false positives, missed detections and silence auto-stop on both boards.
- ESP32-S3-BOX-3 speaker, mic and button mappings need physical validation after the board abstraction work.
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
4. Add integration-side verification for OTA status clearing and sensor retention after successful firmware boot.
5. Implement/verify native Home Assistant entities for every command previously handled locally, using `/api/device/command`, and refresh their state from the ESP `/api/djconnect/status` settings payload.
6. Add a Home Assistant `media_player` entity for proxied playback state/control:
   - state: playing, paused, idle/unavailable from integration playback state;
   - attributes: title, artist, album art, output/source, volume and supported features;
   - commands: play/pause, next/previous, volume, source/output selection and playlist/media start;
   - keep actual backend credentials and playback API calls in Home Assistant.
7. Validate ESP32-S3-BOX-3 display, speaker, mic and button mappings on physical hardware.
8. Re-test OTA from a no-PSRAM LilyGO on the latest stable release to confirm the GitHub TLS memory issue is gone.
9. Stress-test DJ-announcement MP3 playback with several short and long files.
10. Continue reducing `DJConnectApp` size by moving setup/captive/BLE flow into a dedicated `ProvisioningController` runtime flow or `SetupController`.
11. Update GitHub Actions for Node.js 24 compatibility.
12. Add product security review for local HTTP device API, bearer-token lifetime, replay behavior and factory reset behavior.

## Cross-Repo Sync Prompts

The canonical copy/paste prompts for syncing the Home Assistant integration, Apple app, ESP firmware, Raspberry Pi client and website/docs repos are maintained only in `pcvantol/djconnect/SYNC_PROMPTS.md`. This firmware repo intentionally does not keep a local copy. When a firmware change updates a shared contract, make a follow-up change in `pcvantol/djconnect`.

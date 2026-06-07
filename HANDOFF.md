# SpotifyDJ Firmware Handoff

## Current State

SpotifyDJ is proprietary ESP32-S3 firmware for the LilyGO T-Embed-CC1101. It is a Spotify Connect remote with local display UI, rotary/top-button controls, LED-ring feedback, speaker cues, push-to-talk voice upload, web portal, MQTT and Home Assistant pairing/OTA support.

Current repo state includes:

- Firmware version flow based on git tag/build flags; local builds remain `dev` / `vdev`.
- Home Assistant device layer with pairing, mDNS discovery, device-token auth, OTA, Spotify provisioning, DJ response and status updates.
- Spotify OAuth credentials are provisioned through Home Assistant, setup portal or web repair form and stored in NVS with short Preferences keys.
- Physical PTT records WAV on the ESP and uploads to the Home Assistant integration; Home Assistant owns Assist/STT/TTS backend work and returns DJ text plus optional WAV/MP3 audio URL.
- Web portal includes Now Playing, DJ-response simulation, outputs, playlists, logs, diagnostics, OTA upload, WiFi update, Spotify repair, settings and status indicators.
- MQTT publishes status/discovery and supports two-way command/settings control.
- WiFi boot connection timeout is 30 seconds. During WiFi connect, the LED ring shows a blue animation.
- If WiFi cannot connect, the device shows a 100%-brightness recovery menu: retry connect, restart device, turn off, and confirmed factory reset.
- Setup/AP mode keeps brightness at 100%, shows portal active for 10 minutes, allows center-button turn off, and then deep-sleeps if setup is not completed.

Latest validated commands:

```sh
clang++ -std=c++17 -Iinclude -I.pio/libdeps/t_embed_cc1101/ArduinoJson/src test/native/test_logic.cpp -o /tmp/spotifydj_logic_test && /tmp/spotifydj_logic_test
bash test/native/test_release.sh
/Users/pcvantol/.platformio/penv/bin/pio run -e t_embed_cc1101
```

All passed after the latest WiFi/setup/security documentation changes.

## Architecture

Main module boundaries:

- `SpotifyDJApp`: top-level orchestration, setup flow, loop routing, input/screen transitions.
- `DisplayManager`: display drawing only.
- `SpotifyClient`: Spotify Web API auth, playback state and control.
- `WebPortal`: embedded mobile web UI and local web actions.
- `ProvisioningController`: NVS provisioning/settings storage for WiFi, MQTT, language/theme/log level, display/power settings and cue volume.
- `PowerController`: charger/wake/deep-sleep/watchdog policy.
- `MqttPublisher`: MQTT status, discovery, command handling and settings exposure.
- `SpotifyDJDevice`, `SpotifyDJPairing`, `SpotifyDJDiscovery`, `SpotifyDJApiServer`, `SpotifyDJOTA`: Home Assistant device layer.
- `VoiceRecorder` and `VoiceHttpClient`: physical PTT WAV capture/upload.
- `DjResponseAudioPlayer` and `SoundManager`: DJ response audio dispatch/playback and generated speaker cues.
- `LogicHelpers`, `SpotifyDJMenuModel`, `NetworkActivityLogic`: host-testable pure logic.

Core data/security boundaries:

- Device API uses `Authorization: Bearer <device_token>` for protected endpoints.
- Home Assistant pairing validity is runtime state. HA 401/403/404 marks pairing stale/red but does not erase stored pairing automatically.
- Spotify refresh tokens are never logged or shown back to users.
- MQTT passwords are never logged.
- Internal ESP32 Preferences keys must be 15 chars or less.

## Decisions Made

- Home Assistant is the trusted backend for pairing, Spotify command interpretation, Assist/STT/TTS orchestration, OTA offer handling and optional MQTT provisioning.
- The ESP stays focused on local edge behavior: display, controls, LED ring, battery/power policy, mic capture and playback of HA-provided DJ response audio.
- Physical PTT must stay on the WAV-upload route to `/api/spotify_dj/voice`; do not reintroduce direct ESP Home Assistant Assist WebSocket authentication.
- Web PTT is a compact DJ-response simulation path. It requires HA pairing but not Spotify credentials, active playback or browser microphone permission.
- Smart Shuffle was removed because Spotify does not expose a useful public Web API control for it.
- Current Song is a menu screen and uses top-button back; it does not start PTT.
- Release binaries/manifests are published separately from the closed-source firmware source repo.
- Local `dev` / `vdev` builds report OTA-comparable version `0.0.0` to Home Assistant/device API so published releases are seen as upgrades.
- True NVS Encryption is not active in the current Arduino/PlatformIO build. It requires an ESP-IDF or Arduino-as-component build with `CONFIG_NVS_ENCRYPTION=y` plus an `nvs_keys` partition and factory/serial flashing. OTA alone cannot safely enable it.

## Known Issues

- NVS credentials are currently stored in ESP32 NVS but not encrypted by this Arduino/PlatformIO build.
- OTA status clearing in Home Assistant depends on the integration processing the post-boot status payload correctly.
- Spotify queue endpoint may return empty for playlist playback; firmware falls back to playlist tracks, but behavior still depends on Spotify API context availability and scopes.
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
5. Stress-test DJ-response MP3 playback with several short and long files.
6. Continue reducing `SpotifyDJApp` size by moving setup/captive/BLE flow into a dedicated `ProvisioningController` runtime flow or `SetupController`.
7. Add release checklist item for confirming public GitHub Action firmware contains the expected `vX.Y.Z` marker.
8. Add product security review for local HTTP device API, bearer-token lifetime, replay behavior and factory reset behavior.

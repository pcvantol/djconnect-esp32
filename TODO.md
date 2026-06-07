# SpotifyDJ Firmware Backlog

Concrete backlog for follow-up implementation and validation.

## Security

- [ ] Implement true NVS Encryption for credentials.
  - Requires `CONFIG_NVS_ENCRYPTION=y`.
  - Requires partition table with `nvs_keys`.
  - Requires factory/serial flash migration plan; OTA alone is not enough.
  - Define how existing plaintext NVS credentials are wiped or migrated.
- [ ] Evaluate ESP32 Flash Encryption for production devices.
  - Define secure boot/flash encryption provisioning workflow.
  - Document impact on debugging, OTA and factory support.
- [ ] Review local HTTP API security.
  - Token lifetime and rotation.
  - Replay risk.
  - Rate limiting for protected endpoints.
  - Behavior after stale Home Assistant pairing.
- [ ] Add release-time secret scan to CI.
  - Block obvious WiFi passwords, refresh tokens, device tokens and private HA URLs.
- [ ] Confirm every credential write path avoids Serial/web logging.

## WiFi / Setup / Provisioning

- [ ] Add host tests for WiFi-failure menu ordering:
  - retry connect;
  - restart device;
  - turn off;
  - factory reset last.
- [ ] Add host tests for WiFi-failure factory-reset confirmation.
- [ ] Add host tests for setup/AP timeout text and center-button turn-off state.
- [ ] Consider moving captive portal, BLE provisioning and setup/AP loop out of `SpotifyDJApp` into a dedicated `SetupController`.
- [ ] Verify on-device that WiFi connect timeout is actually 30 seconds.
- [ ] Verify blue LED-ring animation during WiFi connect.
- [ ] Verify setup/AP screen shows portal active for 10 minutes.
- [ ] Verify center-button turn off in setup/AP mode.
- [ ] Verify factory reset in WiFi-failure menu requires confirmation.

## Home Assistant Integration

- [ ] Validate OTA status clearing after successful update and reboot.
- [ ] Validate runtime stale-pairing behavior for HA 401, 403 and 404.
- [ ] Confirm HA integration reprovisions latest rotated Spotify refresh token after ESP reports `spotify_configured=false`.
- [ ] Confirm HA integration can return DJ response text plus WAV URL.
- [ ] Confirm HA integration can return DJ response text plus MP3 URL.
- [ ] Confirm Home Assistant status payload includes firmware version after OTA boot.
- [ ] Add integration-side test prompt/checklist for `/api/spotify_dj/voice` STT/TTS provider configuration.

## Spotify

- [ ] Re-test playlist fallback for Up Next when Spotify queue endpoint returns empty.
- [ ] Confirm `playlist-read-private` scope is present in current PKCE helper and HA OAuth flow.
- [ ] Validate `SpotifyDJ Liked Proxy` lookup against:
  - private playlist;
  - public playlist;
  - larger library with multiple playlist pages.
- [ ] Add host tests for Liked Proxy search fallback logic if it can be isolated.
- [ ] Confirm no Spotify controls are active when there is no playback.

## Voice / DJ Response

- [ ] Stress-test MP3 DJ-response playback with multiple lengths/bitrates.
- [ ] Stress-test WAV DJ-response playback after repeated web simulation calls.
- [ ] Confirm green LED-ring animation stays active while DJ response audio plays.
- [ ] Confirm DJ response overlay closes after timeout and does not let Now Playing bleed through.
- [ ] Add guard tests for DJ response audio type detection:
  - `audio/wav`;
  - `audio/x-wav`;
  - `audio/mpeg`;
  - `audio/mp3`;
  - RIFF/WAVE magic bytes;
  - ID3 magic bytes;
  - MP3 frame sync.
- [ ] Confirm PTT works only from Now Playing and not from Current Song.
- [ ] Confirm web DJ-response simulation requires HA pairing but not Spotify credentials.

## Web Portal

- [ ] Re-test visibility-aware polling:
  - logs only while logs panel is visible and not paused;
  - queue only while queue block is visible;
  - playlists only while playlist UI is visible;
  - outputs only while output UI is visible.
- [ ] Verify Safari PWA icon/home-screen behavior on iPhone.
- [ ] Verify all user-facing labels are translated in English and Dutch.
- [ ] Add smoke tests for key generated web HTML fragments if feasible.
- [ ] Re-test web portal speed on weak WiFi and note any remaining slow endpoints.

## MQTT

- [ ] Re-test MQTT auth-failure lock after three `rc=4`/`rc=5` failures.
- [ ] Confirm changing MQTT credentials clears the auth-failure lock.
- [ ] Confirm all settings exposed via MQTT commands still match web/device settings:
  - language;
  - theme;
  - log level;
  - speaker cue volume;
  - brightness;
  - screen timeout;
  - turn-off timeout.
- [ ] Confirm MQTT discovery payloads remain valid after adding/removing settings.

## Power / Battery

- [ ] Re-test low-battery screens at `<20%` and `<10%`.
- [ ] Re-test charger-aware wake probe.
- [ ] Verify battery/charging state does not briefly show false charging after reboot.
- [ ] Add more host tests for `PowerController` wake/charger decisions.
- [ ] Decide if production devices need calibration notes for voltage-estimated battery percentage.

## OTA / Release

- [ ] Confirm GitHub Action release binary contains expected `vX.Y.Z` marker.
- [ ] Confirm only one public OTA binary asset is published for the target device.
- [ ] Confirm manifest `device` value matches ESP OTA validation: `lilygo-t-embed-s3`.
- [ ] Confirm OTA update screen shows target version.
- [ ] Confirm OTA start/progress/complete/failure beeps are simple and not stuttery.
- [ ] Confirm purple LED-ring animation is visible during OTA.
- [ ] Add CI check for `release.sh --dry-run` and invalid version if not already wired.

## Refactor

- [ ] Extract setup/AP/captive/BLE flow from `SpotifyDJApp` into `SetupController`.
- [ ] Continue reducing blocking paths in Spotify/HA/audio code using `NetworkActivity`.
- [ ] Consider isolating DJ response overlay state into a small UI state object.
- [ ] Consider splitting web route registration into smaller route groups.
- [ ] Keep pure option/count logic in host-testable helpers instead of embedded in display code.

## Documentation

- [ ] Keep `README.md`, `AGENTS.md`, `CHANGELOG.md`, `HANDOFF.md` and this backlog aligned after each feature slice.
- [ ] Add production security migration notes when NVS Encryption/Flash Encryption are actually implemented.
- [ ] Add a manual factory test checklist for sold devices.
- [ ] Add troubleshooting section for HA STT/TTS provider errors.

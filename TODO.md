# DJConnect Firmware Issues And Backlog

Concrete issue and task list after release `v3.2.0`.

## Open Issues

- [ ] Home Assistant sensor reset issue must be verified on the current HA integration.
  - Firmware status payload is authoritative and the `3.2.x` firmware contract still posts periodic status.
  - If HA sensors briefly populate and then become unknown/pending, fix integration entity refresh/coordinator behavior.
- [ ] OTA from the physical LilyGO should be re-tested with `v3.2.x`.
  - Firmware now releases wake-word/TFLite and active voice/audio resources before GitHub TLS.
  - Confirm GitHub TLS no longer fails with memory allocation errors on no-PSRAM LilyGO hardware.
- [ ] Okay Nabu wake-word reliability still needs real-room tuning.
  - LilyGO cutoff is currently `0.90`.
  - Validate false positives, missed detections, silence auto-stop and PTT handoff.
- [ ] MP3 DJ-announcement audio playback needs more stress testing.
  - Test short/long MP3s and repeated HA/web flows.
  - Confirm watchdog stays fed and LED animation remains live.
- [ ] GitHub Actions emits Node.js 20 deprecation warnings.
  - Current release workflow passes, but actions should be updated or opted into Node.js 24 before GitHub forces the runner default.

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
- [ ] Consider moving captive portal, BLE provisioning and setup/AP loop out of `DJConnectApp` into a dedicated `SetupController`.
- [ ] Verify on-device that WiFi connect timeout is actually 15 seconds.
- [ ] Verify green LED-ring animation during WiFi connect.
- [ ] Verify setup/AP screen shows portal active for 10 minutes.
- [ ] Verify center-button turn off in setup/AP mode.
- [ ] Verify factory reset in WiFi-failure menu requires confirmation.
- [ ] Verify BLE advertising stays active in Home Assistant pairing mode.
- [ ] Verify pairing mode shows the center-button turn-off hint and blue breathing LED ring.
- [ ] Verify holding the top button for soft reset in pairing/setup never flashes the normal menu first.

## Home Assistant Integration

- [ ] Validate OTA status clearing after successful update and reboot.
- [ ] Validate runtime stale-pairing behavior for HA 401, 403 and 404.
- [ ] Validate that Home Assistant sends a real LAN `ha_local_url` during pairing.
- [ ] Validate that Home Assistant never sends Nabu Casa `.ui.nabu.casa` as `ha_local_url`.
- [ ] Confirm HA reports pairing as pending until ESP confirms token storage through `/api/device/pair`.
- [ ] Confirm playback commands stay disabled until `/api/djconnect/status` accepts the stored device token.
- [ ] Confirm HA integration owns backend OAuth/credential refresh without sending backend tokens to the ESP.
- [ ] Confirm new HA/client setup and options flows do not expose legacy playback
  source/default-playlist override options and use `Client adres` for the client
  URL label.
- [ ] Confirm HA integration can return DJ announcement text plus WAV URL.
- [ ] Confirm HA integration can return DJ announcement text plus MP3 URL.
- [ ] Confirm Home Assistant status payload includes firmware version after OTA boot.
- [ ] Add integration-side test prompt/checklist for `/api/djconnect/voice` STT/TTS provider configuration.
- [ ] Verify HA media/player play-pause entity actions mirror back on the device screen.
- [ ] Add HA config-flow tests for Raspberry Pi mDNS discovery and pairing:
  - `_djconnect._tcp` TXT acceptance for `client_type=raspberry_pi`;
  - `/api/device/pairing-info` overriding TXT data;
  - one discovered Pi prefilled by default;
  - multiple discovered Pi clients shown in a selector;
  - duplicate `device_id` handling;
  - pairing-info failure staying explicit instead of falling back to `djconnect-{pair_code}`.

## Playback Backend

- [ ] Re-test playlist fallback for Up Next when the backend queue endpoint returns empty.
- [ ] Re-test direct queue item playback for `spotify:episode:*` and `spotify:track:*` items without `context_uri`/`queue_context`.
- [ ] Re-test queue context playback for playlist, album and show contexts to confirm context plus offset behavior still works.
- [ ] Confirm required backend playlist scopes/configuration are handled in the HA integration.
- [ ] Validate default playlist lookup against:
  - private playlist;
  - public playlist;
  - larger library with multiple playlist pages.
- [ ] Add host tests for default-playlist search fallback logic if it can be isolated.
- [ ] Confirm no playback controls are active when there is no playback.
- [ ] Confirm center button starts the default playlist, enables shuffle and turns repeat off after start succeeds.

## Voice / DJ Announcement

- [ ] Stress-test MP3 DJ-announcement playback with multiple lengths/bitrates.
- [ ] Stress-test WAV DJ-announcement playback after repeated web simulation calls.
- [ ] Confirm purple LED-ring animation stays active while DJ announcement audio plays.
- [ ] Confirm DJ announcement overlay closes after timeout and does not let Now Playing bleed through.
- [ ] Add guard tests for DJ announcement audio type detection:
  - `audio/wav`;
  - `audio/x-wav`;
  - `audio/mpeg`;
  - `audio/mp3`;
  - RIFF/WAVE magic bytes;
  - ID3 magic bytes;
  - MP3 frame sync.
- [ ] Confirm PTT works only from Now Playing and not from Current Song.
- [ ] Confirm web DJ-announcement simulation requires HA pairing but not backend credentials on the ESP.
- [ ] Confirm middle encoder button cancels processing/DJ-announcement flow quickly.
- [ ] Confirm PTT start cue is not included in uploaded WAV.

## Web Portal

- [ ] Verify web title bar shows `Muziekbediening met karakter`, firmware version and device model on mobile and desktop widths.
- [ ] Verify playback controls and volume slider use the lila/magenta accent instead of orange.
- [ ] Re-test visibility-aware polling:
  - logs only while logs panel is visible and not paused;
  - queue only while queue block is visible;
  - playlists only while playlist UI is visible;
  - outputs only while output UI is visible.
- [ ] Verify Safari PWA icon/home-screen behavior on iPhone.
- [ ] Verify all user-facing labels are translated in English and Dutch.
- [ ] Add smoke tests for key generated web HTML fragments if feasible.
- [ ] Re-test web portal local games on touch, mouse and keyboard:
  - Maze Chase four direction buttons and power pellets;
  - Meteor Run straight-falling varied meteors;
  - Sky Dash varied obstacles and moving star streaks;
  - Paddle Rally equal-height controls and hit/miss feedback.
- [ ] Re-test web portal speed on weak WiFi and note any remaining slow endpoints.

## Home Assistant Native Entities

- [ ] Implement matching Home Assistant entities for every ESP `/api/device/command` payload:
  - status refresh;
  - next/previous;
  - volume;
  - sound output;
  - playlist start;
  - language;
  - theme;
  - log level;
  - speaker cue volume;
  - brightness;
  - screen timeout;
  - turn-off timeout.
- [ ] Confirm HA entity changes use bearer-token auth and handle 401/403/404 as stale pairing.

## Power / Battery

- [ ] Re-test low-battery screens at `<20%` and `<10%`.
- [ ] Re-test charger-aware wake probe.
- [ ] Verify idle turn-off sleep is suppressed while USB-C/external power is connected and still enters deep sleep on battery after the configured timeout.
- [ ] Verify boot logs include reset reason and wakeup cause after panic/watchdog, brownout simulation if available, manual reset and deep-sleep wake.
- [ ] Verify battery/charging state does not briefly show false charging after reboot.
- [ ] Add more host tests for `PowerController` wake/charger decisions.
- [ ] Decide if production devices need calibration notes for voltage-estimated battery percentage.

## OTA / Release

- [x] Confirm GitHub Action release binary contains expected `v3.1.31` marker.
- [x] Confirm the public OTA binary asset is published for `v3.1.31`:
  - `djconnect-lilygo-t-embed-s3-v3.1.31.bin`.
- [x] Confirm manifest is published with the release.
- [ ] Confirm manifest uses only the `firmwares` array and no legacy top-level single-device OTA fields on every future release.
- [ ] Confirm manifest `device` values match ESP OTA validation on every future release:
  - `lilygo-t-embed-s3`.
- [x] Confirm `min_ha_integration` is derived from the firmware major/minor version (`3.1.31` -> `3.1.0`) and `max_ha_integration` is `<3.2.0`.
- [x] Update release hygiene snapshot checksum for `assets/website/site.webmanifest` after the tagline change.
- [ ] Confirm OTA update screen shows target version.
- [ ] Confirm OTA start/progress/complete/failure beeps are simple and not stuttery.
- [ ] Confirm purple LED-ring animation is visible during OTA.
- [x] Add CI/local check for `release.sh --dry-run` and invalid version.
- [ ] Update GitHub Actions to Node.js 24-compatible action versions or opt in once the runner default changes.

## Refactor

- [ ] Extract setup/AP/captive/BLE flow from `DJConnectApp` into `SetupController`.
- [ ] Continue reducing blocking paths in Spotify/HA/audio code using `NetworkActivity`.
- [ ] Consider isolating DJ-announcement overlay state into a small UI state object.
- [ ] Consider splitting web route registration into smaller route groups.
- [ ] Keep pure option/count logic in host-testable helpers instead of embedded in display code.

## Documentation

- [ ] Keep `README.md`, `DESIGN_DECISIONS.md`, `AGENTS.md`, `CHANGELOG.md`, `HANDOFF.md` and this backlog aligned after each feature slice/release.
- [ ] Add production security migration notes when NVS Encryption/Flash Encryption are actually implemented.
- [ ] Add a manual factory test checklist for sold devices.
- [ ] Add troubleshooting section for HA STT/TTS provider errors.

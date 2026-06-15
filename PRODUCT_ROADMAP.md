# DJConnect Product Roadmap

Canonical product roadmap for all DJConnect repositories. Keep this file
byte-for-byte identical in:

- `pcvantol/djconnect`
- `pcvantol/djconnect-app`
- `pcvantol/djconnect-esp32`
- `pcvantol/djconnect-website`
- `pcvantol/djconnect-pi`

This roadmap is broader than `TODO.md`: not every idea here is committed scope.
Use it to shape releases, validate demand and decide what belongs in the free
local/Home Assistant product versus optional premium features.

## Product Proposition

DJConnect. Jouw persoonlijke muziek DJ.

Core promise:

- Fast physical music control without opening a phone.
- Natural voice-driven music requests through Home Assistant Assist.
- A calm now-playing experience for shared living spaces.
- Local-first control, with Home Assistant owning backend credentials.
- One shared backend contract across ESP32 hardware, Apple clients, Raspberry
  Pi/Linux clients and the website/docs experience.

## Release Cycle Rule

Every release must review this roadmap.

- Keep this file synchronized across all DJConnect repos.
- Move implemented items from unchecked to checked in the relevant category.
- Add the implementing major.minor version in parentheses, for example
  `[x] Queue supports up to 100 items (3.1)`.
- If an item ships only for one client, mark the client explicitly, for example
  `[x] ESP32 screenshot endpoint (3.1, ESP32)`.
- Do not remove shipped ideas immediately; keep checked items as product memory
  until a later roadmap cleanup.
- Update README, changelog, handoff, sync prompts, tests and design decisions
  when a roadmap item changes product behavior or public contract.

## Production Release Must-Haves

### General Product Development

- [ ] Stable `3.1.x` client/integration compatibility policy, with clear
  major.minor mismatch errors and no automatic token wipe.
- [ ] Redacted diagnostics for support without bearer tokens, Spotify tokens,
  WiFi passwords, Home Assistant tokens or temporary media URLs.
- [ ] Clear error states for unpaired, stale token, backend unavailable, version
  mismatch, Home Assistant unreachable, STT failed and TTS failed.
- [ ] Release hygiene across all repos: docs, changelog, handoff, sync prompts,
  technical design decisions, roadmap, tests and cleanup reviewed before every
  release.
- [ ] Public download/update path for every released client.
- [ ] Manual smoke checklist for website, Home Assistant integration, ESP32,
  Apple clients and Raspberry Pi client.

### HACS / Home Assistant Integration

- [ ] Stable HACS installation path with polished icon, description, website
  link, README rendering and restart/repair text.
- [ ] Robust Spotify OAuth refresh-token rotation without normal playback repair
  loops.
- [ ] Clear Spotify Premium requirement and resilient reauthorization flow.
- [ ] Reliable Assist STT/TTS setup guidance and diagnostics.
- [ ] Stable entity model per client type:
  ESP32 hardware entities, app-like client runtime entities and backend/playback
  entities without irrelevant controls.
- [ ] HA sensors stay stable after status sync and do not fall back to unknown
  after initial valid values.
- [ ] mDNS pairing plus manual Client API URL fallback for networks where
  Bonjour is filtered, stale or unavailable.
- [ ] Queue/up-next response returns max 100 real backend items, artwork URLs,
  context URI and no artificial duplicate padding.
- [ ] Last STT text, resolved Spotify result, DJ announcement and last track are
  visible as entities or redacted debug attributes.
- [ ] DJ announcement prompt is configurable, multiline and isolated from
  Spotify search/device lookup prompts.
- [ ] End-to-end tests cover pairing, voice, Spotify search, queue, refresh-token
  rotation, mDNS discovery, non-ESP entity filtering and OTA offers.

### Website / Docs

- [ ] Canonical domain, SEO metadata, sitemap, redirects and social preview are
  current.
- [ ] Setup page remains the single source for installation guidance.
- [ ] Compatibility matrix for ESP32, iOS, macOS, Raspberry Pi/Linux and Home
  Assistant versions.
- [ ] Troubleshooting pages for Spotify OAuth, STT failed, TTS failed, mDNS
  discovery, Client API URL, OTA and pairing reset.
- [ ] Product screenshots/videos show PTT, queue, DJ announcement, hardware UI
  and Home Assistant entities.
- [ ] Privacy notice accurately describes website and product behavior.
- [ ] Aggregate download/HACS counters remain cookieless.
- [ ] Link checker, translation coverage and Playwright smoke checks run in
  release validation.

### ESP32 Firmware

- [ ] Stable Home Assistant pairing with model-specific device IDs,
  `client_type=esp32`, mDNS discovery, pairing token storage and stale-pairing
  recovery.
- [ ] OTA reliability with board-specific firmware selection, SHA256
  verification, low-memory handling, useful progress/errors and safe reboot.
- [ ] Wake-word and PTT reliability: Okay Nabu false positives/misses, silence
  auto-stop, WAV capture quality, STT failure handling and DJ-announcement
  playback.
- [ ] Playback command reliability from device, web and HA: play/pause,
  previous, next, volume, shuffle, repeat, output transfer, standard playlist
  and queue item playback.
- [ ] Power and battery stability: no low-battery flicker, predictable
  charging/deep-sleep behavior and first input after screen-off only wakes the
  display.
- [ ] Web portal polish: mobile layout, album art popover, queue refresh, games,
  settings, diagnostics and OTA upload in the DJConnect blue/purple style.
- [ ] LilyGO T-Embed S3 and ESP32-S3-BOX-3 builds, manifests, docs and OTA
  selection remain in lockstep.
- [ ] Serial/web logs remain atomic, searchable and useful for support.
- [x] Up Next stores and renders up to 100 queue items from Home Assistant
  before local truncation (3.1, ESP32).
- [x] Local debug screenshot and screen-open endpoints support automated screen
  capture in development firmware (3.1, ESP32).

### Apple Clients: iOS / macOS

- [ ] Stable app pairing through Home Assistant with one persistent device ID
  per installation.
- [ ] Local `/api/device/*` endpoint for HA-to-app traffic where required by the
  pairing/runtime contract.
- [ ] Clear LAN/Bonjour permission guidance and manual Client API URL fallback.
- [ ] Current playback, queue, DJ announcement and status views match the shared
  Home Assistant contract.
- [ ] App-side diagnostics copy/export with redaction and issue-template links.
- [ ] Demo mode remains local and does not create Home Assistant devices.
- [ ] App Store/TestFlight readiness checklist for permissions, privacy copy,
  onboarding and failure states.

### Raspberry Pi / Linux Client

- [ ] Stable pairing with persistent `djconnect-raspberry-pi-XXXXXXXXXXXX`
  device ID.
- [ ] mDNS advertisement and pairing-info endpoint for HA discovery.
- [ ] Kiosk/full-screen now-playing wall display with album art, queue and DJ
  announcement.
- [ ] Touch/mouse/keyboard input model for playback, queue and settings.
- [ ] Capability reporting so HA does not require unsupported local audio,
  voice or DJ-response endpoints.
- [ ] Safe startup service, update flow and diagnostics for unattended displays.

## Killer Features

### General

- [ ] Natural music request to real playback: artist, track, album, playlist,
  mood and intent start on the right output.
- [ ] Personal DJ announcements based on resolved music metadata and the user's
  custom DJ prompt.
- [ ] One Home Assistant hub, many clients: ESP32, iOS, macOS, Raspberry Pi and
  website/docs share one pairing and protocol model.
- [ ] Privacy-first local control: no DJConnect account required for core use.
- [ ] Personal music memory for preferred artists, disliked results, common
  playlists and announcement style.

### HACS / Home Assistant

- [ ] Setup health check screen: Spotify Premium, OAuth, STT, TTS, playback
  device, mDNS and client reachability in one place.
- [ ] Built-in test wizard: STT test, TTS test, Spotify play test, DJ
  announcement test and pairing callback test.
- [ ] Routine hooks for Home Assistant automations when music is requested,
  playback starts, output changes or DJ announcement is generated.

### Website / Docs

- [ ] Guided "How to start" wizard with exact HACS repo, Spotify Premium
  requirement, HA Assist setup and client pairing steps.
- [ ] Release-aware update dashboard showing latest integration/client/firmware
  versions and compatibility.
- [ ] Public product demo with screenshots and short videos per client.

### ESP32 Firmware

- [ ] Always-listening "oke nabu" with local wake-word detection, LED/listening
  feedback and no cloud wake-word dependency.
- [ ] Room-aware hardware remote: one-touch output transfer to known rooms with
  remembered preferred output.
- [ ] Standard playlist button starts configured playlist, enables shuffle and
  disables repeat.
- [ ] Local visual personality: animated splash, LED ring states, game LED
  feedback, DJ-announcement ring and battery/charging states.
- [ ] Smart queue view with album art, per-item play, refresh and no fake
  duplicated current tracks.

### Apple Clients

- [ ] iOS widgets for quick playback, voice request and current track.
- [ ] macOS menu bar mini remote.
- [ ] Continuity-friendly handoff between Mac/iPhone and room devices.
- [ ] Voice/debug replay UI for the last WAV/STT/TTS response where HA exposes
  safe debug media.

### Raspberry Pi / Linux

- [ ] Shared now-playing wall: cover art, current track, next item, clock and DJ
  text for living-room display.
- [ ] Party display mode with guest QR code, queue and host moderation status.
- [ ] HyperPixel/kiosk themes optimized for wall-mounted displays.

## New Feature Ideas

### General Product Development

- [ ] Configurable standard playlist from HA, web portal, Apple client and
  device menu.
- [ ] DJ modes: concise, enthusiastic, radio host, kid-friendly, Dutch/English
  mixed or no-spoken-announcement.
- [ ] Party mode with locked simple controls, high brightness, persistent queue
  and limited settings access.
- [ ] Quiet hours to dim screens, lower cue volume and suppress non-critical
  sounds.
- [ ] Accessibility mode with bigger text, reduced animation and high contrast.
- [ ] Listening history with replay or add-to-playlist actions.
- [ ] Multi-room scene buttons for cooking, dinner, party, focus and sleep.

### HACS / Home Assistant

- [ ] Expanded Dutch/English local fallback parser for artist, track, album and
  playlist commands.
- [ ] Parsed intent debug attributes: media type, query, artist, title,
  playlist, market and Spotify result.
- [ ] Correction/follow-up commands: "niet deze", "de live versie", "meer zoals
  dit", "speel het album hiervan".
- [ ] Search result disambiguation when Spotify returns weak matches.
- [ ] Artist radio fallback when direct artist start is unavailable.
- [ ] Playlist name search across user playlists first, then public playlists.
- [ ] Repair issues for missing STT provider, missing TTS provider, missing
  Spotify Premium/device and invalid Client API URL.
- [ ] Optional persistent debug history with last N requests, redacted by
  default.
- [ ] More granular sensors for backend health, client health and Spotify auth
  state.
- [ ] HA automation blueprint pack for low battery, startup, pairing issues, DJ
  response errors, OTA notifications, parties and quiet hours.

### Website / Docs

- [ ] Dedicated troubleshooting pages for common support logs.
- [ ] Product architecture page explaining local-first design and where secrets
  live.
- [ ] Download/release page with board-specific firmware and app links.
- [ ] Support intake page that points users to redacted diagnostics.
- [ ] Roadmap page generated from this canonical file or a curated subset.

### ESP32 Firmware

- [ ] Favorite outputs pinned above live output discovery.
- [ ] Queue search/filter in the web portal for long queues.
- [ ] Album art cache controls: size, clear cache and age/count cap.
- [ ] One-click web UI to capture all screens and download a zip/contact sheet.
- [ ] Hardware self-test for display, buttons, encoder, speaker, mic, LED ring,
  WiFi and battery.
- [ ] Guided captive setup wizard for WiFi, HA pairing, language, brightness and
  speaker cue volume.
- [ ] Voice debug tools showing last WAV duration/size, STT text, TTS URL status
  and provider error body.
- [ ] ESP32-S3-BOX-3 optimized 320x240 layouts and touch affordances.
- [ ] Offline-friendly setup screen with QR/deeplink to HA integration
  instructions.
- [ ] Better battery, charging and OTA safety telemetry.

### Apple Clients

- [ ] Local demo mode metadata with clear "not connected to HA" boundaries.
- [ ] Client-side "can Home Assistant reach me?" indicator.
- [ ] Queue editor: reorder, remove or pin upcoming tracks when backend support
  is available.
- [ ] Rich album art popover and lock-screen/current-track affordances.
- [ ] Share diagnostics into GitHub issue template.
- [ ] Apple Watch request/cancel controls.

### Raspberry Pi / Linux

- [ ] Safe local update service and rollback instructions.
- [ ] Configurable display themes and burn-in protection.
- [ ] Touch-first queue and output switcher.
- [ ] Offline fallback screen with pairing recovery steps.
- [ ] Local logs/diagnostics page for kiosk deployments.

## Premium / Paid Feature Candidates

Premium ideas should add convenience, polish or optional hosted services while
keeping the core local/Home Assistant experience useful without payment.

### General Premium

- [ ] Advanced DJ personalities with curated style packs, multi-language voices,
  seasonal themes and custom prompt presets.
- [ ] Household profiles with per-user music preferences, language, family-safe
  rules and announcement tone.
- [ ] Smart music memory: favorite artists by room/time, request history,
  negative feedback and "more like this" suggestions.
- [ ] Advanced analytics: privacy-preserving request summaries, room usage,
  top artists and client usage.
- [ ] Remote support mode with time-limited, opt-in diagnostics sharing.
- [ ] Priority support, setup review or assisted onboarding.

### HACS / Home Assistant Premium

- [ ] Advanced automation recipe pack for parties, dinner, bedtime, wake-up
  music, quiet hours and scenes.
- [ ] Cloud-assisted diagnostics bundle with redacted setup health report.
- [ ] Hosted release/update dashboard for installed client versions and firmware
  readiness.

### Website Premium

- [ ] Account-backed optional theme/personality downloads.
- [ ] Support dashboard for premium users.
- [ ] Hosted guest request pages, only when privacy model is explicit.

### ESP32 Premium

- [ ] Premium LED/screen themes, animated idle screens and seasonal packs.
- [ ] Hardware bundle provisioning/support flow.
- [ ] Advanced local self-test and support bundle export.

### Apple Client Premium

- [ ] iOS widgets, Apple Watch controls and macOS menu bar extras.
- [ ] Premium queue editing tools and saved queue templates.
- [ ] Cross-device sync for DJ persona/profile settings.

### Raspberry Pi / Linux Premium

- [ ] Premium wall-display themes.
- [ ] Multi-display orchestration for synchronized rooms.
- [ ] Party-mode display with guest voting/moderation.

## Free vs Paid Guardrails

- Core pairing, local control, Home Assistant integration, basic PTT, basic DJ
  announcement and essential entities remain free.
- Paid features must not require DJConnect to collect Spotify credentials.
- Paid features should be optional enhancements, not mandatory infrastructure
  for local control.
- ESP32, Apple and Raspberry Pi/Linux clients remain usable with the free Home
  Assistant integration.
- Any hosted premium service needs a clear privacy model before implementation.
- Premium must not weaken the local-first default experience.

## Parking Lot

- [ ] Support future playback providers beyond Spotify through the generic HA
  playback command proxy.
- [ ] Local non-cloud LLM/DJ announcement generation if HA exposes a reliable
  local model path.
- [ ] Signed firmware manifests.
- [ ] Hardware bundle SKU planning.
- [ ] White-label hardware provisioning process.
- [ ] App Store/TestFlight production-readiness scope.
- [ ] Decide whether ESP32-S3-BOX-3 becomes fully supported hardware or remains
  experimental until display/speaker/mic/touch validation is complete.

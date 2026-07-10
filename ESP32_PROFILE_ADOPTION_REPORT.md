# ESP32 Profile Adoption Report

## Summary

Epic 3B Phase 5 adopts the DJConnect Profile Platform contract for the ESP32
firmware without turning the device into a Profile owner or Intelligence Client.

The ESP32 remains the canonical Voice / Control Client. It supplies hardware
request context and capability discovery; Home Assistant resolves Profiles and
owns all personal intelligence.

## What Changed

- Status payloads now include `request_source:"device_status"`, Profile
  Platform capability discovery and `contract_versions.profile_context:1`.
- Playback command payloads now include `request_source:"device_command"` and
  Profile Platform capability hints.
- Voice/PTT requests now identify their source as `voice` through the JSON text
  test path and the raw WAV upload headers.
- Local device info and pairing-info endpoints advertise Profile Platform
  support while explicitly reporting no profile CRUD or profile-selection UI.
- Firmware logic recognizes canonical Profile Platform errors including
  `profile_required`, `device_not_mapped`, `profile_backend_missing`,
  `profile_music_account_missing`, `profile_backend_account_mismatch`,
  `profile_access_denied`, `private_session_restriction` and
  `invalid_request_context`.
- Profile Platform errors are treated as Home Assistant/Profile setup guidance,
  not stale pairing. Pairing is still cleared only for pairing/auth failures.
- Native tests and the Node contract fixture assert that ESP32 requests remain
  device-scoped and do not carry profile-owned state.

## What Intentionally Did Not Change

- No Profile CRUD.
- No Profile selection UI.
- No Household management.
- No Music DNA, recommendations, mood or Ask DJ history storage.
- No local Profile resolver or resolver priority order.
- No speaker recognition.
- No direct OpenAI or direct Home Assistant Assist websocket path from firmware.
- No Apple, Windows, Pi, Android, cloud or feature-flag implementation.

## Firmware Responsibilities

- Hardware controls: buttons, encoder and wake behavior.
- Display, LED ring, speaker cues and microphone capture.
- WiFi, pairing, local runtime, battery policy and OTA.
- Device identity: `device_id`, `client_type:"esp32"`, firmware/model/local URL.
- Voice capture and upload to Home Assistant.
- Rendering and optional playback of Home Assistant-provided DJ responses.
- Runtime status and diagnostics that are hardware-focused.

## Backend Responsibilities

- DJConnect Profile resolution from request context.
- Profile mappings, Household behavior and fallback policy.
- Music DNA, recommendations, mood, Ask DJ history and response style.
- Music Backend routing, credentials, accounts and playback-zone decisions.
- Privacy/private-session policy.
- Assist/STT/TTS orchestration and generated DJ responses.
- Canonical Profile Platform errors and setup guidance.

## HA Voice Satellite Adoption Recommendations

- Reuse the same backend Profile Resolver rather than copying ESP32 behavior.
- Treat HA Voice Satellites as request sources/resolution signals unless
  registering them as full DJConnect Devices creates real product value.
- Resolve shared-room satellites to room, household, guest-safe or kids Profiles
  by explicit mapping, area/room mapping or fallback.
- Do not infer a personal Profile from a satellite without explicit mapping or a
  future speaker-identity signal.
- Keep satellite profile mapping and privacy policy in Home Assistant, not in
  firmware or clients.

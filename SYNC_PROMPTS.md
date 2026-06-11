# Sync Prompts

Use these prompts when handing work between the Home Assistant integration,
Apple app, and ESP firmware repos.

Canonical repo locations:

- Home Assistant integration: `pcvantol/djconnect`
- Apple app: `pcvantol/djconnect-app`
- ESP firmware: `pcvantol/djconnect-esp32`

## Home Assistant Integration

```text
Sync the DJConnect Home Assistant integration with the Apple app and ESP
client contracts.

Requirements:
- Treat iOS/macOS as app clients, not ESP hardware devices.
- Pair app clients through POST /api/djconnect/pair.
- Pair ESP clients through their local /api/device/pair flow after resolving
  /api/device/pairing-info and verifying the visible pair_code.
- Accept stable device_id, device_name, client_type, firmware, app_version,
  platform.
- Accept the app-generated code as pair_code, pairing_code, or pairing_token.
- Return a DJConnect bearer token on success. The current compatible field is
  device_token; bearer_token and token may also be returned.
- Return ha_local_url and language metadata during successful app pairing.
- Keep cloud/remote URLs out of Apple app runtime traffic; cloud URLs are only
  needed by Home Assistant-owned Spotify OAuth config flows.
- Return ha_version or ha_major_minor on status/command responses so Apple
  clients can enforce the matching major.minor contract.
- Apple clients host local /api/device/* app endpoints for HA -> app traffic,
  but must not implement ESP-only reboot or OTA routes.
- Persist client_type as ios, macos, or esp32. Do not reintroduce device_type.
- Authenticated status/command/voice routes must accept Authorization: Bearer
  plus X-DJConnect-Device-ID.
- Validate that client_type matches the device_id prefix/model family:
  ios -> djconnect-ios-*, macos -> djconnect-macos-*, esp32 -> ESP
  model-specific ids such as djconnect-lilygo-t-embed-s3-*.
- During app pairing, 401/403 code mismatch responses stop polling, keep the
  visible app code, and do not rotate device_id automatically.
- Create native HA entities for paired app clients when status is received.
- Return HTTP 426 version_mismatch when client and HA major.minor protocol
  versions do not match; do not treat this as stale auth.
- Return backend_unavailable as HTTP 200 success:false with
  backend_available:false, not as HTTP 503.
```

## Apple App

```text
Sync the DJConnect Apple app with the Home Assistant integration contract.

Requirements:
- Keep one stable device_id per app installation across normal launches.
- Reset Pairing clears the DJConnect bearer token, rotates the app pairing
  code, and creates a fresh device_id for a new setup.
- Pair by polling POST /api/djconnect/pair with pair_code, pairing_code, and
  pairing_token set to the same app-generated code.
- Store only the returned DJConnect bearer token in Keychain and persist
  ha_local_url, device_id, and client_type.
- Expose local /api/device/info, pairing-info, pair, command, dj_response, and
  forget routes for HA -> app traffic; do not expose ESP-only reboot/OTA.
- Send device_id, client_type, firmware, app_version, device_name, ha_local_url,
  and local_url on status payloads. Send device_id and client_type on command
  payloads. Always use the local Home Assistant URL for app-to-HA traffic.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping the
  token until explicit user reset.
- Treat 401/403 during unauthenticated pairing polling as code/setup mismatch:
  stop polling, keep the visible app code, and ask the user to re-enter it.
- Show first-run onboarding once per installation with the Home Assistant setup
  link and Spotify Premium requirement. Do not request Spotify credentials in
  the app.
- Detect likely unclean exits and offer only user-mediated crash reporting:
  copy redacted diagnostics or open a prefilled `pcvantol/djconnect` issue.
- Do not log bearer tokens, HA tokens, Spotify secrets, or audio URLs.
```

## ESP Firmware

```text
Sync the DJConnect ESP firmware with the Home Assistant integration contract.

Requirements:
- ESP clients are physical DJConnect devices and must use client_type esp32.
- Use model-specific device_id values, for example
  djconnect-lilygo-t-embed-s3-XXXXXXXXXXXX or
  djconnect-esp32-s3-box-3-XXXXXXXXXXXX.
- Do not accept or generate legacy djconnect-XXXXXXXXXXXX ids.
- Expose local ESP endpoints: GET /api/device/info,
  GET /api/device/pairing-info, POST /api/device/pair,
  POST /api/device/command, POST /api/device/dj_response,
  POST /api/device/forget, plus ESP-only reboot/OTA routes where supported.
- /api/device/pairing-info must return the real device_id, visible pair_code,
  client_type esp32, firmware, device_name, and reachable local_url.
- POST /api/device/pair must require device_token and ha_local_url.
- Persist only the DJConnect device bearer token and ha_local_url. Do not store
  Spotify OAuth/client secrets, Home Assistant long-lived tokens, or playback
  backend credentials.
- Always use ha_local_url for ESP -> HA status, command, and voice traffic.
  Never use Nabu Casa/cloud URLs for device runtime traffic.
- Send device_id, client_type esp32, firmware, ha_pairing_status, local_url,
  language, log_level, and current device settings in status payloads.
- Send raw WAV voice audio to POST /api/djconnect/voice with Authorization:
  Bearer <device_token> and X-DJConnect-Device-ID.
- Treat backend_unavailable and version_mismatch as recoverable without
  clearing pairing.
- Treat authenticated 401/403/404 as stale/setup recovery while keeping
  enough diagnostics to recover.
- Never log bearer tokens, HA tokens, Spotify secrets, WiFi passwords, or
  temporary audio URLs.
```

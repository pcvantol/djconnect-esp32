# ESP32 Management Summary

**Decision:** `ESP32_GOVERNANCE_ADOPTION_ESTABLISHED` pending review.

The firmware repository references central Version 2.2 governance and records
its PlatformIO, board, firmware-artifact, checksum and OTA release profile.
No firmware code changed.

## Dependabot Maintenance Status — 2026-07-27

**Decision:** `GO_PLATFORM_DEPENDABOT_MAINTENANCE_COMPLETE`.

The platform-wide Dependabot maintenance round is complete. This repository
merged [#38](https://github.com/pcvantol/djconnect-esp32/pull/38), updating ten
immutable GitHub Actions pins after exact-SHA Owner Authorization. Firmware,
PlatformIO and device behavior did not change.

Current GitHub evidence: zero open Dependabot security alerts and zero open
Dependabot pull requests. The canonical platform record is maintained in
`pcvantol/djconnect`.

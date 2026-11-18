# ESP32 Governance Adoption

This repository adopts AI-Native Engineering Operating System **2.2** from
`pcvantol/djconnect/docs/governance/PLATFORM_ARCHITECT_SYSTEM_INSTRUCTIONS.md`.
There are no local exceptions.

Definition of Done: synchronized clean main; implementation-reality check;
applicable native C++ tests, PlatformIO board build and release-script checks;
firmware binary/version/checksum/OTA validation; updated rolling records and
immutable history; one reviewable PR.

Release profile: LilyGO-target firmware binaries with target metadata,
checksums and OTA/GitHub Release distribution through `djconnect-firmware`.
Docker, Cloudflare Workers and HACS are not firmware deployment targets.

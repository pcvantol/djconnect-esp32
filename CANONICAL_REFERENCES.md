# DJConnect Canonical References

Status: repository-local canonical reference map

Repository: `pcvantol/djconnect-esp32`

This repository extends the DJConnect Platform Foundation. It does not redefine
it.

## Platform Foundation

- Canonical repository: `pcvantol/djconnect`
- Canonical document: `FOUNDATION_INDEX.md`
- Local responsibility: follow the platform foundation while implementing this
  repository's owned surface.
- May this repository modify it: no, except by making an explicit coordinated
  change in `pcvantol/djconnect`.

Canonical foundation documents include:

- `FOUNDATION_INDEX.md`
- `DJCONNECT_CONSTITUTION.md`
- `PRODUCT_VISION.md`
- `DESIGN_FOUNDATION_VERSION.md`
- `DESIGN_PRINCIPLES.md`
- `ARCHITECTURE_PRINCIPLES.md`
- `DOMAIN_MODEL.md`
- `PLATFORM_PRINCIPLES.md`
- `CLIENT_CAPABILITY_MATRIX.md`
- `LOCALIZATION_STANDARD.md`
- `PRODUCT_LANGUAGE.md`
- `PLATFORM_GOVERNANCE.md`
- `PLATFORM_QUALITY_STANDARD.md`
- `PLATFORM_BACKLOG.md`
- `REPOSITORY_OWNERSHIP.md`
- `ADR_INDEX.md`
- `CI_CD_RELEASE_GOVERNANCE.md`

## Verification Foundation

- Canonical repository: `pcvantol/djconnect`
- Canonical documents: `BOOTSTRAP_CODEX_VERIFICATION.md`,
  `docs/verification/00_VERIFICATION_VISION.md`,
  `docs/verification/01_VERIFICATION_ARCHITECTURE.md` and `PROMPT_INDEX.md`
- Local responsibility: provide repository-owned targets, artifacts or evidence
  when a canonical verification phase asks for them.
- May this repository modify it: no, except through an explicit canonical
  verification phase in `pcvantol/djconnect`.

## Meta Engineering Foundation

- Canonical repository: `pcvantol/djconnect`
- Canonical document: `docs/meta/README.md`
- Local responsibility: follow the repository-first engineering memory model,
  AI collaboration model and phase completion protocol.
- May this repository modify it: no, except by making an explicit coordinated
  change in `pcvantol/djconnect`.

## Platform Prompt Index

- Canonical repository: `pcvantol/djconnect`
- Canonical document: `PROMPT_INDEX.md`
- Local responsibility: keep only repository-local phase state in this
  repository's `PROMPT_INDEX.md`.
- May this repository modify the platform roadmap: no.

## Repository Ownership

- Canonical repository: `pcvantol/djconnect`
- Canonical document: `REPOSITORY_OWNERSHIP.md`
- Local responsibility: stay within this repository's ownership boundary.
- May this repository modify it: no, except by making an explicit coordinated
  change in `pcvantol/djconnect`.

## Local Technical Design Ownership

- Canonical repository: `pcvantol/djconnect-esp32` for repository-specific technical
  design, implementation, build, release and test details.
- Local responsibility: maintain implementation reality for ESP32 firmware implementation, constrained device UI, local device API, pairing, mDNS, OTA client behavior and firmware-local rendering.
- May this repository modify it: yes, within its ownership boundary.

## Clean-Session Entrypoints

- Repository work: read `BOOTSTRAP_CODEX_SESSION.md`.
- Repository state: read `REPOSITORY_STATUS.md`.
- Repository-local phase state: read `PROMPT_INDEX.md`.
- Platform or verification work: read the canonical documents in
  `pcvantol/djconnect` referenced above.

`CHAT_BOOTSTRAP.md` is deprecated and is not a canonical entrypoint.

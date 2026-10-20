# DJConnect Repository Prompt Index

Brand V2 source Finalization — 20 October 2026.

## Repository Status

Predecessor: [PR #63 — Brand V2 firmware and portal source](https://github.com/pcvantol/djconnect-esp32/pull/63), protected merge `94759d61ca0cedf5bf40e7a1277e8ed7b5465561`.
Reviewed/owner-authorized source candidate: `6e9884a4382abe3a83287afd066e5f64bf441054`.
Complete candidate/merge Git trees equal: `eba280da558a5ab3236f6ad046cbc9cbc48695ed`.
All predecessor technical CI and exact-SHA Owner Authorization passed. Firmware build,
native/release/asset/HA-contract checks and CodeQL are source/software evidence.
This firmware repository has no HACS integration release outcome; no release/tag/OTA
or hardware rollout was performed. Post-merge main CI is separate and not asserted here.
Canonical local `main == origin/main`: **false**, existing editor WIP is retained;
the isolated Finalization branch starts at the exact verified remote main above.
Stale-local-branch result: implementation branch retained pending safe cleanup;
no unrelated branches were removed.

## Management Summary

Completion Report: [Brand V2 source Finalization history](docs/history/prompts/2026-10-20-esp32-brand-v2-finalization.md).

Brand V2 source is delivered. Core [#1224](https://github.com/pcvantol/djconnect/pull/1224)
is protected merged at `7e14e5970b3f68d33d243ae6bab2b281fad59bb2`; all 28 brand
blobs and six imported files match the accepted source. ESP32 owns only local
display/portal adaptation. Independent read-only source/limited visual review
passed without findings. Twenty portal header fixtures and decoded RGB565 previews
do not establish captive-page, complete TFT-screen or physical acceptance.

Most recent merged source outcomes, newest first at this readback:

- [#63](https://github.com/pcvantol/djconnect-esp32/pull/63), `94759d61ca0cedf5bf40e7a1277e8ed7b5465561`:
  pinned glass/headset brand, small-size variants, theme-safe compositing and portal
  icons; all technical CI/review/authorization passed; physical/installed acceptance open.
- [#61](https://github.com/pcvantol/djconnect-esp32/pull/61), `9523267d04b5e4eab2d2bfad56c5e2a38d1e0b03`:
  immutable GitHub Actions dependency update; no firmware behavior or physical claim.
  This is the verified Brand V2 admission base, not a newly executed maintenance task.

## Roadmap Position

Existing DJConnect platform generation/phase remain canonical in
`pcvantol/djconnect/PRODUCT_ROADMAP.md`; no new generation identifier is invented.
Active repository increment: **Brand V2 consumer Finalization**, governance-only.
Owner-selected source slice #62/#63 is merged. The overall integral rebrand and
physical consumer acceptance are not declared complete. Immutable prompt history
and canonical platform roadmap are preserved.

## Rolling Horizon (Next 5 Planned)

No subsequent ESP32 execution item is authorized in this Finalization. Do not fill
five slots with recommendations or treat backlog order as new dispatch. The existing
canonical `SETTINGS::ESP32::ADOPTION` record remains Planned/NOT_ISSUED; its prior
preparation does not select firmware implementation. No automatic successor.
Source: `pcvantol/djconnect/docs/roadmap/v1/lanes/DJC-ESP32.md` and owning register
[#58](https://github.com/pcvantol/djconnect-esp32/issues/58).

## Blocked Items

- Finalization: requires its own exact candidate CI/review/Owner Authorization and
  protected merge; source approval is not transferred to a different Finalization SHA.
- Workspace cleanup: original canonical `.vscode/extensions.json` WIP and lagging
  local main are preserved. Required owner disposition/readiness evidence is absent;
  no reset, stash, forced deletion or clean is authorized to conceal this condition.
- Physical/installed Brand V2 acceptance: no selected LilyGO/HA target or installed
  artifact receipt. Requires explicit target admission and actual screen/color checks.

## Deferred Items

Firmware publication, tag, OTA, physical flashing and whole-platform rebrand closure
remain outside this source transaction. No other lane, runtime contract, provider,
Persona, playback, account or roadmap work is changed.

## Repository State

Before protected merge: **MERGED_UNRECONCILED / FINALIZATION_REVIEWABLE**.
Upon this Finalization protected merge: **MERGED_RECONCILED** for predecessor #63.
Workspace State remains **NOT_READY** until its separate cleanup conditions are met.
Repository reconciliation is not installed or physical product acceptance.

## Workspace State

**NOT_READY**. Existing canonical editor WIP and synchronization/branch cleanup
must be resolved safely by their owner before `WORKSPACE_READY` can be asserted.
No next capability is admitted by this document.

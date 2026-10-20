# Brand V2 ESP32 consumer candidate

Owning work: [ESP32 #62](https://github.com/pcvantol/djconnect-esp32/issues/62).
Source base: `9523267d04b5e4eab2d2bfad56c5e2a38d1e0b03`.

## Exact source binding

- Producer: Core `883cb9f3ff9de8ec7f5ae4c5f04f852e0d8bd866`, [PR #1224](https://github.com/pcvantol/djconnect/pull/1224).
- Producer manifest SHA256: `73cc408e10d88aa1fb8a1be2783b9c577aec398ce3dcd54c2f997ad1425984d2`.
- Published candidate archive SHA256: `aa51cf9bd417a2009cdd3bed037f46b5424ac85869fbe3e9fbb2657b4754f977` (producer-reported, archive not imported here).
- Original approved master SHA256: `f05fcdf4602d9c63add7799469c64060a8f13e5efa66ad6c7b762fb8273ec89f` (producer-owned original retained there).
- Actual import: four committed PNGs (`mark-16/32/40/256.png`) from that exact commit, individually SHA256-verified against the manifest. Manifest itself is SHA256-verified. No mutable-main import or AI redraw.
- `assets/brand-v2/consumer-manifest.json` binds every derived output. Its explicit candidate status remains until protected producer acceptance is established.

### Protected producer reconciliation — 20 October 2026

Core #1224 is protected merged as
`7e14e5970b3f68d33d243ae6bab2b281fad59bb2`. All 28 Brand V2 file blobs are
identical to the originally imported producer candidate. Each of the six actual
imports (four PNGs, manifest and producer usage README) independently matches its
accepted Git blob and recorded SHA256. See [machine-readable receipt](brand-v2/producer-acceptance.json).
The original consumer manifest retains its historical review-candidate wording;
this later receipt establishes producer-source reconciliation only. Independent
consumer review, protected delivery and physical acceptance remain distinct.

## Asset-reference census

| Existing surface | New treatment |
| --- | --- |
| `DisplayManager::drawDJConnectIcon`, boot/confirmation/pairing/About/DJ overlay | Glass at larger sizes; headset-only at 40px or below. RGB565 composed on black or white, with light-mode inversion compensation. |
| `WebPortal` corporate header, 28 CSS px | Exact canonical 32px headset PNG via `/icon-32.png`. |
| `DJConnectApp` captive setup header, 32 CSS px | Same micro asset and local route. |
| Both portals' favicon routes | ICO with exact canonical 16/32/40px PNG frames. |
| Existing PWA/touch aliases | Transparent 192px glass mark; route behavior retained. |
| Web app manifest | Versioned existing 192px route to invalidate old cached artwork; remove nonexistent root 512px entry and unqualified maskable claim. Compatibility alias routes remain. |
| TFT and portal brand line | Invariant `Your music. Your DJ.`; controls retain five-language translation. |
| `assets/esp_display` preview PNGs | Regenerated glass, transparent outside the rim. |
| Unused `assets/ha_integration`, `assets/website`, duplicate embedded exports and portrait splash | Retired stale examples; originals remain in Git history. These were not runtime assets or authoritative cross-repository exports. |
| Spotify/music/playback symbols, Persona/avatar, album/artist art | Retained; not corporate substitution targets. |

## Software evidence

- Native C++ tests, HA contract smoke and portal language completeness: PASS. Existing HTTP contract e2e: PASS, eight local fixture routes.
- Offline pinned generator `--check` and four asset-format tests: PASS.
- Actual embedded HTML in Chromium with local fixture APIs: 20 header/visible-viewport cases, five languages × two themes × mobile/desktop, zero JavaScript page errors. Micro image decoded at 32px. Screenshots cover the top 330px, not complete portal navigation or captive setup. See [browser records](brand-v2/browser-results.json) and adjacent PNGs.
- Actual PROGMEM RGB565 sample previews: [bitmap sheet](brand-v2/framebuffer-bitmaps.png), reproducible with `scripts/preview_brand_v2.py`. This is a decoded bitmap/draw-size preview, not a physical capture. The second row applies existing panel inversion to the compensated light-mode array; the mark retains its blue/purple colors.
- Final LilyGO `t_embed_cc1101` build including light compensation, overlay-cache invalidation and versioned PWA manifest: PASS, 21.13 seconds; RAM 125,468/327,680 bytes (38.3%), flash 3,069,848/6,553,600 bytes (46.8%). Existing legacy-I2S dependency warnings remain.
- Release-test branding checks now bind the producer manifest, four source PNGs and generated-output hashes instead of retired legacy examples. Full dry-run acceptance is checked by CI; the existing `release.sh` root guard requires a `.git` directory and rejects this managed worktree's `.git` file, so no local full dry-run PASS is claimed and no release-tool behavior was changed.

Exact candidate `6a39e4dc3913df42a85d66c7e10228cc9cd5be5b` now has completed
successful firmware/native/HTTP/asset/release/CodeQL/Gitleaks/governance/projection
and TDE checks on GitHub. Three conditional cleanup jobs were skipped.
The separate Owner Authorization status requires HIGH_RISK authorization for the
exact final candidate; CI success is not that authorization.

Independent read-only reviewer-agent: source and limited visual review PASS on
the same exact candidate, no actionable findings. The generator and four asset
tests were independently rerun. [Review scope and limitations](brand-v2/independent-review.md).

## Remaining gates

Core #1224 was open/unmerged at admission; its producer-source gate is now
reconciled above. Independent source/limited visual review has passed. Exact
final-candidate CI/Owner Authorization and protected delivery remain separate
gates; the later documentation-only reconciliation gets its own reviewer readback.

No physical LilyGO was selected or used. Physical bezel/color/legibility,
light-panel inversion and actual boot/setup/About/pairing screens remain OPEN.
No release version, tag, OTA, deployment, credential, API protocol, audio path or
physical-device setting is changed. Existing local work in the original checkout
is preserved.

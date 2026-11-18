# DJConnect Brand V2

Owner-approved glass-headphones identity, [Core #1223](https://github.com/pcvantol/djconnect/issues/1223).
Exact English tagline: **Your music. Your DJ.**

## Source and semantic variants

`source/DJConnect-BrandV2-APPROVED-ORIGINAL-1254.png` is the exact original
1254×1254 opaque RGB master, not a regenerated interpretation. SHA256:
`f05fcdf4602d9c63add7799469c64060a8f13e5efa66ad6c7b762fb8273ec89f`.
The received ZIP SHA256 is in `manifest.json`; its original README/manifest
remain in `source/`. The owner's subsequent correction defines the **glass rim as
the asset boundary**, transparent outside, and requests **headset only at16–40px**.
The original source remains immutable; deterministic editable alpha contours in
`tools/branding/contours.json` derive `glass-master.png` and `headset-master.png`
without redrawing or altering interior RGB pixels. These derived sources have
their own hashes, distinct from the original master. Two automatic imagegen
extractions were rejected for stray outer pixels; neither is a distributed source.
The approved geometry and original RGB are retained by the contour recipe instead.
The master has no embedded ICC
profile; derived RGB/RGBA graphics use those same channel values, intended for sRGB display.

| Variant | Files | Intended use |
| --- | --- | --- |
| Micro headset | `mark-{16,20,24,32,40}.png`, `headset.svg`, `headset-master.png` | Transparent headset/central symbol, no glass button; maximally fit within the square |
| Glass mark | `mark-{64,128,256,512,1024}.png`, `mark.svg`, `glass-master.png` | Glass button, transparent outside its rim; no name/tagline inside |
| Primary | `wordmark-primary-{light,dark}.{png,svg}` | 2048×512 mark + DJConnect + exact English tagline; use at512px wide or larger |
| Compact | `wordmark-compact-{light,dark}.{png,svg}` | 1728×448 mark + DJConnect; no tagline; use below512px wide |
| Preview | `preview.png` | Actual16–128px icons and representative wordmarks on white/navy/colour; source review only |

`light` means dark lettering for a light background; `dark` means light lettering
for a dark background. Wordmark canvases and icons are transparent RGBA. The glass
interior remains opaque; the exterior navy rectangle is removed. The derived
glass/headset contours are tightly bounded and fitted without aspect distortion.
No recolouring or newly drawn headset is used. Platform-specific native exports
may need opaque compositing/masks; that is explicit consumer work, not an excuse
to put an opaque navy square into these canonical transparent assets.
The SVGs are **raster-backed image marks with outlined Inter typography**, not
traced/vector headphones. Every SVG is self-contained, embeds its derived alpha source
and has no external font/image dependency. The original raster and editable alpha
recipe are supplied. Core dimensions do not qualify native Apple icon admission.

HA `brand/icon.png` / `icon@2x.png` are256/512px square. HA `logo.png` is a
1024×256 primary wordmark; `logo@2x.png` is2048×512. Dark wordmarks use HA's
`dark_logo` names. No separate dark icon is necessary: the mark is identical on
both themes. All four pre-existing HA/legacy brand directories receive identical
outputs. Canonical SVG aliases and `assets/djconnect_icon.png` remain valid.
The size-named `djconnect-logo-512x256.png` compatibility alias fits the undistorted
512×128 wordmark centered in its original512×256 transparent canvas; prefer the
tightly bounded primary/compact family for new consumers.

These HA dimensions and theme naming follow the
[Home Assistant brand image specification](https://github.com/home-assistant/brands/blob/master/README.md#image-specification).
No upstream HA brands publication is performed by generation.

## Offline deterministic generation

Use Python3.14 and a separate build environment:

```sh
python3.14 -m venv /tmp/djc-brand-build
/tmp/djc-brand-build/bin/python -m pip install -r tools/branding/requirements.txt
/tmp/djc-brand-build/bin/python scripts/build_brand.py
/tmp/djc-brand-build/bin/python scripts/build_vibecast.py --output /tmp/djc-vibecast-check --write-local --write-display --check-local
/tmp/djc-brand-build/bin/python scripts/build_brand.py --check
```

Final generation itself is offline and uses no AI or system fonts. Pillow13.0.0 /
fonttools4.67.0 are build-only pins, not HA dependencies. The unmodified Inter4.1
font and its OFL license/provenance are in `tools/branding/fonts/`.
`manifest.json` records source/recipe/font/output SHA256, PNG dimensions and alpha,
variant semantics and tool versions. No timestamp, working directory or mutable
branch name is part of asset generation. CI regenerates and compares all bytes.
PNG framing and Sub row filtering are explicit; classic zlib fixed-Huffman
encoding avoids Pillow's architecture-dependent zlib-ng stream selection. This
changes compressed bytes only, not decoded artwork. Strict cross-host comparison
remains a gate; it is not replaced with a visual-tolerance comparison.

The VibeCast template uses `@@BRAND_ICON@@`; its builder embeds packaged
`brand/icon.png` for local, display and static hosts and authenticates that input
in each source manifest. Builder identity1.1.1 reflects the changed source set;
wire/renderer contracts and integration release version remain unchanged.
Do not paste a new independent base64 image into any generated page.

## Exact producer handoff

After committing the reviewed candidate, export only committed blobs:

```sh
python3 scripts/package_brand.py --revision <exact-40-character-producer-commit> --output /tmp/DJConnect-BrandV2-candidate.zip
```

The deterministic ZIP contains these assets, exact master, source/output manifests,
generator, font/license, instructions and `handoff.json` with **producer commit**
and every included hash. The companion receipt gives archive SHA256. Export fails
if a committed output, source or recipe differs from its manifest; uncommitted WIP
is never silently exported. The pack is explicitly a **candidate**, not acceptance.
Consumers must pin the reviewed protected Core producer commit and archive/manifest
hashes, then explicitly record acceptance before rollout. Do not pin `main` URLs
or the earlier incomplete icon-only ZIP as an accepted native/Website handoff.

Apple [#138](https://github.com/pcvantol/djconnect-app/issues/138) owns actual native
catalogs, platform masks/dark/tinted handling,16–40px native legibility, wrappers,
accessibility and five-language tagline presentation. Website
[#62](https://github.com/pcvantol/djconnect-website/issues/62) owns favicon/OG/site
wordmark and published proof. Neither consumer repository is mutated by this recipe.
The source contact sheet does not qualify installed HA, Apple native or Website UI.
Protected Core review/CI, exact-main reconciliation, consumer acceptance and any
separately authorized deployment remain distinct gates. No integration version,
release, installed state or runtime behavior changes here.

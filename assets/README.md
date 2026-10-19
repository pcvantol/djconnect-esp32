# DJConnect firmware branding

Brand V2 candidate source is pinned in `brand-v2/producer-manifest.json` to Core
commit `883cb9f3ff9de8ec7f5ae4c5f04f852e0d8bd866`. Producer usage rules are in
`brand-v2/PRODUCER_USAGE.md`. This import does not assert protected producer or
physical firmware acceptance. The exact invariant brand line is
**Your music. Your DJ.**, including when controls use another language.

`mark-16/32/40.png` are unmodified transparent headset-only micro-icons;
`mark-256.png` is the unmodified glass mark, transparent outside the rim.
The original opaque master stays with the canonical producer and is never
substituted for a transparent production variant.

Generate and verify offline with Python 3.14:

```sh
python3.14 -m venv /tmp/esp32-brand-build
/tmp/esp32-brand-build/bin/python -m pip install -r scripts/brand-requirements.txt
/tmp/esp32-brand-build/bin/python scripts/build_brand_v2.py
/tmp/esp32-brand-build/bin/python scripts/build_brand_v2.py --check
/tmp/esp32-brand-build/bin/python test/native/test_brand_v2.py
```

`esp_display/` contains transparent glass previews. Actual PROGMEM RGB565
arrays composite alpha over black for dark mode and over white with panel
inversion compensation for light mode. Draws at
40px or less select the headset array; larger draws select glass. No runtime
alpha decoder, network image request or extra heap buffer is added. The device
light theme still uses whole-panel inversion; only the corporate mark compensates
for that policy to retain its blue/purple colors.

The portal's 28px header uses the canonical 32px headset PNG. ICO entries contain
the exact canonical 16/32/40px PNGs. The existing 192px touch/PWA route uses a
transparent glass mark. `consumer-manifest.json` records generated hashes.
Explicit PNG framing/stored DEFLATE avoids architecture-specific encoder bytes.

Unused old HA/website example packs, portrait splash and duplicate embedded
display exports are retired. HA/public website branding belongs to its own
repositories. Semantic music/playback glyphs, Persona/avatar and album art stay
unchanged.

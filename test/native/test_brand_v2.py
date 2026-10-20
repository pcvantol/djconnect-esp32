#!/usr/bin/env python3
"""Pinned producer and encoded firmware format verification, no device claim."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("brand", ROOT / "scripts/build_brand_v2.py")
brand = importlib.util.module_from_spec(spec)
spec.loader.exec_module(brand)


def embedded(name, light=False):
    text = (ROOT / "include/assets" / name).read_text()
    arrays = re.findall(r"\[\] PROGMEM = \{(.*?)\};", text, re.S)
    return bytes(int(n, 16) for n in re.findall(r"0x([0-9a-f]{2})", arrays[int(light)]))


class BrandTests(unittest.TestCase):
    def test_generated_bytes_and_receipt(self):
        for name, expected in brand.outputs().items():
            self.assertEqual((ROOT / name).read_bytes(), expected, name)
        receipt = json.loads((ROOT / "assets/brand-v2/consumer-manifest.json").read_text())
        self.assertEqual(receipt["producer_commit"], "883cb9f3ff9de8ec7f5ae4c5f04f852e0d8bd866")
        self.assertIn("NOT_PRODUCER_ACCEPTANCE", receipt["qualification"])

    def test_display_marks_use_recolored_headset_master(self):
        source = (ROOT / "assets/brand-v2/headset-master.png").read_bytes()
        self.assertEqual(hashlib.sha256(source).hexdigest(),
                         "21a6273f273936574a7301e7d91c2b0513da66fe17534c258d67c675c0294adc")
        original = Image.open(io.BytesIO(source)).convert("RGBA")
        self.assertEqual(original.size, (754, 754))
        palette = (ROOT / "assets/brand-v2/headset-purple-navy-magenta.png").read_bytes()
        self.assertEqual(hashlib.sha256(palette).hexdigest(),
                         "f69eb72b882f08c0dacbd3efa7e83165a56a01763aaa18e48557c03b65e11322")
        master = Image.open(io.BytesIO(palette)).convert("RGBA")
        self.assertEqual(master.size, (1254, 1254))
        for size in (64, 80, 96, 128, 160, 170):
            actual = Image.open(ROOT / f"assets/esp_display/djconnect_icon_{size}x{size}.png")
            self.assertEqual(actual.tobytes(),
                             master.resize((size, size), Image.Resampling.LANCZOS).tobytes())
        # Independently bind the actual large RGB565 array to the headset pixels.
        rgba = master.resize((160, 160), Image.Resampling.LANCZOS)
        rgb = Image.alpha_composite(Image.new("RGBA", rgba.size, (0, 0, 0, 255)), rgba).convert("RGB")
        expected = bytearray()
        for r, g, b in rgb.get_flattened_data():
            expected.extend((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, "big"))
        self.assertEqual(embedded("DJConnectIcon160.h"), bytes(expected))
        micro = master.resize((40, 40), Image.Resampling.LANCZOS)
        rgb = Image.alpha_composite(Image.new("RGBA", micro.size, (0, 0, 0, 255)), micro).convert("RGB")
        expected = b"".join((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, "big")
                            for r, g, b in rgb.get_flattened_data())
        self.assertEqual(embedded("DJConnectMicro40.h"), expected)

    def test_ico_contains_exact_micro_sources(self):
        data = embedded("djconnect_favicon_ico.h")
        self.assertEqual(struct.unpack_from("<HHH", data), (0, 1, 3))
        for i, size in enumerate((16, 32, 40)):
            entry = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
            self.assertEqual(entry[:2], (size, size))
            length, offset = entry[-2:]
            self.assertEqual(data[offset:offset + length], (ROOT / f"assets/brand-v2/mark-{size}.png").read_bytes())

    def test_web_alpha_and_micro_header(self):
        self.assertEqual(embedded("djconnect_micro_32_png.h"), (ROOT / "assets/brand-v2/mark-32.png").read_bytes())
        image = Image.open(io.BytesIO(embedded("djconnect_icon_192_png.h")))
        self.assertEqual(image.size, (192, 192))
        self.assertEqual(image.getpixel((0, 0))[3], 0)
        portal = (ROOT / "src/WebPortal.cpp").read_text()
        self.assertIn('class="brand-icon" src="/icon-32.png?v=4"', portal)
        self.assertNotIn("Muziekbediening met karakter", portal)
        manifest = json.loads(embedded("djconnect_site_webmanifest.h"))
        self.assertEqual(manifest["icons"], [{"src": "/icon-192.png?v=4", "sizes": "192x192",
                                            "type": "image/png", "purpose": "any"}])
        self.assertEqual(manifest["description"], "Your music. Your DJ.")

    def test_framebuffer_transparency_is_composited(self):
        for name, size in (("DJConnectIcon160.h", 160), ("DJConnectMicro40.h", 40)):
            data = embedded(name)
            self.assertEqual(len(data), size * size * 2)
            self.assertEqual(data[:2], b"\0\0")
            self.assertNotEqual(data[(size * size // 2) * 2:], b"\0" * (size * size))
            light = embedded(name, light=True)
            self.assertEqual(len(light), len(data))
            self.assertEqual(light[:2], b"\0\0")  # Hardware inversion makes the transparent corner white.
            # Independently verify white-background alpha compositing and
            # panel inversion, including this asset's partially opaque pixels.
            reference = Image.open(ROOT / "assets/brand-v2/headset-purple-navy-magenta.png").convert("RGBA")
            reference = reference.resize((size, size), Image.Resampling.LANCZOS)
            white = Image.new("RGBA", reference.size, (255, 255, 255, 255))
            rgb = Image.alpha_composite(white, reference).convert("RGB")
            expected = b"".join(((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) ^ 65535).to_bytes(2, "big")
                                for r, g, b in rgb.get_flattened_data())
            self.assertEqual(light, expected)
        display = (ROOT / "src/DisplayManager.cpp").read_text()
        self.assertIn("const bool micro = size <= 40;", display)
        self.assertNotIn("if (color != TFT_BLACK)", display)


if __name__ == "__main__":
    unittest.main()

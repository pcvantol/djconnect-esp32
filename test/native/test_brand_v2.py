#!/usr/bin/env python3
"""Pinned producer and encoded firmware format verification, no device claim."""
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
            # The opaque neon center retains its color under panel inversion.
            index = (size // 2 * size + size // 2) * 2
            self.assertEqual(int.from_bytes(light[index:index + 2], "big") ^ 65535,
                             int.from_bytes(data[index:index + 2], "big"))
        display = (ROOT / "src/DisplayManager.cpp").read_text()
        self.assertIn("const bool micro = size <= 40;", display)
        self.assertNotIn("if (color != TFT_BLACK)", display)


if __name__ == "__main__":
    unittest.main()

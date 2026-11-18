#!/usr/bin/env python3
"""Decode the actual PROGMEM arrays at firmware draw sizes; no hardware claim."""
from pathlib import Path
import re
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
sheet = Image.new("RGB", (800, 290), "#101522")
labels = ImageDraw.Draw(sheet)
labels.text((14, 8), "RGB565 / draw-size software preview - NOT a physical device capture", fill="white")
images = {}
for size, name in ((160, "DJConnectIcon160.h"), (40, "DJConnectMicro40.h")):
    text = (ROOT / "include/assets" / name).read_text()
    for light, array in enumerate(re.findall(r"\[\] PROGMEM = \{(.*?)\};", text, re.S)):
        data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", array))
        rgb = []
        for i in range(0, len(data), 2):
            color = int.from_bytes(data[i:i + 2], "big")
            rgb.append((((color >> 11) & 31) * 255 // 31,
                        ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31))
        image = Image.new("RGB", (size, size))
        image.putdata(rgb)
        images[size, light] = image
for light in (0, 1):
    y = 35 + light * 120
    labels.text((14, y), "light / compensated" if light else "dark", fill="white")
    for column, size in enumerate((32, 38, 44, 54, 64, 78, 88)):
        source = images[40 if size <= 40 else 160, light]
        image = Image.new("RGB", (size, size))
        image.putdata([source.getpixel((x * source.width // size, z * source.height // size))
                       for z in range(size) for x in range(size)])
        if light:
            image = ImageOps.invert(image)
        x = 120 + column * 94
        sheet.paste(image, (x, y + 18))
        labels.text((x, y + 108), f"{size}px", fill="white")
output = ROOT / "docs/verification/brand-v2/framebuffer-bitmaps.png"
output.parent.mkdir(parents=True, exist_ok=True)
sheet.save(output)
print(output)

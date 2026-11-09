#!/usr/bin/env python3
"""grid_sheet.py OUT.png W FILE...: the images in a 2-column grid, each scaled to width W, the file stem on each."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

out, w, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
ims = [Image.open(f).convert("RGB") for f in files]
ims = [i.resize((w, int(i.height * w / i.width))) for i in ims]
h = ims[0].height
sheet = Image.new("RGB", (2 * w, h * ((len(ims) + 1) // 2)))
for k, (f, i) in enumerate(zip(files, ims)):
    x, y = (k % 2) * w, (k // 2) * h
    sheet.paste(i, (x, y))
    ImageDraw.Draw(sheet).text((x + 4, y + 4), Path(f).stem, fill=(0, 255, 0))
sheet.save(out)
print(out)

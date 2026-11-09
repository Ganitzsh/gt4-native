#!/usr/bin/env python3
"""stack.py DIR N: the PNGs of DIR (sorted) stacked N per sheet, each with its file name written on it, as
DIR/sheet_K.png."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d, n = Path(sys.argv[1]), int(sys.argv[2])
files = sorted(p for p in d.glob("*.png") if not p.name.startswith("sheet_"))
for k in range(0, len(files), n):
    ims = [Image.open(f).convert("RGB") for f in files[k:k + n]]
    w = max(i.width for i in ims)
    sheet = Image.new("RGB", (w, sum(i.height for i in ims)))
    y = 0
    for f, i in zip(files[k:k + n], ims):
        sheet.paste(i, (0, y))
        ImageDraw.Draw(sheet).text((4, y + 4), f.stem, fill=(0, 255, 0))
        y += i.height
    sheet.save(d / f"sheet_{k // n + 1}.png")
    print(d / f"sheet_{k // n + 1}.png", [f.stem for f in files[k:k + n]])

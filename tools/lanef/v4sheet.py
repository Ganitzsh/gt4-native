#!/usr/bin/env python3
"""v4sheet.py DIR OUT FRAMES...: the hunt's images (f#####.png: base | skipped | changes) for the given frames (or
'every:N' for every Nth saved image, 'range:A-B' for those in a range), stacked into sheets of 6 as OUT_k.png, each
scaled to 1440 wide, frame number written on it."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

d, out = Path(sys.argv[1]), sys.argv[2]
files = sorted(d.glob("f*.png"))
pick = []
for a in sys.argv[3:]:
    if a.startswith("every:"):
        pick += files[::int(a[6:])]
    elif a.startswith("range:"):
        lo, hi = map(int, a[6:].split("-"))
        pick += [f for f in files if lo <= int(f.stem[1:]) <= hi]
    else:
        pick += [f for f in files if int(f.stem[1:]) == int(a)]
pick = sorted(set(pick))
for k in range(0, len(pick), 6):
    ims = [Image.open(f).convert("RGB") for f in pick[k:k + 6]]
    ims = [i.resize((1440, int(i.height * 1440 / i.width))) for i in ims]
    sheet = Image.new("RGB", (1440, sum(i.height for i in ims)))
    y = 0
    for f, i in zip(pick[k:k + 6], ims):
        sheet.paste(i, (0, y))
        ImageDraw.Draw(sheet).text((4, y + 4), f.stem, fill=(0, 255, 0))
        y += i.height
    sheet.save(f"{out}_{k // 6 + 1}.png")
    print(f"{out}_{k // 6 + 1}.png", [f.stem for f in pick[k:k + 6]])

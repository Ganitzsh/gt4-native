#!/usr/bin/env python3
"""n3_edgeloss.py DIR [N]: ranks a hunt's saved frames (f#####.png: base | skipped | changes) by edge loss: per pixel,
the luminance gradient magnitude in base minus that in the skipped frame, where positive and over 12, summed over the
frame without the HUD. Removing a painted marking takes its sharp edges away; removing soft lamp light barely changes
the gradients. Prints the top N and writes them as sheets (DIR/edge_k.png, 6 per sheet, base | skipped only)."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

d = Path(sys.argv[1])
top = int(sys.argv[2]) if len(sys.argv) > 2 else 18


def grad(img):
    y = 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]
    gx = np.zeros_like(y)
    gy = np.zeros_like(y)
    gx[:, 1:-1] = y[:, 2:] - y[:, :-2]
    gy[1:-1, :] = y[2:, :] - y[:-2, :]
    return np.hypot(gx, gy)


scores = []
for f in sorted(d.glob("f*.png")):
    im = np.asarray(Image.open(f).convert("RGB")).astype(np.float32)
    w = im.shape[1] // 3
    b, s = im[:, :w], im[:, w:2 * w]
    h = b.shape[0]
    keep = np.ones(b.shape[:2], bool)
    keep[int(h * 0.71):, int(w * 0.31):int(w * 0.69)] = False   # speedometer
    keep[int(h * 0.92):, :] = False                             # pad hints
    keep[:int(h * 0.31), :] = False                             # top HUD and sky
    loss = np.clip(grad(b) - grad(s) - 12, 0, None)
    scores.append((float(loss[keep].sum()), f))
scores.sort(reverse=True)
for sc, f in scores[:top]:
    print(f"{f.stem}: edge loss {sc:.0f}")
print(f"median {scores[len(scores) // 2][0]:.0f} over {len(scores)} frames")
pick = [f for _, f in scores[:top]]
for k in range(0, len(pick), 6):
    ims = []
    for f in pick[k:k + 6]:
        im = Image.open(f).convert("RGB")
        w = im.width // 3
        ims.append((f, im.crop((0, 0, 2 * w, im.height))))
    sheet = Image.new("RGB", (ims[0][1].width, sum(i.height for _, i in ims)))
    y = 0
    for f, i in ims:
        sheet.paste(i, (0, y))
        ImageDraw.Draw(sheet).text((4, y + 4), f.stem, fill=(0, 255, 0))
        y += i.height
    sheet.save(d / f"edge_{k // 6 + 1}.png")

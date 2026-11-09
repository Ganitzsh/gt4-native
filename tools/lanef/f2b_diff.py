#!/usr/bin/env python3
"""f2b_diff.py FRAME TAG...: F2b step 0. Frame FRAME of build/sessions/f2b0 (the base replay) against each
build/sessions/f2b0_TAG: the pixels that change, grouped into blobs (8-connected, a 2-pixel dilation), each blob's
centre, size and mean change; a PNG per tag (the base frame dimmed, the changed pixels in red, blob boxes in yellow)
in build/sessions/f2b0_TAG/diff_FRAME.png."""
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

ROOT = "C:/Users/julie/gt4-lane-F/build/sessions/"
frame = int(sys.argv[1])
base_files = dict(fm.frame_files(ROOT + "f2b0"))
base = fm.read_ppm(base_files[frame]).astype(int)
for tag in sys.argv[2:]:
    files = dict(fm.frame_files(ROOT + "f2b0_" + tag))
    other = fm.read_ppm(files[frame]).astype(int)
    d = np.abs(base - other).max(axis=2)
    mask = d > 2
    blobs = []
    B = 4   # blocks of 4 x 4 pixels, 8-connected
    hb, wb = (mask.shape[0] + B - 1) // B, (mask.shape[1] + B - 1) // B
    pad = np.zeros((hb * B, wb * B), bool)
    pad[:mask.shape[0], :mask.shape[1]] = mask
    blocks = pad.reshape(hb, B, wb, B).any(axis=(1, 3))
    seen = np.zeros_like(blocks)
    for by, bx in zip(*np.nonzero(blocks)):
        if seen[by, bx]:
            continue
        stack, members = [(by, bx)], []
        seen[by, bx] = True
        while stack:
            y, x = stack.pop()
            members.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < hb and 0 <= nx < wb and blocks[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        ys0 = min(m[0] for m in members) * B
        ys1 = (max(m[0] for m in members) + 1) * B
        xs0 = min(m[1] for m in members) * B
        xs1 = (max(m[1] for m in members) + 1) * B
        sl = (slice(ys0, min(ys1, mask.shape[0])), slice(xs0, min(xs1, mask.shape[1])))
        px = mask[sl]
        if px.sum() < 3:
            continue
        ys, xs = np.nonzero(px)
        blobs.append((int(px.sum()), sl[1].start + xs.mean(), sl[0].start + ys.mean(), sl[1].stop - sl[1].start,
                      sl[0].stop - sl[0].start, float(d[sl][px].mean()), sl))
    blobs.sort(key=lambda b: -b[0])
    h, w = mask.shape
    print(f"{tag}: frame {frame} ({w}x{h}): {int(mask.sum())} pixels change, {len(blobs)} blobs")
    for b in blobs[:25]:
        print(f"   {b[0]:6} px at ({b[1]:6.1f}, {b[2]:6.1f}) box {b[3]:3}x{b[4]:<3} mean change {b[5]:5.1f}")
    img = (base * 0.45).astype(np.uint8)
    img[mask] = [255, 40, 40]
    pic = Image.fromarray(img)
    dr = ImageDraw.Draw(pic)
    for b in blobs[:40]:
        sl = b[6]
        dr.rectangle([sl[1].start - 2, sl[0].start - 2, sl[1].stop + 1, sl[0].stop + 1], outline=(255, 230, 0))
    pic.save(ROOT + f"f2b0_{tag}/diff_{frame}.png")

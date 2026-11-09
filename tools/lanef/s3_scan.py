#!/usr/bin/env python3
"""s3_scan.py SESSION_FRAMES: session 3's live bursts L<k>_<n>.ppm (640x360), each burst analysed alone.
Per burst: (a) the blink: pixels whose luminance varies most over the burst (top 0.3 %, the lamp) give a lamp signal;
the rest of the frame (those pixels dilated by 6 px left out, HUD masked) gives mean OKLab b and mean linear Y per
frame; their correlation with the lamp signal and the difference between lamp-on and lamp-off frames (median split)
is the tint; (b) R11's band detector on the frames at 320x180; (c) the largest one-frame change of the scene's mean
log luminance and the structure correlation there."""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

d = Path(sys.argv[1])
bursts = defaultdict(list)
for p in d.glob("L*_*.ppm"):
    m = re.match(r"L(\d+)_(\d+)\.ppm", p.name)
    if m:
        bursts[int(m.group(1))].append((int(m.group(2)), p))
notes = json.load(open("C:/Users/julie/gt4-lane-F/build/lanef_tools/r5_hud.notes.json"))


def running_median(v, k):
    p = k // 2
    vp = np.pad(v, p, mode="edge")
    return np.array([np.median(vp[i:i + k]) for i in range(len(v))])


for b in sorted(bursts):
    files = [p for _, p in sorted(bursts[b])]
    rgb = [fm.read_ppm(str(p)) for p in files]
    h, w, _ = rgb[0].shape
    masks = fm.Masks(notes, w, h)
    lin = [fm.linear(x) for x in rgb]
    Y = np.stack([fm.luminance(l) for l in lin])
    var = Y.var(axis=0) * masks.scene
    thr = np.percentile(var[masks.scene], 99.7)
    lamp = var >= thr
    # leave the lamp (dilated) out of the rest
    grow = lamp.copy()
    for _ in range(6):
        g = grow.copy()
        g[1:] |= grow[:-1]; g[:-1] |= grow[1:]; g[:, 1:] |= grow[:, :-1]; g[:, :-1] |= grow[:, 1:]
        grow = g
    rest = masks.scene & ~grow
    sig = Y[:, lamp].mean(axis=1)
    rb, ry = [], []
    for l, y in zip(lin, Y):
        a, bb = fm.oklab_ab(l)
        rb.append(float(bb[rest].mean()))
        ry.append(float(y[rest].mean()))
    rb, ry = np.array(rb), np.array(ry)
    on = sig > np.median(sig)
    cb = float(np.corrcoef(sig, rb)[0, 1]) if sig.std() > 0 and rb.std() > 0 else 0.0
    cy = float(np.corrcoef(sig, ry)[0, 1]) if sig.std() > 0 and ry.std() > 0 else 0.0
    dy = 100 * (ry[on].mean() / ry[~on].mean() - 1)
    db = rb[on].mean() - rb[~on].mean()
    ys, xs = np.nonzero(lamp)
    # blink period from the lamp signal's zero crossings
    s0 = sig - np.median(sig)
    cross = np.flatnonzero(np.diff(np.sign(s0)) != 0)
    period = 2 * np.median(np.diff(cross)) if len(cross) > 2 else 0
    # (b) bands at 320x180
    nb = 0
    for y in Y[::5]:
        y2 = y[::2, ::2]
        hh, ww = y2.shape
        r0, r1, c0, c1 = int(0.55 * hh), int(0.92 * hh), int(0.10 * ww), int(0.90 * ww)
        sub = y2[r0:r1, c0:c1]
        col = sub.mean(axis=0)
        ratio = col / np.maximum(running_median(col, max(9, (c1 - c0) // 4) | 1), 1e-6)
        coherent = (sub > 1.3 * np.median(sub, axis=1, keepdims=True)).mean(axis=0)
        hit = (ratio >= 1.3) & (coherent >= 0.6)
        runs = np.diff(np.concatenate([[0], hit.astype(int), [0]]))
        starts, ends = np.flatnonzero(runs == 1), np.flatnonzero(runs == -1)
        nb += int(any(e - s >= 3 for s, e in zip(starts, ends)))
    # (c) one-frame changes
    ly = np.array([np.log(np.maximum(y, 1e-4))[masks.scene].mean() for y in Y])
    dl = np.abs(np.diff(ly))
    k = int(np.argmax(dl)) + 1 if len(dl) else 0
    print(f"L{b}: {len(files)} frames; lamp pixels {lamp.sum()} at x {xs.min()}-{xs.max()}, y {ys.min()}-{ys.max()}, "
          f"lamp swing {100 * (sig.max() / max(sig.min(), 1e-6) - 1):.0f} %, blink period {period:.0f} frames | "
          f"rest of frame: Y on/off {dy:+.2f} %, OKLab b on-off {db:+.4f}, corr(lamp, b) {cb:+.2f}, corr(lamp, Y) {cy:+.2f} | "
          f"band frames {nb} of {len(Y[::5])} | largest one-frame change {100 * (np.exp(dl.max()) - 1) if len(dl) else 0:.1f} % at {k}")

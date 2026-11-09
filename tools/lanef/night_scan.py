#!/usr/bin/env python3
"""night_scan.py REPLAY_DIR FIRST LAST [LAP]: R10's reopening test, R11's band detector and R12's tint check on a
replay's frames FIRST..LAST (320x180, HUD masked with r5_hud.notes.json).

1. Steps (PLAN.md N2's reopening test): N1's level shifts (tools/frame_metrics.py step) of at least 5 %; for each, the
   largest one-frame change within +-10 frames and its share of the shift, the structure correlation there (a cut is
   < 0.5), and whether a shift of the same sign comes LAP frames later (+-300) when LAP is given.
2. Bands: frames where a run of 3+ road columns is >= 1.3 x a running median across a quarter of the width, coherent
   in >= 60 % of the region's rows (R11.md's detector); clusters at least 40 frames apart, with x ranges.
3. Tint: the scene's mean OKLab b (yellow +) per frame, high-passed by a 61-frame running median; windows of 180 frames
   whose high-passed b has an autocorrelation peak >= 0.5 at a lag of 20-90 frames (a blink) and a swing >= 0.004."""
import json
import subprocess
import sys

import numpy as np

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

d, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
lap = int(sys.argv[4]) if len(sys.argv) > 4 else 0
notes = json.load(open("C:/Users/julie/gt4-lane-F/build/lanef_tools/r5_hud.notes.json"))
files = dict(fm.frame_files(d))
idx = [n for n in sorted(files) if first <= n <= last]
masks, prev = None, None
logy, corr, bvals, band = [], [], [], {}


def running_median(v, k):
    p = k // 2
    vp = np.pad(v, p, mode="edge")
    return np.array([np.median(vp[i:i + k]) for i in range(len(v))])


for n in idx:
    rgb = fm.read_ppm(files[n])
    h, w, _ = rgb.shape
    if masks is None:
        masks = fm.Masks(notes, w, h)
        r0, r1, c0, c1 = int(0.55 * h), int(0.92 * h), int(0.10 * w), int(0.90 * w)
        reg = masks.scene[r0:r1, c0:c1]
    lin = fm.linear(rgb)
    y = fm.luminance(lin)
    ly = np.log(np.maximum(y, 1e-4))
    logy.append(float(ly[masks.scene].mean()))
    st = fm.structure(np.where(masks.scene, ly, float(ly[masks.scene].mean())))
    corr.append(float((st * prev).sum()) if st is not None and prev is not None else 1.0)
    prev = st
    a, b = fm.oklab_ab(lin)
    bvals.append(float(b[masks.scene].mean()))
    sub = np.where(reg, y[r0:r1, c0:c1], np.nan)
    col = np.nanmean(sub, axis=0)
    ratio = col / np.maximum(running_median(col, max(9, (c1 - c0) // 4) | 1), 1e-6)
    coherent = np.nanmean(sub > 1.3 * np.nanmedian(sub, axis=1, keepdims=True), axis=0)
    hit = (ratio >= 1.3) & (coherent >= 0.6)
    k, best = 0, None
    while k < len(hit):
        if hit[k]:
            j = k
            while j < len(hit) and hit[j]:
                j += 1
            if j - k >= 3 and (best is None or j - k > best[1] - best[0]):
                best = (c0 + k, c0 + j, float(ratio[k:j].mean()))
            k = j
        else:
            k += 1
    if best:
        band[n] = best
x, corr, bv = np.array(logy), np.array(corr), np.array(bvals)
pct = lambda v: 100 * (np.exp(v) - 1)
print(f"frames {idx[0]}..{idx[-1]} ({len(idx)}); cuts (corr < 0.5): {[idx[i] for i in np.flatnonzero(corr < 0.5)][:40]}; "
      f"repeated frames: {int(sum(1 for k in range(1, len(x)) if x[k] == x[k - 1]))}")

# 1. steps, through the pushed N1
res = json.loads(subprocess.run([sys.executable, "C:/Users/julie/gt4-lane-F/tools/frame_metrics.py", d, "step", "--notes",
                                 "C:/Users/julie/gt4-lane-F/build/lanef_tools/r5_hud.notes.json"],
                                capture_output=True, text=True).stdout)
shifts = [s for s in res["step"]["level_shifts"] if first <= s["frame"] <= last]
pos = {n: i for i, n in enumerate(idx)}
print(f"N1 level shifts in range: {len(shifts)}")
for s in shifts:
    if s["change"] < 0.05 or s["frame"] not in pos:
        continue
    i = pos[s["frame"]]
    lo, hi = max(1, i - 10), min(len(x) - 1, i + 10)
    one = max(range(lo, hi + 1), key=lambda k: abs(x[k] - x[k - 1]))
    level = np.log1p(s["change"])
    share = abs(x[one] - x[one - 1]) / level if level else 0
    again = ""
    if lap:
        others = [o for o in res["step"]["level_shifts"] if o["direction"] == s["direction"] and abs(o["frame"] - s["frame"] - lap) <= 300]
        again = f"; a lap later: {[(o['frame'], round(o['change'], 3)) for o in others] or 'none'}"
    # a replay that repeats a frame (presents it again) shows the change of several game frames in one: not a step
    stall = sum(1 for k in range(max(1, one - 8), one) if x[k] == x[k - 1])
    if share >= 0.7 and corr[one] >= 0.5 and stall:
        verdict = f"after {stall} repeated frames (a replay stall): not a one-frame step"
    else:
        verdict = "ONE-FRAME STEP" if share >= 0.7 and corr[one] >= 0.5 else "not a one-frame step"
    print(f"  {s['frame']} {s['direction']} {100 * s['change']:.1f} %: largest one-frame {pct(x[one] - x[one - 1]):+.1f} % at "
          f"{idx[one]} (share {share:.2f}), corr {corr[one]:.3f} -> {verdict}{again}")

# 2. bands
cl = []
for n in sorted(band):
    if cl and n - cl[-1][-1] < 40:
        cl[-1].append(n)
    else:
        cl.append([n])
print(f"band frames: {len(band)} in {len(cl)} clusters")
for c in cl[:30]:
    top = max(c, key=lambda n: band[n][2])
    print(f"  {c[0]}..{c[-1]} ({len(c)} frames): strongest {top}, columns {band[top][0]}-{band[top][1]} of 320, {band[top][2]:.2f} x")

# 3. tint
hp = bv - running_median(bv, 61)
found = []
for s0 in range(0, len(hp) - 180, 60):
    w = hp[s0:s0 + 180] - hp[s0:s0 + 180].mean()
    if w.std() == 0:
        continue
    ac = np.array([np.corrcoef(w[:-L], w[L:])[0, 1] for L in range(20, 91)])
    L = int(np.argmax(ac)) + 20
    swing = float(np.percentile(w, 95) - np.percentile(w, 5))
    if ac.max() >= 0.5 and swing >= 0.004:
        found.append((idx[s0], idx[s0 + 179], L, round(float(ac.max()), 2), round(swing, 4)))
print(f"tint: OKLab b median {np.median(bv):.4f}, high-passed p95-p5 {np.percentile(hp, 95) - np.percentile(hp, 5):.4f}; "
      f"blinking windows: {len(found)}")
for f in found[:20]:
    print(f"  {f[0]}..{f[1]}: period {f[2]} frames, autocorrelation {f[3]}, swing {f[4]}")

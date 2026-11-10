#!/usr/bin/env python3
"""A3b step 0: the main view's camera per frame (a3b view lines) against the game's signals (camfield changes of
+1a0, +1a8, +138, +ac8; the camera object / entry). Per frame: the constant-velocity residual per VBlank (metres),
the view direction's turn (degrees) and the p00 change (FOV, %). Prints every frame over the thresholds with the
signals within 2 frames, then the distribution on frames without a signal. py -I a3b_geo.py FILE"""
import math
import re
import sys

ROLE = sys.argv[2] if len(sys.argv) > 2 else "1"   # (the view role: 1 the race's main view, 6 Photo Travel's preview)
views, fields = [], []
for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"\[(\d+\.\d+)\].*a3b view vbl (\d+) role (\d+) flags (\d+) pass (\d+) pos (\S+) (\S+) (\S+) dir (\S+) "
                  r"(\S+) (\S+) p00 (\S+) p11 (\S+)", line)
    if m and m.group(3) == ROLE:
        g = m.groups()
        views.append((float(g[0]), int(g[1]), tuple(map(float, g[5:8])), tuple(map(float, g[8:11])), float(g[11])))
        continue
    m = re.search(r"\[(\d+\.\d+)\].*a3b camfield \+(\w+) (\d+) -> (\d+)", line)
    if m and m.group(2) in ("1a0", "1a8", "138", "ac8", "aec", "140"):
        fields.append((float(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4))))

print(len(views), "main-view frames,", len(fields), "signal changes")
rows = []
for k in range(2, len(views)):
    t0, v0, p0, d0, f0 = views[k - 2]
    t1, v1, p1, d1, f1 = views[k - 1]
    t2, v2, p2, d2, f2 = views[k]
    g1, g2 = max(v1 - v0, 1), max(v2 - v1, 1)
    pred = [p1[i] + (p1[i] - p0[i]) * g2 / g1 for i in range(3)]
    res = math.dist(pred, p2) / g2   # metres per VBlank of the gap
    dot = sum(a * b for a, b in zip(d1, d2)) / max(1e-9, math.hypot(*d1) * math.hypot(*d2))
    turn = math.degrees(math.acos(max(-1.0, min(1.0, dot)))) / g2
    fov = abs(f2 / f1 - 1.0) * 100 if f1 else 0.0
    near = [f"{f[1]}:{f[2]}>{f[3]}" for f in fields if abs(f[0] - t2) < 0.05]
    rows.append((t2, v2, g2, res, turn, fov, near))
flag = [r for r in rows if r[3] > 1.0 or r[4] > 10.0 or r[5] > 2.0]
print(f"frames over a threshold (residual > 1 m/VBlank, turn > 10 deg/VBlank, FOV > 2 %): {len(flag)}")
for t, v, g, res, turn, fov, near in flag:
    print(f"  {t:.3f} vbl {v} gap {g}: residual {res:.2f} m, turn {turn:.1f} deg, fov {fov:.1f} %  signals {near}")
unsig = [r for r in rows if not r[6]]
for name, idx in (("residual m/VBlank", 3), ("turn deg/VBlank", 4), ("fov %", 5)):
    v = sorted(r[idx] for r in unsig)
    print(f"no-signal frames, {name}: median {v[len(v) // 2]:.3f}, p99 {v[int(len(v) * 0.99)]:.3f}, "
          f"p999 {v[int(len(v) * 0.999)]:.3f}, max {v[-1]:.3f}")
gaps = {}
for r in rows:
    gaps[r[2]] = gaps.get(r[2], 0) + 1
print("VBlank gaps between main views:", dict(sorted(gaps.items())))
sig_unseen = [f for f in fields if f[1] in ("1a0", "1a8", "ac8", "aec", "140") and not any(abs(r[0] - f[0]) < 0.05 and
              (r[3] > 1.0 or r[4] > 10.0 or r[5] > 2.0) for r in rows)]
print("signal changes with no geometric jump within 2 frames:", [(round(f[0], 3), f[1], f[2], f[3]) for f in sig_unseen])

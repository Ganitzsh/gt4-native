#!/usr/bin/env python3
"""A3b step 0: the cut thresholds under a lower frame rate or a hitch, offline. The main views of a session log
(a3b view lines, role 1) thinned to every Nth, so each step spans N VBlanks as at 60/N fps or across an N-VBlank
hitch; on each, the per-VBlank residual of a constant-velocity prediction, the turn and the FOV change, as A3b's net
measures them, and the frames over the thresholds (1 m per VBlank, 90 degrees, 3 % FOV) with no camfield signal
within 2 views: false flags. py -I a3b_hitch.py LOG..."""
import math
import re
import sys

RES, TURN, FOV = 1.0, 90.0, 3.0
for path in sys.argv[1:]:
    views, sig = [], []
    for line in open(path, errors="replace"):
        m = re.search(r"\[(\d+\.\d+)\].*a3b view vbl (\d+) role 1 flags \d+ pass \d+ pos (\S+) (\S+) (\S+) dir (\S+) "
                      r"(\S+) (\S+) p00 (\S+)", line)
        if m:
            g = m.groups()
            views.append((float(g[0]), int(g[1]), tuple(map(float, g[2:5])), tuple(map(float, g[5:8])), float(g[8])))
            continue
        m = re.search(r"\[(\d+\.\d+)\].*a3b camfield \+(1a0|1a8|ac8|aec|140) (\d+) -> (\d+)", line)
        if m:
            sig.append(float(m.group(1)))
    for n in (1, 2, 3, 4, 6, 8):
        v = views[::n]
        flags, worst = [], [0.0, 0.0, 0.0]
        for k in range(2, len(v)):
            (_, b0, p0, d0, f0), (_, b1, p1, d1, f1), (t2, b2, p2, d2, f2) = v[k - 2], v[k - 1], v[k]
            g1, g2 = max(b1 - b0, 1), max(b2 - b1, 1)
            pred = [p1[i] + (p1[i] - p0[i]) * g2 / g1 for i in range(3)]
            res = math.dist(pred, p2) / g2
            dot = sum(a * b for a, b in zip(d1, d2)) / max(1e-9, math.hypot(*d1) * math.hypot(*d2))
            turn = math.degrees(math.acos(max(-1.0, min(1.0, dot))))   # the whole step's turn: a cut is a jump
            # the FOV's change per VBlank (compounded over the step: a steady zoom is the same rate at any frame rate)
            fov = abs((f2 / f1) ** (1.0 / g2) - 1.0) * 100 if f1 and f2 / f1 > 0 else 0.0
            near = any(abs(s - t2) < 0.05 * n + 0.05 for s in sig)
            if not near:
                worst = [max(worst[0], res), max(worst[1], turn), max(worst[2], fov)]
                if res > RES or turn > TURN or fov > FOV:
                    flags.append((b2, round(res, 2), round(turn, 1), round(fov, 1)))
        print(f"{path.split('/')[-1].split(chr(92))[-1]} every {n}: {len(v)} views, false flags {len(flags)} "
              f"{flags[:4]}; worst without a signal: residual {worst[0]:.2f} m/VBlank, turn {worst[1]:.1f} deg, "
              f"fov {worst[2]:.1f} %", flush=True)

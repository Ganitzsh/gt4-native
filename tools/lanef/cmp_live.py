#!/usr/bin/env python3
"""cmp_live.py LIVE_DIR REPLAY_DIR [LABEL]: live frames (live/f_<k>.ppm, k = host frames since the first burst)
against a replay's frames (f_<n>.ppm): the offset n = k + off that minimises the mean absolute 8-bit difference on
a control window (live 1000-1100, offsets -10..+10), then that difference per window and at lane C's frames."""
import sys

import numpy as np

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

live = dict(fm.frame_files(sys.argv[1]))
rep = dict(fm.frame_files(sys.argv[2]))
label = sys.argv[3] if len(sys.argv) > 3 else sys.argv[2]
cache = {}


def img(d, k):
    key = (id(d), k)
    if key not in cache:
        cache[key] = fm.read_ppm(d[k]).astype(np.int16)
    return cache[key]


def diff(k, off):
    if k not in live or k + off not in rep:
        return None
    return float(np.abs(img(live, k) - img(rep, k + off)).mean())


best = None
for off in range(-10, 11):
    v = [diff(k, off) for k in range(1000, 1101, 4)]
    v = [x for x in v if x is not None]
    if v and (best is None or np.mean(v) < best[1]):
        best = (off, float(np.mean(v)))
off = best[0]
print(f"{label}: live {len(live)} frames (0..{max(live)}), replay {len(rep)}; replay = live + {off} "
      f"(control 1000-1100 mean |diff| {best[1]:.2f})")
windows = [("1-50", 1, 50), ("51-200", 51, 200), ("control 1000-1100", 1000, 1100), ("1440-1500", 1440, 1500),
           ("2090-2140", 2090, 2140), ("3440-3480", 3440, 3480), ("3540-3600", 3540, 3600)]
for name, a, b in windows:      # in replay frame numbers
    v = [diff(n - off, off) for n in range(a, b + 1)]
    v = [x for x in v if x is not None]
    if v:
        print(f"  replay {name}: mean |diff| {np.mean(v):6.2f}, max {np.max(v):6.2f} ({len(v)} frames)")
    else:
        print(f"  replay {name}: no live frames")
for n in (201, 1468, 2113, 3465):
    v = [(m, diff(m - off, off)) for m in range(n - 3, n + 4)]
    print(f"  replay {n} +-3: " + " ".join(f"{m}:{x:.1f}" if x is not None else f"{m}:-" for m, x in v))
cache.clear()

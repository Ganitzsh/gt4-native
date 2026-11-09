#!/usr/bin/env python3
"""n3_bisect.py TAG CAP FRAME: which of the layer-state candidates of build/sessions/n3_TAG (their host texture ids, from
the N3 log) draw the lamp pools on the road at FRAME of CAP? Skips halves of them (GT4_R10_SKIPTEX, the debug host of
f2b_step0.py) until single ids remain whose skip darkens the road region (lower 40 %, centre 60 %) by more than 0.3 of
mean luminance. Results in build/sessions/n3_TAG/bisect.txt."""
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

ROOT = Path("C:/Users/julie/gt4-lane-F/build/sessions")
TOOLS = Path(__file__).resolve().parent
tag, cap, frame = sys.argv[1], sys.argv[2], int(sys.argv[3])
out = open(ROOT / f"n3_{tag}" / "bisect.txt", "w", encoding="utf-8")


def say(s):
    print(s, flush=True)
    out.write(s + "\n")
    out.flush()


log = (ROOT / f"n3_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")
host = {}
for l in log:
    m = re.match(r"N3 host texture (\d+) for (\w+)", l)
    if m:
        host[m.group(2)] = int(m.group(1))
ids = []
for l in log:
    m = re.match(r"N3 candidate: texture (\w+) ", l)
    if m and host.get(m.group(1)) and host[m.group(1)] not in ids:
        ids.append(host[m.group(1)])
runs = 0


def road(tagname):
    a = fm.read_ppm(dict(fm.frame_files(str(ROOT / f"f2b0_{tagname}")))[frame]).astype(float)
    h, w = a.shape[:2]
    r = a[int(h * 0.6):, int(w * 0.2):int(w * 0.8)]
    return float((0.2126 * r[..., 0] + 0.7152 * r[..., 1] + 0.0722 * r[..., 2]).mean())


def replay(tagname, skip):
    args = [sys.executable, str(TOOLS / "f2b_step0.py"), "--no-build", f"--cap={cap}", f"--frames={frame + 20}",
            "--drawlist=0,0,1,1", f"--capture={frame}-{frame}", f"--tag={tagname}"]
    if skip:
        args.append("GT4_R10_SKIPTEX=" + ",".join(map(str, skip)))
    subprocess.run(args, capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    return road(tagname)


base = replay(f"{tag}_b0", [])
say(f"{len(ids)} candidate ids; road mean {base:.2f} at frame {frame}")


def darkens(subset):
    global runs
    runs += 1
    v = replay(f"{tag}_b{runs}", subset)
    say(f"run {runs}: skip {len(subset)} {subset[:6]}{'...' if len(subset) > 6 else ''}: road {v:.2f} ({v - base:+.2f})")
    return base - v > 0.3


found, todo = [], [ids]
while todo and runs < 30:
    s = todo.pop()
    if not darkens(s):
        continue
    if len(s) == 1:
        found.append(s[0])
        continue
    todo += [s[len(s) // 2:], s[:len(s) // 2]]
say(f"pool textures: {found} ({runs} replays)")

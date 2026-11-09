#!/usr/bin/env python3
"""f2b_bisect.py: F2b step 0. Which textures draw the street-lamp heads in frame 2514 of r5_ss_ab.cap? The lamp dots are
the base frame's bright yellow pixels (R > 170, G > 130, B < 150) in the region x 630-760, y 260-330 (1280x720; under
the HUD's text). The scene target's (target 4) textures of VBlank 15774 are split in halves and skipped
(GT4_R10_SKIPTEX) until single textures remain whose skip changes those pixels. One replay at a time through
f2b_step0.py --no-build; results in build/sessions/f2b0/bisect.txt."""
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import frame_metrics as fm  # noqa: E402

ROOT = Path("C:/Users/julie/gt4-lane-F/build/sessions")
TOOLS = Path(__file__).resolve().parent
FRAME = 2514
out = open(ROOT / "f2b0" / "bisect.txt", "w", encoding="utf-8")


def say(s):
    print(s, flush=True)
    out.write(s + "\n")
    out.flush()


texs = []
for line in open(ROOT / "f2b0" / "r10.log.draws", encoding="utf-8"):
    m = re.match(r"^15774 draw \d+ modeled 1 .* tex 1:(\d+):\d+ target 4 ", line)
    if m and int(m.group(1)) not in texs:
        texs.append(int(m.group(1)))
base = fm.read_ppm(dict(fm.frame_files(str(ROOT / "f2b0")))[FRAME]).astype(int)
region = np.zeros(base.shape[:2], bool)
region[260:330, 630:760] = True
lum = base.sum(axis=2).astype(float)
k = 7   # the mean of a 15 x 15 neighbourhood, from an integral image
ii = np.pad(lum, ((k + 1, k), (k + 1, k)), mode="edge").cumsum(0).cumsum(1)
local = (ii[2 * k + 1:, 2 * k + 1:] - ii[:-2 * k - 1, 2 * k + 1:] - ii[2 * k + 1:, :-2 * k - 1] + ii[:-2 * k - 1, :-2 * k - 1]) \
    / (2 * k + 1) ** 2
dots = region & (lum > 1.8 * local) & (lum > 150) & (base[..., 2] < 0.95 * base[..., 1])
say(f"{len(texs)} modeled textures on target 4; {int(dots.sum())} lamp-dot pixels")
runs = 0


def changes(subset):
    global runs
    runs += 1
    tag = f"bis{runs}"
    subprocess.run([sys.executable, str(TOOLS / "f2b_step0.py"), "--no-build", f"--tag={tag}",
                    "GT4_R10_SKIPTEX=" + ",".join(map(str, subset))], capture_output=True, text=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)
    other = fm.read_ppm(dict(fm.frame_files(str(ROOT / f"f2b0_{tag}")))[FRAME]).astype(int)
    n = int((dots & (other.sum(axis=2) < 0.5 * lum)).sum())   # the dot itself goes dark, not only its surroundings
    say(f"run {runs}: skip {len(subset)} textures {subset[:6]}{'...' if len(subset) > 6 else ''}: {n} dot pixels change")
    return n


found = []
todo = [texs]
while todo and runs < 40:
    s = todo.pop()
    if not changes(s):
        continue
    if len(s) == 1:
        found.append(s[0])
        continue
    todo += [s[len(s) // 2:], s[:len(s) // 2]]
say(f"textures drawing the lamp dots: {found} ({runs} replays)")

#!/usr/bin/env python3
"""n3_where.py CAP FRAMES IDS: where a texture group changes the picture. Two replays with the debug host
(gt4_host_f2b0.exe), base and IDS skipped, 640 x 360, a frame every 100; prints each frame's changed pixels and saves the
frame with the most as a side-by-side (base | changed pixels in red) in build/sessions/n3_where.png."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

cap, frames, ids = sys.argv[1], sys.argv[2], sys.argv[3]
work = CLONE / "build" / "sessions" / "n3_where"
out = {}
for name, skip in (("base", None), ("skip", ids)):
    d = work / name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip:
        env["GT4_R10_SKIPTEX"] = skip
    locks = take_slot(f"N3 where: {name}")
    try:
        subprocess.run([str(CLONE / "build/lanef_run/gt4_host_f2b0.exe"), "--headless", "--size", "640x360", "--replay", cap,
                        "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--capture", str(d / "f.ppm"), "--capture-every", "100"],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    out[name] = {n: fm.read_ppm(p).astype(int) for n, p in fm.frame_files(str(d))}
best = None
for n, b in sorted(out["base"].items()):
    if n not in out["skip"]:
        continue
    m = np.abs(b - out["skip"][n]).max(axis=2) > 2
    print(f"frame {n}: {int(m.sum())} pixels changed")
    if m.sum() and (best is None or m.sum() > best[1]):
        best = (n, int(m.sum()), b, m)
if best:
    n, c, b, m = best
    img = (b * 0.6).astype(np.uint8)
    img[m] = [255, 40, 40]
    Image.fromarray(np.concatenate([b.astype(np.uint8), img], axis=1)).save(CLONE / "build" / "sessions" / "n3_where.png")
    print(f"most at frame {n}: {c} pixels; build/sessions/n3_where.png")

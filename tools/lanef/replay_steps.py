#!/usr/bin/env python3
"""replay_steps.py CAPTURE FRAMES [--size WxH] [--preset original]: replay a local capture headless with every frame
captured (frame interpolation and the HD pack off), then list camera cuts and abrupt level shifts of the mean log
luminance (HUD masked), as the N0 scans did. One heavy slot; the host starts without a console."""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("capture")
ap.add_argument("frames", type=int)
ap.add_argument("--size", default="320x180")
ap.add_argument("--preset", default="original")
a = ap.parse_args()
cap = Path(a.capture)
out = CLONE / "build" / "sessions" / f"replay_{cap.stem}_{a.preset}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot(f"replay {cap.name} ({a.frames} frames, every frame captured)")
try:
    host = CLONE / "build" / "lanef_run" / "gt4_host_replay.exe"
    host.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
    args = [str(host), "--headless", "--size", a.size, "--replay", str(cap), "--frames", str(a.frames), "--fixed-step",
            "--preset", a.preset, "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
            "--capture", str(out / "f.ppm"), "--capture-every", "1"]
    r = subprocess.run(args, capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    log(f"replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
    log("slot released")

notes = json.load(open(CLONE / "build" / "lanef_tools" / "r5_hud.notes.json"))
files = fm.frame_files(str(out))
masks = None
x, corr, idx, prev = [], [], [], None
for n, path in files:
    rgb = fm.read_ppm(path)
    h, w, _ = rgb.shape
    if masks is None:
        masks = fm.Masks(notes, w, h)
    logy = np.log(np.maximum(fm.luminance(fm.linear(rgb)), 1e-4))
    st = fm.structure(np.where(masks.scene, logy, float(logy[masks.scene].mean())))
    corr.append(float((st * prev).sum()) if st is not None and prev is not None else 1.0)
    prev = st
    idx.append(n)
    x.append(float(logy[masks.scene].mean()))
x, corr, idx = np.array(x), np.array(corr), np.array(idx)
np.savez(out / "series.npz", idx=idx, mean_log=x, corr=corr)
print(f"{len(files)} frames, nonzero frames: {sum(1 for v in x if v > -9)}")
print("camera cuts:", [int(idx[i]) for i in np.flatnonzero(corr < 0.5)][:30])
K, c = 15, []
for t in range(K, len(x) - K):
    if (corr[max(0, t - 2):t + 3] < 0.5).any():
        continue
    d = np.median(x[t:t + K]) - np.median(x[t - K:t])
    two = x[min(t + 1, len(x) - 1)] - x[t - 1]
    if abs(d) > 0.03 and abs(two) > 0.7 * abs(d) and max(np.std(x[t - K:t]), np.std(x[t:t + K])) < 0.5 * abs(d):
        if c and t - c[-1][0] <= 3:
            if abs(d) > abs(c[-1][1]):
                c[-1] = (t, d)
        else:
            c.append((t, d))
print("level shifts:", [(int(idx[t]), round(float(np.exp(abs(d)) - 1), 3), "up" if d > 0 else "down") for t, d in c])

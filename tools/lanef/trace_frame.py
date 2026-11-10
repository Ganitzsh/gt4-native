#!/usr/bin/env python3
"""trace_frame.py CAP FRAME X0 Y0 X1 Y1: which skipped texture identity draws a region of one frame (the coordinator's
check of lap_ssr5f_boot frame 9,725's orange bands, 2026-11-09). The candidates are the identities v4 skipped in the
hunt (build/sessions/n3v4b_ssr5fb/v4.log). A bisection: replays of CAP to FRAME on the knobs7 host with
GT4_N3_SKIPIDS = half of the candidates, the region's colour against the base frame (orange: R - B); the half that
removes it is split again. Then that identity's v4 lines (vc, c/r, share, width, texel) are printed. One heavy-both slot
per replay (an 8 GB capture)."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402
import slot  # noqa: E402

cap, frame = sys.argv[1], int(sys.argv[2])
x0, y0, x1, y1 = map(int, sys.argv[3:7])   # at 640 x 360
HOST = CLONE / "build/lanef_run/gt4_host_n3knobs7.exe"
OUT = CLONE / "build/sessions/trace_frame"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
lines = [l for l in (CLONE / "build/sessions/n3v4b_ssr5fb/v4.log").read_text(encoding="utf-8").splitlines() if l.endswith("skip")]
cands = sorted({re.search(r"identity (\w+)", l).group(1) for l in lines})
log(f"{len(cands)} candidate identities")


def run(ids, tag):
    d = OUT / tag
    d.mkdir()
    env = dict(os.environ, GT4_HOST_RECORD_SCENE="1")
    if ids:
        env["GT4_N3_SKIPIDS"] = ",".join(ids)
    locks = slot.take(["heavy-both"], f"lane F: trace frame {frame} ({tag})", min_gb=7.0)
    try:
        subprocess.run([str(HOST), "--headless", "--size", "640x360", "--replay", cap, "--frames", str(frame), "--fixed-step",
                        "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                        "--set", "lighting.night=enhanced", "--capture-frames", str(frame), "--capture-dir", str(d)],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    im = fm.read_ppm(dict(fm.frame_files(str(d)))[frame]).astype(np.float32)[y0:y1, x0:x1]
    return float((im[..., 0] - im[..., 2]).mean())


base = run([], "base")
log(f"base: orange (R - B) {base:.1f}")
pool = cands
step = 0
while len(pool) > 1:
    half = pool[: len(pool) // 2]
    o = run(half, f"s{step}")
    gone = o < base * 0.5
    log(f"skip {len(half)} of {len(pool)}: orange {o:.1f} -> {'gone' if gone else 'stays'}")
    pool = half if gone else pool[len(pool) // 2:]
    step += 1
o = run(pool, "final")
log(f"identity {pool[0]}: skipping it alone, orange {o:.1f} (base {base:.1f})")
for l in lines:
    if pool[0] in l:
        print(l)
        break
for d in OUT.iterdir():
    if d.is_dir() and d.name not in ("base", "final"):
        shutil.rmtree(d, ignore_errors=True)

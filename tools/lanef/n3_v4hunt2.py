#!/usr/bin/env python3
"""n3_v4hunt2.py TAG CAP FRAMES DENSE: the hunt with v4 itself (the architect, 2026-11-09). gt4_host_n3orient.exe judges
every lit-road-layer item at first sight by v4 (v2's state and colour terms, centre/rim < 3, up-facing share >= 0.9,
width >= 0.9 m, max(u, v) per texel >= 0.03 m) and skips its texture's draws from then on (GT4_N3_V4SKIP=1, keyed by
texture identity, so new host ids for the same texture are covered). Base: the same replay without the skip (both with
lighting.night = enhanced, scene records and the N3 log on). 640 x 360, a frame every 25 from 300 plus DENSE (A-B/STEP).
Every frame with a change is saved as base | skipped | changes (red) in build/sessions/n3v4b_TAG/; the 'N3 v4' lines of
the skip run are kept (v4.log). Two heavy slots in turn."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

tag, cap, frames, dense = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
out = CLONE / "build" / "sessions" / f"n3v4b_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
shots = {}
for name, skip in (("base", False), ("skip", True)):
    d = out / name
    d.mkdir()
    env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1", GT4_N3_LOG="1", GT4_HOST_RECORD_SCENE="1")
    if skip:
        env["GT4_N3_V4SKIP"] = "1"
    locks = take_slot(f"N3 v4 hunt (v4 itself) {tag}: {name}")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "640x360", "--replay", cap,
                            "--frames", str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced", "--capture-frames",
                            f"300-{frames}/25,{dense}", "--capture-dir", str(d)],
                           capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        if skip:
            (out / "v4.log").write_text("\n".join(l for l in r.stdout.splitlines() if l.startswith("N3 v4:")) + "\n",
                                        encoding="utf-8")
        log(f"{name}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[name] = dict(fm.frame_files(str(d)))
changed = 0
for n in sorted(shots["base"]):
    if n not in shots["skip"]:
        continue
    b, g = fm.read_ppm(shots["base"][n]), fm.read_ppm(shots["skip"][n])
    m = np.abs(g.astype(np.int16) - b).max(axis=2) > 2
    c = int(m.sum())
    print(f"frame {n}: {c} pixels changed", flush=True)
    if c > 200:
        changed += 1
        img = (b * 0.6).astype(np.uint8)
        img[m] = [255, 40, 40]
        Image.fromarray(np.concatenate([b, g, img], axis=1)).save(out / f"f{n:05d}.png")
print(f"{changed} frames changed of {len(shots['base'])}")
for name in ("base", "skip"):
    shutil.rmtree(out / name, ignore_errors=True)

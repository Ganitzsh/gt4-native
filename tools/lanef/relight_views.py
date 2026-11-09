#!/usr/bin/env python3
"""relight_views.py: the night relight's albedo and light, split (the architect's check, 2026-11-09). lap_csr5 with
v4's skip and the batwing (65 degrees, I0 3) on the knobs4 host, three replays: GT4_LIGHT_VIEW 0 (the picture), 3 (the
estimated albedo), 4 (the added light alone, sum / (sum + 1)); frames 2000, 3600, 4200; a sheet per frame (picture,
albedo, light) in build/sessions/relight_views/."""
import os, shutil, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402
OUT = CLONE / "build/sessions/relight_views"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
frames = [2000, 3600, 4200]
shots = {}
for v in ("0", "3", "4"):
    d = OUT / f"v{v}"
    d.mkdir()
    env = dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_N3_V4SKIP="1", GT4_N3_GLOW_BATWING="65", GT4_N3_GLOW_RANGE="40",
               GT4_N3_GLOW_INTENSITY="3", GT4_LIGHT_VIEW=v)
    locks = take_slot(f"lane F: N3 relight views {v}")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3knobs4.exe"), "--headless", "--size", "640x360", "--replay",
                            str(CLONE / "build/caps/lap_csr5.cap"), "--frames", "4200", "--fixed-step", "--preset", "original",
                            "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set",
                            "lighting.night=enhanced", "--capture-frames", ",".join(map(str, frames)), "--capture-dir", str(d)],
                           capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        log(f"view {v}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[v] = dict(fm.frame_files(str(d)))
for n in frames:
    ims = [fm.read_ppm(shots[v][n]).astype(np.uint8) for v in ("0", "3", "4") if n in shots[v]]
    if len(ims) == 3:
        Image.fromarray(np.concatenate(ims, axis=1)).save(OUT / f"split_{n}.png")
        a = ims[1].astype(np.float32).mean(axis=2)
        H = a.shape[0]
        print(f"frame {n}: albedo mean, near road (rows 75-95 %) {a[int(H * .75):int(H * .95), 200:440].mean():.1f}, "
              f"far road (rows 52-60 %) {a[int(H * .52):int(H * .60), 260:380].mean():.1f}")
for v in ("0", "3", "4"):
    shutil.rmtree(OUT / f"v{v}", ignore_errors=True)
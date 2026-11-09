#!/usr/bin/env python3
"""n3_v4hunt.py TAG CAP FRAMES DENSE: the architect's point (2), looking for wide markings at night on purpose. Every
texture with an item of width >= 0.9 m among the v3 looks (n3o_TAG: orient.tsv and its 'N3 footprint' lines) is
skipped at once (GT4_R10_SKIPTEX, gt4_host_n3orient.exe: the same host ids as the log), against base: 640 x 360, a
frame every 50 from 300, plus DENSE (A-B/STEP: the start/finish crossing). Each changed frame is saved as
base | skipped | changes (red) in build/sessions/n3v4_TAG/; prints the changed pixels per frame. Two heavy slots in turn."""
import os
import re
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
S = CLONE / "build" / "sessions"
out = S / f"n3v4_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
v3 = set()
for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = l.split("\t")
    if c[3] == "1" and float(c[4]) >= 0.9 and float(c[9]) > 0:
        v3.add(c[0])
wide = set()
for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 footprint: host texture (\d+) pieces \d+ area (\S+) width (\S+)", l)
    if m and m.group(1) in v3 and float(m.group(2)) > 0 and float(m.group(3)) >= 0.9:
        wide.add(m.group(1))
ids = ",".join(sorted(wide, key=int))
log(f"{tag}: {len(wide)} textures with an item >= 0.9 m wide (of {len(v3)} v3 textures)")
(out / "skipped.txt").write_text(ids + "\n", encoding="ascii")
pick = f"300-{frames}/50,{dense}"
shots = {}
for name, skip in (("base", None), ("skip", ids)):
    d = out / name
    d.mkdir()
    env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip:
        env["GT4_R10_SKIPTEX"] = skip
    locks = take_slot(f"N3 v4 hunt {tag}: {name}")
    try:
        subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "640x360", "--replay", cap,
                        "--frames", str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--capture-frames", pick, "--capture-dir", str(d)],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[name] = dict(fm.frame_files(str(d)))
for n in sorted(shots["base"]):
    if n not in shots["skip"]:
        continue
    b, g = fm.read_ppm(shots["base"][n]), fm.read_ppm(shots["skip"][n])
    m = np.abs(g.astype(np.int16) - b).max(axis=2) > 2
    c = int(m.sum())
    print(f"frame {n}: {c} pixels changed", flush=True)
    if c > 200:
        img = (b * 0.6).astype(np.uint8)
        img[m] = [255, 40, 40]
        Image.fromarray(np.concatenate([b, g, img], axis=1)).save(out / f"f{n:05d}.png")
for name in ("base", "skip"):
    shutil.rmtree(out / name, ignore_errors=True)

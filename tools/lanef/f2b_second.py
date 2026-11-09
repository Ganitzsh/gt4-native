#!/usr/bin/env python3
"""f2b_second.py CAP FRAMES TAG: F2b's second-course check. The release host from a75a6c2 (the capture's game) replays
CAP with lighting.night = enhanced and scene records on; keeps the classifier's verdict lines ('F2b glow class'), the
lamp totals ('F2b course lamps'), the gather's and the scene summary's lines, and frames every 500 at 640 x 360 in
build/sessions/f2b2_TAG/. One heavy slot."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, tag = sys.argv[1], sys.argv[2], sys.argv[3]
out = CLONE / "build" / "sessions" / f"f2b2_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot(f"F2b second course: {tag}")
try:
    r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_main_a75a6c2.exe"), "--headless", "--size", "640x360", "--replay",
                        cap, "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced", "--capture-frames",
                        f"500-{frames}/500", "--capture-dir", str(out)], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW, env=dict(os.environ, GT4_HOST_RECORD_SCENE="1"))
    (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    log(f"replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
for l in (out / "host.log").read_text(encoding="utf-8", errors="replace").splitlines():
    if any(k in l for k in ("F2b", "glow", "gather", "Gather", "lights")):
        print(l[:400])

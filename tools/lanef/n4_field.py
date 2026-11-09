#!/usr/bin/env python3
"""n4_field.py CAP FRAMES: the build folder's host (copied to build/lanef_run/gt4_host_n4field.exe) replays CAP (read in
place) with lighting.night = enhanced, the scene split and scene records on, and prints the scene front end's flare
bracket counts and N4's skip line. One heavy slot."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames = sys.argv[1], sys.argv[2]
host = CLONE / "build" / "lanef_run" / "gt4_host_n4field.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
out = CLONE / "build" / "sessions" / "n4_field"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot("N4 field rule: replay")
try:
    r = subprocess.run([str(host), "--headless", "--size", "640x360", "--replay", cap, "--frames", frames, "--fixed-step",
                        "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                        "--set", "lighting.night=enhanced", "--capture-frames", "600-600", "--capture-dir", str(out)],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, **({} if "nosplit" in sys.argv else {"GT4_GSN_SCENE_SPLIT": "1"}), GT4_HOST_RECORD_SCENE="1"))
    text = r.stdout + r.stderr
    (out / "host.log").write_text(text, encoding="utf-8", errors="replace")
    log(f"replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
for m in re.finditer(r"(split o[nf]+ |flare bracket draws[^;]*|scene frames ending with a bracket open[^;]*|N4 draws skipped[^\n]*)", text):
    print(m.group(1))

#!/usr/bin/env python3
"""cap_course.py CAP [FRAMES]: which crs/ file a capture's course comes from: a short replay with the set log
(gt4_host_n3orient.exe, GT4_N3_SETLOG=1; the sets a recording exports at its start), then which_course.py. One slot."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, CLONE  # noqa: E402

cap = sys.argv[1]
frames = sys.argv[2] if len(sys.argv) > 2 else "600"
out = CLONE / "build" / "sessions" / ("cap_course_" + Path(cap).stem)
out.mkdir(parents=True, exist_ok=True)
locks = take_slot(f"which course: {Path(cap).name}")
try:
    r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "320x180", "--replay", cap,
                        "--frames", frames, "--fixed-step", "--preset", "original", "--set", "texture.hd_pack=off"],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, GT4_N3_SETLOG="1"))
    (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "which_course.py"), str(out / "host.log")])

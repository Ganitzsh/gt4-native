#!/usr/bin/env python3
"""replay_cap.py CAP FRAMES [SIZE]: replay build/caps/CAP with the main host copy (build/lanef_run/gt4_host_main_*.exe,
the newest), every frame captured, into build/sessions/replay_<stem>/. One heavy slot (a host from 70e7fdb on reads
the capture without the file cache)."""
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap = CLONE / "build" / "caps" / sys.argv[1]
frames = int(sys.argv[2])
size = sys.argv[3] if len(sys.argv) > 3 else "320x180"
host = max((CLONE / "build" / "lanef_run").glob("gt4_host_main_*.exe"), key=lambda p: p.stat().st_mtime)
out = CLONE / "build" / "sessions" / f"replay_{cap.stem}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot(f"replay {cap.name} ({frames} frames, every frame captured)")
try:
    r = subprocess.run([str(host), "--headless", "--size", size, "--replay", str(cap), "--frames", str(frames), "--fixed-step",
                        "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                        "--capture", str(out / "f.ppm"), "--capture-every", "1"],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    log(f"replay {cap.name} with {host.name}: exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
    log("slot released")

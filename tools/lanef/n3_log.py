#!/usr/bin/env python3
"""n3_log.py CAP FRAMES TAG: N3 step 0. The N3-logging host (build/lanef_run/gt4_host_n3log.exe, local) replays CAP (read
in place) with lighting.night = enhanced (the scene split and B6's classifier on) and GT4_N3_LOG=1, and keeps the 'N3
candidate' lines and the scene summary in build/sessions/n3_TAG/host.log. One heavy slot (tools/slot.py)."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, tag = sys.argv[1], sys.argv[2], sys.argv[3]
out = CLONE / "build" / "sessions" / f"n3_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot(f"N3 step 0 log: {tag}")
try:
    r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3log.exe"), "--headless", "--size", "640x360", "--replay", cap,
                        "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, GT4_N3_LOG="1", GT4_HOST_RECORD_SCENE="1"))
    (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    log(f"replay {tag} exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
lines = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith("N3 candidate")]
print(f"{tag}: {len(lines)} candidate materials")
for l in lines:
    print("  ", l[len("N3 candidate: "):])

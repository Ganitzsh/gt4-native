#!/usr/bin/env python3
"""f2b_perf.py CAP FRAMES TAG: F2b's PERF, night enhanced against night off in a night race. The clean release host from
a75a6c2 (the capture's game; F2b in it) replays CAP twice with --profile-json, 1280 x 720, fixed step, lighting.night
original then enhanced (scene records on both times), one heavy slot each (tools/slot.py, --min-gb 7); then
tools/perf_report.py on the enhanced run against the original one, the first 600 frames skipped (start-up and the
replay's re-upload window). Output in build/sessions/f2b_perf_TAG/."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, tag = sys.argv[1], sys.argv[2], sys.argv[3]
out = CLONE / "build" / "sessions" / f"f2b_perf_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
for night in ("original", "enhanced"):
    locks = take_slot(f"F2b PERF {tag}: night {night}")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_main_a75a6c2.exe"), "--headless", "--size", "1280x720",
                            "--replay", cap, "--frames", frames, "--fixed-step", "--preset", "original", "--set",
                            "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}",
                            "--profile-json", str(out / f"{night}.jsonl")], capture_output=True, text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW, env=dict(os.environ, GT4_HOST_RECORD_SCENE="1"))
        (out / f"{night}.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"night {night}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
r = subprocess.run([sys.executable, str(CLONE / "tools/perf_report.py"), str(out / "enhanced.jsonl"), "--baseline",
                    str(out / "original.jsonl"), "--skip", "600", "--json", str(out / "report.json")],
                   capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
(out / "report.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
print(r.stdout[-6000:], r.stderr[-1000:])
for night in ("original", "enhanced"):
    for l in (out / f"{night}.log").read_text(encoding="utf-8", errors="replace").splitlines():
        if l.startswith(("F2b course lamps", "Rendered")) or "scene cache:" in l:
            print(night, "|", l[:300] if "scene cache:" not in l else l[l.index("scene cache:"):l.index("scene cache:") + 260])

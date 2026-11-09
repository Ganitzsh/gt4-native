#!/usr/bin/env python3
"""f2_perf.py CAP FRAMES SIZE: replays CAP at SIZE with lighting.night = enhanced and GT4_NIGHT_TEST_LIGHTS=12, and with
it original; prints the scene stage's 'lighting' GPU time per frame over the last 160 frames (--profile-json). One heavy
slot per replay; no validation layers (timings)."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, size = sys.argv[1], int(sys.argv[2]), sys.argv[3]
host = CLONE / "build" / "lanef_run" / "gt4_host_f2perf.exe"
shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
for night in ("original", "enhanced"):
    out = CLONE / "build" / "sessions" / f"f2perf_{night}"
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    locks = take_slot(f"F2 perf replay ({night}, {size})")
    try:
        r = subprocess.run([str(host), "--headless", "--size", size, "--replay", str(CLONE / "build/caps" / cap), "--frames",
                            str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}", "--set", "render.gs=native",
                            "--profile-json", str(out / "profile.json")],
                           capture_output=True, text=True, env=dict(os.environ, GT4_NIGHT_TEST_LIGHTS="12"),
                           creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    lines = (out / "profile.json").read_text().splitlines()[-160:]
    tot, gpu = {}, 0.0
    for l in lines:
        d = json.loads(l)
        gpu += d.get("gpu_ms", 0.0)
        for p in d.get("gs_passes", []):
            tot[p["name"]] = tot.get(p["name"], 0.0) + p["gpu_us"]
    n = max(1, len(lines))
    log(f"{night} at {size}: exit {r.returncode}; frame GPU {gpu / n:.3f} ms; lighting {tot.get('lighting', 0.0) / n:.1f} us; "
        f"draws {tot.get('draws', 0.0) / n:.1f} us")

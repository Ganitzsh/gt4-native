#!/usr/bin/env python3
"""bands_repro.py: the owner's night bands, step 0 (the coordinator, 2026-11-09). The owner's Odin settings
(C:/Users/julie/odin_settings_20261109.ini, copied, with field_mode = blend added under [display]) on both night laps,
replayed headless at the owner's 1280 x 720 by the clean host of current main; a frame every 100 from 600 kept as
PNG in build/sessions/bands_<tag>/ for review. One heavy slot per replay. Never writes the owner's file."""
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

S = CLONE / "build" / "sessions"
src = Path("C:/Users/julie/odin_settings_20261109.ini").read_text(encoding="utf-8")
lines = src.splitlines()
out_lines, displays = [], 0
for l in lines:
    out_lines.append(l)
    if l.strip() == "[display]":
        displays += 1
        if displays == 2:   # the second [display] section (race_frames): field_mode beside it
            out_lines.append("# fields: blend (the coordinator's addition for this run)")
            out_lines.append("field_mode = blend")
if displays < 2:
    out_lines += ["", "[display]", "field_mode = blend"]
ini = S / "bands_settings.ini"
ini.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
host = sorted((CLONE / "build" / "lanef_run").glob("gt4_host_close_*.exe"), key=lambda p: p.stat().st_mtime)[-1]
caps = {"ssr5r": (CLONE / "build/caps/lap_ssr5r.cap", 10200), "csr5": (CLONE / "build/caps/lap_csr5.cap", 9000)}
only = sys.argv[1:]
for tag, (cap, frames) in caps.items():
    if only and tag not in only:
        continue
    out = S / f"bands_{tag}"
    shutil.rmtree(out, ignore_errors=True)
    (out / "ppm").mkdir(parents=True)
    run_ini = out / "settings.ini"   # (a copy per run: an interactive host saves to its settings file)
    shutil.copy2(ini, run_ini)
    locks = take_slot(f"night bands step 0: {tag} with the owner's settings")
    try:
        r = subprocess.run([str(host), "--headless", "--size", "1280x720", "--settings", str(run_ini), "--replay", str(cap),
                            "--frames", str(frames), "--fixed-step", "--capture-frames", f"600-{frames}/100", "--capture-dir",
                            str(out / "ppm")], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
        (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"{tag}: exit {r.returncode} ({host.name})")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    for n, p in fm.frame_files(str(out / "ppm")):
        Image.fromarray(fm.read_ppm(p).astype(np.uint8)).save(out / f"f{n:05d}.png")
    shutil.rmtree(out / "ppm", ignore_errors=True)
    print(f"{tag}: {len(list(out.glob('f*.png')))} frames in {out}", flush=True)

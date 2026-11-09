#!/usr/bin/env python3
"""f2b_look.py CAP FRAME: F2b's look. The logging host (build/lanef_run/gt4_host_n3log.exe) replays CAP (in place) to
FRAME twice, lighting.night original and enhanced, frame FRAME captured at 1280x720; writes a side-by-side PNG (left
off, right on) and the difference's mean over the road region to build/sessions/f2b_look/. One heavy slot per replay."""
import os
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

cap, frame = sys.argv[1], int(sys.argv[2])
root = CLONE / "build" / "sessions" / "f2b_look"
shutil.rmtree(root, ignore_errors=True)
pics = {}
for night in ("original", "enhanced"):
    out = root / night
    out.mkdir(parents=True)
    locks = take_slot(f"F2b look ({night})")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3log.exe"), "--headless", "--size", "1280x720", "--replay", cap,
                            "--frames", str(frame + 2), "--fixed-step", "--preset", "original", "--set",
                            "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}",
                            "--set", "render.gs=native", "--capture-frames", f"{frame}-{frame}", "--capture-dir", str(out)],
                           capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                           env=dict(os.environ, GT4_HOST_RECORD_SCENE="1"))
        (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"replay {night}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    pics[night] = fm.read_ppm(dict(fm.frame_files(str(out)))[frame])
a, b = pics["original"].astype(float), pics["enhanced"].astype(float)
h, w = a.shape[:2]
road = (slice(int(h * 0.6), h), slice(int(w * 0.2), int(w * 0.8)))
lum = lambda x: 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]
print(f"frame {frame}: road mean luminance off {lum(a)[road].mean():.2f}, on {lum(b)[road].mean():.2f}; "
      f"whole frame off {lum(a).mean():.2f}, on {lum(b).mean():.2f}; pixels changed {100 * (np.abs(b - a).max(axis=2) > 2).mean():.1f} %")
side = np.concatenate([pics["original"], pics["enhanced"]], axis=1)
Image.fromarray(side).save(root / f"side_{frame}.png")

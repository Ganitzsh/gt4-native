#!/usr/bin/env python3
"""pack_onoff.py TAG CAP FRAMES_LIST: the owner's settings (build/sessions/bands_settings.ini) with texture.hd_pack=off,
the listed frames at 1280 x 720, beside the hd_pack=on frames already in build/sessions/bands_TAG/ (f#####.png):
pack_on_off_N.png (on | off) in the same folder. One heavy slot."""
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

tag, cap, pick = sys.argv[1], sys.argv[2], sys.argv[3]
out = CLONE / "build" / "sessions" / f"bands_{tag}"
d = out / "off"
shutil.rmtree(d, ignore_errors=True)
d.mkdir(parents=True)
ini = out / "settings_off.ini"
shutil.copy2(CLONE / "build" / "sessions" / "bands_settings.ini", ini)
host = sorted((CLONE / "build" / "lanef_run").glob("gt4_host_close_*.exe"), key=lambda p: p.stat().st_mtime)[-1]
last = max(int(x) for x in pick.split(","))
locks = take_slot(f"HD pack on/off: {tag}")
try:
    r = subprocess.run([str(host), "--headless", "--size", "1280x720", "--settings", str(ini), "--set", "texture.hd_pack=off",
                        "--replay", cap, "--frames", str(last + 2), "--fixed-step", "--capture-frames", pick, "--capture-dir", str(d)],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    log(f"exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
for n, p in fm.frame_files(str(d)):
    on = out / f"f{n:05d}.png"
    if on.exists():
        a = np.asarray(Image.open(on).convert("RGB"))
        b = fm.read_ppm(p).astype(np.uint8)
        Image.fromarray(np.concatenate([a, b], axis=1)).save(out / f"pack_on_off_{n}.png")
        print(out / f"pack_on_off_{n}.png")
shutil.rmtree(d, ignore_errors=True)

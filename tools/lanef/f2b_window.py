#!/usr/bin/env python3
"""f2b_window.py CAP FRAMES_LIST TAG: frames FRAMES_LIST (e.g. 350,400,470,520) at 1280 x 720, lighting.night original
and enhanced, with the a75a6c2 release host (the capture's game); saved as original | enhanced pairs in
build/sessions/f2b_win_TAG/pair_N.png. Two heavy slots in turn."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

cap, pick, tag = sys.argv[1], sys.argv[2], sys.argv[3]
last = max(int(x) for x in pick.split(","))
out = CLONE / "build" / "sessions" / f"f2b_win_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
shots = {}
for night in ("original", "enhanced"):
    d = out / night
    d.mkdir()
    locks = take_slot(f"F2b window {tag}: {night}")
    try:
        subprocess.run([str(CLONE / "build/lanef_run/gt4_host_main_a75a6c2.exe"), "--headless", "--size", "1280x720", "--replay",
                        cap, "--frames", str(last + 2), "--fixed-step", "--preset", "original", "--set",
                        "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set", f"lighting.night={night}",
                        "--capture-frames", pick, "--capture-dir", str(d)], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW, env=dict(os.environ, GT4_HOST_RECORD_SCENE="1"))
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[night] = dict(fm.frame_files(str(d)))
for n in sorted(shots["original"]):
    if n in shots["enhanced"]:
        a, b = fm.read_ppm(shots["original"][n]), fm.read_ppm(shots["enhanced"][n])
        Image.fromarray(np.concatenate([a, b], axis=1)).save(out / f"pair_{n}.png")
        print(out / f"pair_{n}.png")

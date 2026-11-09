#!/usr/bin/env python3
"""n3_zoom.py CAP FRAME IDS NAME [CROP x0,y0,x1,y1]: one frame at 1280 x 720, base against IDS skipped (debug host
gt4_host_f2b0.exe), saved as base | skipped (stacked), cropped if CROP is given (in 1280 x 720 pixels) and scaled 2x,
in build/sessions/n3_zoom/NAME.png. Two replays, one heavy slot each."""
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

cap, frame, ids, name = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
crop = tuple(map(int, sys.argv[5].split(","))) if len(sys.argv) > 5 else None
root = CLONE / "build" / "sessions" / "n3_zoom"
pics = []
for k, skip in (("base", None), ("skip", ids)):
    d = root / f"{name}_{k}"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip:
        env["GT4_R10_SKIPTEX"] = skip
    locks = take_slot(f"N3 zoom {name}: {k}")
    try:
        subprocess.run([os.environ.get("N3_ZOOM_HOST", str(CLONE / "build/lanef_run/gt4_host_f2b0.exe")), "--headless", "--size", "1280x720", "--replay", cap,
                        "--frames", str(frame + 2), "--fixed-step", "--preset", "original", "--set",
                        "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--capture-frames", str(frame),
                        "--capture-dir", str(d)], capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    im = Image.fromarray(fm.read_ppm(dict(fm.frame_files(str(d)))[frame]).astype(np.uint8))
    if crop:
        im = im.crop(crop).resize(((crop[2] - crop[0]) * 2, (crop[3] - crop[1]) * 2), Image.NEAREST)
    pics.append(np.asarray(im))
    shutil.rmtree(d, ignore_errors=True)
Image.fromarray(np.concatenate(pics, axis=0)).save(root / f"{name}.png")
print(root / f"{name}.png")

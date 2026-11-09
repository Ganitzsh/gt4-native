#!/usr/bin/env python3
"""setting_bisect.py TAG CAP FRAME: which of the owner's settings makes the green lane markings (frame FRAME). Replays
with the owner's settings (bands_settings.ini, hd_pack off) and one change each, frame FRAME at 1280 x 720; a sheet of
the road region per variant in build/sessions/bands_TAG/bisect_FRAME.png, with each variant's mean green excess
(G - (R + B) / 2) in the lower half printed. One heavy slot per variant."""
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

tag, cap, frame = sys.argv[1], sys.argv[2], int(sys.argv[3])
out = CLONE / "build" / "sessions" / f"bands_{tag}"
host = sorted((CLONE / "build" / "lanef_run").glob("gt4_host_close_*.exe"), key=lambda p: p.stat().st_mtime)[-1]
variants = [("owner", []), ("filter bilinear", ["texture.filter=bilinear"]), ("anisotropy 1", ["texture.anisotropy=1"]),
            ("auto_lod_bias off", ["texture.auto_lod_bias=off"]), ("render.gs original", ["render.gs=original"]),
            ("aa off", ["render.aa=off"]), ("preset original", ["graphics.preset=original"])]
tiles = []
for name, sets in variants:
    d = out / "bisect_tmp"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir()
    ini = d / "settings.ini"
    shutil.copy2(CLONE / "build" / "sessions" / "bands_settings.ini", ini)
    args = [a for s in ["texture.hd_pack=off"] + sets for a in ("--set", s)]
    if "graphics.preset=original" in sets:
        args = ["--preset", "original", "--set", "texture.hd_pack=off"]
    locks = take_slot(f"settings bisect {tag}: {name}")
    try:
        subprocess.run([str(host), "--headless", "--size", "1280x720", "--settings", str(ini), *args, "--replay", cap, "--frames",
                        str(frame + 2), "--fixed-step", "--capture-frames", str(frame), "--capture-dir", str(d)],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    files = dict(fm.frame_files(str(d)))
    if frame not in files:
        print(f"{name}: no frame")
        continue
    im = fm.read_ppm(files[frame]).astype(np.float32)
    low = im[im.shape[0] // 2:]
    excess = float((low[..., 1] - (low[..., 0] + low[..., 2]) / 2).clip(0).mean())
    print(f"{name:20}: green excess in the lower half {excess:.2f}", flush=True)
    tile = Image.fromarray(im[340:660, 160:1120].astype(np.uint8)).resize((480, 160))
    ImageDraw.Draw(tile).text((4, 4), f"{name} ({excess:.1f})", fill=(255, 255, 0))
    tiles.append(tile)
    shutil.rmtree(d, ignore_errors=True)
sheet = Image.new("RGB", (960, 160 * ((len(tiles) + 1) // 2)))
for k, t in enumerate(tiles):
    sheet.paste(t, ((k % 2) * 480, (k // 2) * 160))
sheet.save(out / f"bisect_{frame}.png")
print(out / f"bisect_{frame}.png")

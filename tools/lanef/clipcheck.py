#!/usr/bin/env python3
"""clipcheck.py: the owner's two playtest clips (2026-11-09) on the desktop. lap_ssr5f_boot (S.S. Route 5 forward at
night, from game start) replayed with the dump's own settings (odin_playtest_20261110/dumps/*/settings.ini), twice:
lighting.night = enhanced (as played) and = original. Frames 9,650-9,900 every 5 (the overpass with the amber bands,
clip 225924's spot) and 10,050-10,700 every 50 (the grandstand curve, clip 230003's). Sheets per spot, enhanced over
original, in build/sessions/clipcheck/, with the road's mean luma and magenta per frame printed. Two heavy-both slots."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import log, CLONE  # noqa: E402  (installs the slot log)
import frame_metrics as fm  # noqa: E402
import slot  # noqa: E402

CAP = CLONE / "build/caps/lap_ssr5f_boot.cap"
HOST = CLONE / "build/lanef_run/gt4_host_boot_790c25c.exe"
DUMP = sorted(Path("C:/Users/julie/odin_playtest_20261110/dumps").glob("*/settings.ini"))[0]
OUT = CLONE / "build/sessions/clipcheck"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
frames = "9650-9900/5,10050-10700/50"
shots = {}
for night in ("enhanced", "original"):
    d = OUT / night
    d.mkdir()
    ini = d / "settings.ini"
    shutil.copy2(DUMP, ini)
    locks = slot.take(["heavy-both"], f"lane F: clip check, lap_ssr5f_boot at the dump's settings, night {night}", min_gb=7.0)
    try:
        r = subprocess.run([str(HOST), "--headless", "--size", "1280x720", "--settings", str(ini), "--set",
                            f"lighting.night={night}", "--replay", str(CAP), "--frames", "10700", "--fixed-step",
                            "--capture-frames", frames, "--capture-dir", str(d)],
                           capture_output=True, text=True, env=dict(os.environ, GT4_HOST_RECORD_SCENE="1"),
                           creationflags=subprocess.CREATE_NO_WINDOW)
        log(f"{night}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shots[night] = dict(fm.frame_files(str(d)))


def road(im):
    h, w = im.shape[:2]
    r = im[int(h * .62):int(h * .72), int(w * .1):int(w * .9)].astype(np.float32)
    return (0.2126 * r[..., 0] + 0.7152 * r[..., 1] + 0.0722 * r[..., 2]).mean(), (r[..., 0].mean() + r[..., 2].mean()) / 2 - r[..., 1].mean()


for name, sel in (("overpass", range(9650, 9901, 5)), ("grandstand", range(10050, 10701, 50))):
    print(name)
    rows = []
    for n in sel:
        if n in shots["enhanced"] and n in shots["original"]:
            e, o = (fm.read_ppm(shots[k][n]).astype(np.uint8) for k in ("enhanced", "original"))
            (ye, me), (yo, mo) = road(e), road(o)
            print(f"  {n}: road luma enhanced {ye:.0f} original {yo:.0f}; magenta enhanced {me:+.1f} original {mo:+.1f}")
            rows.append(np.concatenate([e, o], axis=0))
    picks = rows[:: max(1, len(rows) // 6)][:6]
    if picks:
        Image.fromarray(np.concatenate(picks, axis=1)).resize((320 * len(picks), 360)).save(OUT / f"{name}.png")

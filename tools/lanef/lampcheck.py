#!/usr/bin/env python3
"""lampcheck.py: the architect's check before the lamp cache (2026-11-09). lap_csr5 600-5000 every 100 on the
lamp-count host (gt4_host_n3knobs3.exe: GT4_N3_LAMPLOG=1 prints, per frame, the course lamps F2b sends and how many
are within 20 m of the camera, behind it, within 30 m in front), B (enhanced, v4's skip) and C (enhanced, no skip),
both with the batwing at 65 degrees x6, against the cached GS path A. Per frame: the skipped patch's darker share in
the near road (the bottom third of the frame) and in the rest, beside the lamp counts; one frame (VBlank given) with
its lamps' screen positions marked. Two heavy slots (one replay each)."""
import os
import re
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

CAP = CLONE / "build/caps/lap_csr5.cap"
HOST = CLONE / "build/lanef_run/gt4_host_n3knobs3.exe"
OUT = CLONE / "build/sessions/lampcheck"
A = CLONE / "build/sessions/n3dark_A_lap_csr5_600_5000_100"
MARK = int(sys.argv[1]) if len(sys.argv) > 1 else 3600   # the replay frame to mark
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
knobs = {"GT4_N3_GLOW_BATWING": "65", "GT4_N3_GLOW_RANGE": "40", "GT4_N3_GLOW_INTENSITY": "6"}
logs = {}
for key, extra in (("B", {"GT4_N3_V4SKIP": "1", "GT4_N3_LOG": "1"}), ("C", {})):
    d = OUT / key
    d.mkdir()
    env = dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_N3_LAMPLOG="1", **knobs, **extra)
    locks = take_slot(f"lane F: N3 lamp check {key}")
    try:
        r = subprocess.run([str(HOST), "--headless", "--size", "640x360", "--replay", str(CAP), "--frames", "5000",
                            "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
                            "texture.hd_pack=off", "--set", "lighting.night=enhanced", "--capture-frames",
                            "600-5000/100", "--capture-dir", str(d)], capture_output=True, text=True, env=env,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        logs[key] = r.stdout + r.stderr
        log(f"{key}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
(OUT / "B_lamps.log").write_text("\n".join(l for l in logs["B"].splitlines() if l.startswith("F2b lamp")),
                                 encoding="utf-8")
pat = re.compile(r"F2b lamps: vblank (\d+) view (\d+) n (\d+) near20 (\d+) behind (\d+) front30 (\d+) nearest (\S+)")
lines = [tuple(map(float, m.groups())) for m in map(pat.match, logs["B"].splitlines()) if m]
# one line per scene execution; the main view's lines carry the most lamps: keep the largest n per VBlank
per_vb = {}
for vb, view, n, near20, behind, front30, nearest in lines:
    if vb not in per_vb or n > per_vb[vb][1]:
        per_vb[vb] = (view, n, near20, behind, front30, nearest)
vbs = sorted(per_vb)
first_vb = vbs[0] if vbs else 0
print(f"lamp lines: {len(lines)} over {len(per_vb)} VBlanks; first VBlank {first_vb:.0f}")
y = lambda im: 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]
fa, fb, fc = (dict(fm.frame_files(str(p))) for p in (A, OUT / "B", OUT / "C"))
rows = []
for n in sorted(set(fa) & set(fb) & set(fc)):
    a, b, c = (fm.read_ppm(f[n]).astype(np.float32) for f in (fa, fb, fc))
    patch = np.abs(b - c).max(axis=2) > 2
    dd = y(b) - y(a)
    H = patch.shape[0]
    near = patch.copy()
    near[: int(H * 2 / 3)] = False
    far = patch & ~near
    sh = lambda m: float((dd[m] < -4).mean()) if m.sum() > 50 else float("nan")
    vb = first_vb + n - 600   # the first logged scene frame is near the first replay frame captured (approximate)
    cand = [v for v in vbs if abs(v - (vbs[0] + n - 1)) <= 2]
    lam = per_vb[cand[0]] if cand else None
    rows.append((n, sh(near), sh(far), lam))
print("frame | near-road darker | far darker | lamps n, within 20 m, behind, in front within 30 m, nearest (m)")
for n, sn, sf, lam in rows:
    ls = f"{lam[1]:.0f}, {lam[2]:.0f}, {lam[3]:.0f}, {lam[4]:.0f}, {lam[5]:.1f}" if lam else "-"
    print(f"{n:5} | {sn * 100:5.1f} % | {sf * 100:5.1f} % | {ls}")
ok = [r for r in rows if r[3] and r[1] == r[1]]
if ok:
    for lo, hi, name in ((0.3, 1.01, "near road over 30 % darker"), (0.0, 0.05, "near road under 5 % darker")):
        sel = [r for r in ok if lo <= r[1] < hi]
        if sel:
            print(f"{name}: {len(sel)} frames, mean lamps within 20 m {np.mean([r[3][2] for r in sel]):.2f}, "
                  f"in front within 30 m {np.mean([r[3][4] for r in sel]):.2f}, nearest {np.median([r[3][5] for r in sel]):.1f} m")
# frame MARK with its lamps on screen
marks = [l for l in logs["B"].splitlines() if l.startswith("F2b lamp at:")]
if MARK in fb and marks:
    im = Image.fromarray(fm.read_ppm(fb[MARK]).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    pts = [tuple(map(float, re.search(r"screen (\S+) (\S+)", l).groups())) for l in marks]
    span = max(1.0, max(p[0] for p in pts) * 1.05)
    sx = im.width / 1280.0 if span > 700 else im.width / 640.0   # the scene target's pixels: 640 or 1280 wide
    for x, yy in pts:
        dr.ellipse([x * sx - 4, yy * sx * 0.8 - 4, x * sx + 4, yy * sx * 0.8 + 4], outline=(255, 255, 0))
    im.save(OUT / f"lamps_{MARK}.png")
    print(f"marked {len(pts)} lamps on frame {MARK}: {OUT / f'lamps_{MARK}.png'} (screen scale assumed; check by eye)")
for k in "BC":
    shutil.rmtree(OUT / k, ignore_errors=True)

#!/usr/bin/env python3
"""lapboot_check.py: lap_ssr5r_boot's replay check for its CAPTURES row. One replay (heavy-both: an 8 GB capture) on
the recording's own host (gt4_host_boot_95a639b.exe), headless 1280 x 720, preset original, interpolation and HD pack
off, frames 1900-20200 every 100 (lit or not: over 5 % of pixels not black) and 9700-10200 every frame (to place the
live shots: live00000 is the first shot after the recording's start point). Then each live shot against the replay
frame of the same VBlank: mean absolute difference and the share of pixels off by more than 8 levels."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

T = Path(__file__).resolve().parent
sys.path.insert(0, str(T))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402
import slot  # noqa: E402

cap = CLONE / "build/caps/lap_ssr5r_boot.cap"
host = CLONE / "build/lanef_run/gt4_host_boot_95a639b.exe"
import json
ev = [json.loads(l) for l in (CLONE / "build/sessions/lap_ssr5r_boot/events.jsonl").read_text(encoding="utf-8").splitlines()
      if l.startswith("{") and "\"capture\"" in l]
shot_at = {e["file"][:-4]: e["frame"] for e in ev if e.get("type") == "capture" and e["file"].startswith("live")}
OFF = int(os.environ.get("LAPBOOT_OFFSET", "0"))   # replay VBlank = session frame + OFF (frames skipped at boot)
W = int(os.environ.get("LAPBOOT_WINDOW", "1"))
pick = sorted(shot_at.items(), key=lambda kv: kv[1])
pick = pick[::max(1, len(pick) // 12)] if OFF else pick
shot_at = dict(pick)
grid = ("" if OFF else "1900-20200/100,") + ",".join(f"{f + OFF - W}-{f + OFF + W}" for _, f in pick if f + OFF + W <= 20200)
out = CLONE / "build/sessions" / ("lapboot_align" if OFF else "lapboot_check")
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = slot.take(["heavy-both"], "lane F: lap_ssr5r_boot replay check (8 GB capture)", min_gb=7.0)
try:
    r = subprocess.run([str(host), "--headless", "--size", "1280x720", "--replay", str(cap), "--frames", "20200",
                        "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
                        "texture.hd_pack=off", "--capture-frames", grid, "--capture-dir", str(out)],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    log(f"replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
frames = dict(fm.frame_files(str(out)))
read = lambda p: fm.read_ppm(str(p)).astype(np.float32)
sampled = [n for n in range(1900, 20201, 100) if n in frames]
lit = [n for n in sampled if (read(frames[n]).max(axis=2) > 8).mean() > 0.05]
dark = sorted(set(sampled) - set(lit))
print(f"sampled {len(sampled)} (1900-20200 every 100), lit {len(lit)}; not lit: {dark}")
live = sorted((T.parent / "sessions/lap_ssr5r_boot/frames").glob("live*.ppm"))
rows = []
for p in live:
    f = shot_at.get(p.stem)
    if f is None:
        continue
    cands = [n for n in range(f + OFF - W, f + OFF + W + 1) if n in frames]
    if not cands:
        continue
    a = read(p)
    diffs = {n: float(np.abs(a - read(frames[n])).mean()) for n in cands}
    n = min(diffs, key=diffs.get)
    b = read(frames[n])
    rows.append((p.stem, f, n, diffs[n], float((np.abs(a - b).max(axis=2) > 8).mean())))
offsets = sorted({r[2] - r[1] for r in rows})
print(f"live shots placed: {len(rows)} of {len(live)}; replay frame minus session frame: {offsets}")
if rows:
    print(f"live shots against the replay at the same VBlank: {len(rows)}; mean |diff| median "
          f"{np.median([r[3] for r in rows]):.2f}, max {max(r[3] for r in rows):.2f}; pixels off by over 8: median "
          f"{np.median([r[4] for r in rows]) * 100:.2f} %, max {max(r[4] for r in rows) * 100:.2f} %")
    for r in sorted(rows, key=lambda r: -r[4])[:5]:
        print(f"    {r[0]} = replay frame {r[2]}: mean |diff| {r[3]:.2f}, off by over 8 {r[4] * 100:.2f} %")
else:
    print("no live shot placed")
for n in list(frames):
    if not (n in sampled and n % 1000 == 0) and n not in {r[2] for r in sorted(rows, key=lambda r: -r[4])[:5]}:
        Path(frames[n]).unlink()

#!/usr/bin/env python3
"""n2_reopen.py CAP FRAMES LAP TAG: PLAN N2's reopening test on a night capture. The clean host (gt4_host_close_*.exe,
newest) replays CAP on the GS path at original, every frame captured at 320 x 180 (one heavy slot), then
tools/frame_metrics.py step with r5_ss_ab's notes (the B-spec view's scene rectangle and HUD) and level_threshold 0.05.
A hit reopens N2 when: the level shift is at least 5 %, one frame carries at least 70 % of it (the largest single
frame-to-frame change within 3 frames of it, against the shift), it is not a camera cut, and a hit comes at the same
track position on the next lap (LAP frames later, +-150). Frames from 600 on (start-up and the re-upload window).
The frames are deleted afterwards. Output in build/sessions/n2_TAG/."""
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, lap, tag = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
out = CLONE / "build" / "sessions" / f"n2_{tag}"
shutil.rmtree(out, ignore_errors=True)
(out / "f").mkdir(parents=True)
host = sorted((CLONE / "build" / "lanef_run").glob("gt4_host_close_*.exe"), key=lambda p: p.stat().st_mtime)[-1]
locks = take_slot(f"N2 reopening check: {tag}")
try:
    r = subprocess.run([str(host), "--headless", "--size", "320x180", "--replay", cap, "--frames", str(frames), "--fixed-step",
                        "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                        "--capture", str(out / "f" / "f.ppm"), "--capture-every", "1"], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    log(f"replay exit {r.returncode} ({host.name})")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
(out / "f" / "f.ppm").unlink(missing_ok=True)   # (the last frame again, unnumbered)
notes = json.loads((CLONE / "build/caps/r5_ss_ab.notes.json").read_text(encoding="utf-8"))
mine = {"scene": notes["scene"], "hud": notes["hud"], "cuts": []}
(out / "notes.json").write_text(json.dumps(mine), encoding="utf-8")
m = subprocess.run([sys.executable, str(CLONE / "tools/frame_metrics.py"), str(out / "f"), "step", "--notes", str(out / "notes.json"),
                    "--set", "level_threshold=0.05", "--json", str(out / "step.json")], capture_output=True, text=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)
(out / "frame_metrics.txt").write_text(m.stdout + m.stderr, encoding="utf-8")
res = json.loads((out / "step.json").read_text(encoding="utf-8"))
step = res.get("step", res)
series = res.get("series", [])
shifts = [s for s in step.get("level_shifts", []) if s["frame"] >= 600]
logy = {}
for row in series:
    if isinstance(row, dict) and "frame" in row:
        logy[row["frame"]] = row.get("logY")
print(f"{len(shifts)} level shifts of 5 % or more after frame 600 (largest frame change anywhere: {step.get('largest')})")
reopen = []
for s in shifts:
    f, size = s["frame"], s["change"]
    one = 0.0
    for g in range(f - 3, f + 4):
        if g in logy and g - 1 in logy and logy[g] is not None and logy[g - 1] is not None:
            one = max(one, abs(math.expm1(logy[g] - logy[g - 1])))
    share = one / size if size else 0.0
    again = [t for t in shifts if abs(t["frame"] - (f + lap)) <= 150 and t["direction"] == s["direction"]]
    verdict = share >= 0.7 and bool(again)
    print(f"  frame {f}: {s['direction']} {size * 100:.1f} %, largest one-frame change {one * 100:.1f} % ({share * 100:.0f} %); "
          f"next lap {'frame ' + str(again[0]['frame']) if again else 'none'}: {'REOPENS' if verdict else 'no'}")
    if verdict:
        reopen.append(f)
print("N2:", "reopens at " + str(reopen) if reopen else "stays parked (no one-frame level shift repeating on the next lap)")
if not logy:
    print("note: no per-frame series found in step.json; keys:", list(step.keys())[:12])
shutil.rmtree(out / "f", ignore_errors=True)

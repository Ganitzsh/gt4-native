#!/usr/bin/env python3
"""lamplist.py: the architect's check (b) for clip A's pop (2026-11-10). Builds the local debug host in
C:/Users/julie/gt4-lane-F-dbg (GT4_N3_LAMPLIST=FROM-TO lists every course lamp and billboard candidate of the main view
per VBlank: g = the game's record, p = a promoted billboard, b = a candidate left as one; id = the object or the
instance serial), replays lap_ssr5f_boot to VBlank TO at night enhanced (the list and every frame) and original (every
frame), then marks every frame where a billboard flush's promoted lamps (one instance serial = one section's set) leave
at once while near the camera, beside the road's frame-to-frame change in both modes. Three heavy slots in turn.
    py lamplist.py [FROM TO] [--no-build]"""
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
args = [a for a in sys.argv[1:] if not a.startswith("--")]
FROM, TO = (int(args[0]), int(args[1])) if len(args) == 2 else (9600, 9950)
DBG = Path("C:/Users/julie/gt4-lane-F-dbg")
HOST = CLONE / "build/lanef_run/gt4_host_lamplist.exe"
CAP = CLONE / "build/caps/lap_ssr5f_boot.cap"
OUT = CLONE / "build/sessions" / f"lamplist_{FROM}_{TO}"
ANALYSE = "--analyse" in sys.argv   # the logs and frames of the last run, nothing replayed
if not ANALYSE:
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)


def slot_run(what, argv, env=None, out=None):
    locks = take_slot(f"lane F: lamp list (pop check b): {what}")
    try:
        r = subprocess.run(argv, capture_output=True, text=True, creationflags=NW, env=dict(os.environ, **(env or {})))
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    if out:
        out.write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    return r


if "--no-build" not in sys.argv and not ANALYSE:
    cmd = OUT / "build.cmd"
    cmd.write_text(rf"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d {DBG}
cmake --build build\host-dbg --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    b = slot_run("debug host build", ["cmd.exe", "/d", "/c", str(cmd)], out=OUT / "build.log")
    print(f"build: exit {b.returncode}", flush=True)
    if b.returncode:
        print((b.stdout + b.stderr)[-3000:])
        raise SystemExit(1)
    shutil.copy2(DBG / "build/host-dbg/gt4_host.exe", HOST)

common = ["--headless", "--size", "640x360", "--replay", str(CAP), "--frames", str(TO + 2), "--fixed-step",
          "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
          "--capture-frames", f"{FROM}-{TO + 1}"]
shots = {}
for night in ("enhanced", "original"):
    d = OUT / night
    if ANALYSE:
        shots[night] = dict(fm.frame_files(str(d)))
        continue
    d.mkdir()
    env = {"GT4_HOST_RECORD_SCENE": "1"}
    if night == "enhanced":
        env.update(GT4_N3_LAMPLOG="1", GT4_N3_LAMPLIST=f"{FROM}-{TO + 1}")
    r = slot_run(f"replay {night}", [str(HOST), *common, "--set", f"lighting.night={night}", "--capture-dir", str(d)],
                 env, OUT / f"host_{night}.log")
    print(f"replay {night}: exit {r.returncode}", flush=True)
    shots[night] = dict(fm.frame_files(str(d)))

# the lists: VBlank -> [(kind, id, x, y, z, sx, sy)]
frames = defaultdict(list)
pat = re.compile(r"F2b lamp: vblank (\d+) view \d+ kind (\w) id ([0-9a-f]+) pos (\S+) (\S+) (\S+) w \S+ sx (\S+) sy (\S+)")
for line in (OUT / "host_enhanced.log").read_text(encoding="utf-8", errors="replace").splitlines():
    m = pat.match(line)
    if m:
        g = m.groups()
        frames[int(g[0])].append((g[1], g[2], *map(float, g[3:8])))
print(f"list lines over {len(frames)} VBlanks", flush=True)


def present(lamp, later, tol=3.0):
    """A lamp of frame N still there in frame N+1: any lamp (any kind) within tol metres in view space."""
    x, y, z = lamp[2:5]
    return any((o[2] - x) ** 2 + (o[3] - y) ** 2 + (o[4] - z) ** 2 <= tol * tol for o in later)


def road_change(name, n):
    """Mean |frame n+1 - frame n| over the lower half (the road), both images at 640x360."""
    a, b = shots[name].get(n), shots[name].get(n + 1)
    if not a or not b:
        return float("nan")
    a, b = (fm.read_ppm(p).astype(np.float32) for p in (a, b))
    h = a.shape[0]
    return float(np.abs(b[h // 2:] - a[h // 2:]).mean())


vbs = sorted(frames)
events = []
for n, n1 in zip(vbs, vbs[1:]):
    if n1 != n + 1:
        continue
    now, nxt = frames[n], frames[n1]
    groups = defaultdict(list)
    for lamp in now:
        if lamp[0] == "p":
            groups[lamp[1]].append(lamp)
    for gid, lamps in groups.items():
        gone = [l for l in lamps if not present(l, nxt)]
        near = min((l[2] ** 2 + l[3] ** 2 + l[4] ** 2) ** 0.5 for l in lamps)
        if len(gone) >= 2 and len(gone) == len(lamps) and near < 40.0:
            onscreen = sum(1 for l in lamps if l[4] < -0.1 and 0 <= l[5] <= 640 and 0 <= l[6] <= 360)
            events.append((n, gid, len(lamps), near, onscreen))
    cand_now = sum(1 for l in now if l[0] in "pb")
    cand_next = sum(1 for l in nxt if l[0] in "pb")
    if cand_now - cand_next >= 4:
        log(f"VBlank {n}->{n1}: GT4's billboard candidates {cand_now} -> {cand_next}")
print(f"section drops (a flush's promoted lamps all gone in the next frame, nearest < 40 m): {len(events)}", flush=True)
for n, gid, k, near, onscreen in events:
    print(f"  VBlank {n}->{n + 1}: flush {gid}, {k} lamps, nearest {near:.1f} m, {onscreen} on screen; "
          f"road change enhanced {road_change('enhanced', n):.2f}, original {road_change('original', n):.2f} "
          f"(the window's median: enhanced {np.median([road_change('enhanced', v) for v in range(FROM, TO, 10)]):.2f})",
          flush=True)
# the counts per VBlank, for a plot by eye
with open(OUT / "counts.tsv", "w", encoding="utf-8") as f:
    f.write("vblank\tgame\tpromoted\tcandidates\tpromoted_near30\n")
    for n in vbs:
        L = frames[n]
        f.write(f"{n}\t{sum(l[0] == 'g' for l in L)}\t{sum(l[0] == 'p' for l in L)}\t{sum(l[0] == 'b' for l in L)}\t"
                f"{sum(l[0] == 'p' and (l[2]**2 + l[3]**2 + l[4]**2) ** 0.5 < 30 for l in L)}\n")
print(f"counts: {OUT / 'counts.tsv'}", flush=True)

#!/usr/bin/env python3
"""carlamp_gates.py: the kSrcCarLamp change's gates (2026-11-10), on the worktree C:/Users/julie/gt4-lane-F-carlamp:
  1. a host from an empty folder there (BUILD_TESTING on) and the full ctest;
  2. ORIG: race7, race_grid43, race_boot at original against lane B's orig_r6 hash lists;
  3. the review images: lap_ssr5f_boot at the grandstand curve (frames 10,050-10,700 every 50) at the playtest dump's
     settings with night enhanced, the new host against main's (gt4_host_boot_790c25c.exe): a sheet, before over
     after, and the road's magenta per frame.
One heavy slot per job."""
import hashlib
import os
import re
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

NW = subprocess.CREATE_NO_WINDOW
WT = Path("C:/Users/julie/gt4-lane-F-carlamp")
LANE_B = Path("C:/Users/julie/gt4-lane-B/build/caps")
head = subprocess.run(["git", "-C", str(WT), "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True).stdout.strip()
if subprocess.run(["git", "-C", str(WT), "status", "--porcelain", "--untracked-files=no"], capture_output=True, text=True).stdout.strip():
    raise SystemExit("the worktree isn't clean")
OUT = CLONE / "build/sessions" / f"carlamp_gates_{head}"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
cmd = OUT / "build.cmd"
cmd.write_text(rf"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d {WT}
if exist build\host rmdir /s /q build\host
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON || exit /b 1
cmake --build build\host --parallel 6 || exit /b 1
ctest --test-dir build\host --output-on-failure || exit /b 1
""", encoding="ascii")
locks = take_slot(f"lane F: kSrcCarLamp change: host from an empty folder + ctest at {head}")
try:
    b = subprocess.run(["cmd.exe", "/d", "/c", str(cmd)], capture_output=True, text=True, creationflags=NW)
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
(OUT / "build.log").write_text(b.stdout + b.stderr, encoding="utf-8", errors="replace")
tests = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", b.stdout)
print(f"1. build from empty + ctest: exit {b.returncode}; {tests.group(0) if tests else 'no ctest summary'}", flush=True)
if b.returncode:
    print(b.stdout[-2500:])
    raise SystemExit(1)
host = CLONE / "build/lanef_run" / f"gt4_host_carlamp_{head}.exe"
shutil.copy2(WT / "build/host/gt4_host.exe", host)


def replay(exe, name, cap, frames, args, env=None):
    locks = take_slot(f"lane F: kSrcCarLamp change gates: {name}")
    try:
        r = subprocess.run([str(exe), "--headless", "--replay", str(cap), "--frames", str(frames), "--fixed-step", *args],
                           capture_output=True, text=True, creationflags=NW, env=dict(os.environ, **(env or {})))
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    return r.stdout + r.stderr


ok_all = True
for cap, frames, every in (("race7", 100, 1), ("race_grid43", 700, 1), ("race_boot", 4914, 10)):
    d = OUT / f"orig_{cap}"
    d.mkdir()
    replay(host, f"orig {cap}", LANE_B / f"{cap}.cap", frames,
           ["--size", "1280x960", "--preset", "original", "--set", "texture.hd_pack=off", "--set", "display.frame_interpolation=off",
            "--set", "debug.overlay=off", "--capture", str(d / "f.ppm"), "--capture-every", str(every)])
    want = {}
    for line in (LANE_B / f"orig_r6_{cap}.sha256").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            n, h = line.split()
            want[n] = h.upper()
    got = {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in d.glob("f*.ppm")}
    missing = [n for n in want if n not in got]
    diff = [n for n in want if n in got and got[n] != want[n]]
    ok = not missing and not diff
    ok_all &= ok
    print(f"2. ORIG {cap}: {len(want)} listed, {len(want) - len(missing) - len(diff)} identical, {len(diff)} differ, "
          f"{len(missing)} missing: {'pass' if ok else 'FAIL'}", flush=True)
    shutil.rmtree(d, ignore_errors=True)
print("   ORIG:", "pass" if ok_all else "FAIL", flush=True)

dump = sorted(Path("C:/Users/julie/odin_playtest_20261110/dumps").glob("*/settings.ini"))[0]
shots = {}
for name, exe in (("before", CLONE / "build/lanef_run/gt4_host_boot_790c25c.exe"), ("after", host)):
    d = OUT / name
    d.mkdir()
    shutil.copy2(dump, d / "settings.ini")
    replay(exe, f"grandstand {name}", CLONE / "build/caps/lap_ssr5f_boot.cap", 10700,
           ["--size", "1280x720", "--settings", str(d / "settings.ini"), "--set", "lighting.night=enhanced",
            "--capture-frames", "10050-10700/50", "--capture-dir", str(d)], {"GT4_HOST_RECORD_SCENE": "1"})
    shots[name] = dict(fm.frame_files(str(d)))
rows = []
for n in sorted(set(shots["before"]) & set(shots["after"])):
    a, b = (fm.read_ppm(shots[k][n]).astype(np.uint8) for k in ("before", "after"))
    h, w = a.shape[:2]
    mag = lambda im: float((im[int(h * .62):int(h * .72), int(w * .1):int(w * .9)].astype(np.float32) @ np.array([0.5, -1.0, 0.5])).mean())
    print(f"3. {n}: road magenta (R+B)/2-G before {mag(a):+.1f}, after {mag(b):+.1f}", flush=True)
    rows.append(np.concatenate([a, b], axis=0))
picks = rows[:: max(1, len(rows) // 6)][:6]
if picks:
    Image.fromarray(np.concatenate(picks, axis=1)).resize((320 * len(picks), 360)).save(OUT / "grandstand_before_after.png")
    print(f"   sheet: {OUT / 'grandstand_before_after.png'}", flush=True)

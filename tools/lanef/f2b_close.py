#!/usr/bin/env python3
"""f2b_close.py: F2b's closing checks with a clean host of current main (the clone's tree must be clean; built here,
copied to build/lanef_run/gt4_host_close_<hash>.exe, checked to hold none of the local debug strings).
1. ORIG: race7 (100 frames), race_grid43 (700), race_boot (4,914, every tenth) at 1280x960, preset original, against
   lane B's orig_r6 hash lists (C:/Users/julie/gt4-lane-B/build/caps, read in place; made at 7f58836).
2. Records off: lap_csr5.cap at lighting.night = original (the host wants no scene records: no split, no cache, no
   lamp pass): the scene cache's, glow class's and lamp pass's counters must read zero. And night enhanced on
   r5_sections.cap (recorded without scene records): the lamp pass must find no candidates.
3. B6's cost on a night capture: lap_csr5.cap at night enhanced with GT4_SCENE_CACHE_LOG=1; the p95 and max of the
   scene cache's thread CPU a scene frame.
Each replay takes one heavy slot (tools/slot.py, --min-gb 7). Output in build/sessions/f2b_close/."""
import hashlib
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
OUT = CLONE / "build" / "sessions" / "f2b_close"
OUT.mkdir(parents=True, exist_ok=True)
LANE_B = Path("C:/Users/julie/gt4-lane-B/build/caps")
CAPS = CLONE / "build" / "caps"


def git(*a):
    return subprocess.run(["git", "-C", str(CLONE), *a], capture_output=True, text=True, creationflags=NW).stdout.strip()


status = git("status", "--porcelain", "--untracked-files=no")
if status:
    raise SystemExit("the tree isn't clean: " + status)
head = git("rev-parse", "--short=7", "HEAD")
host = CLONE / "build" / "lanef_run" / f"gt4_host_close_{head}.exe"
if not host.exists():
    build = os.path.join(os.environ["TEMP"], "lanef_close_build.cmd")
    Path(build).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    locks = take_slot(f"F2b close: clean host build at {head}")
    try:
        b = subprocess.run(["cmd.exe", "/d", "/c", build], capture_output=True, text=True, creationflags=NW)
        log(f"host build exit {b.returncode}")
        if b.returncode:
            print(b.stdout[-3000:])
            raise SystemExit(1)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    exe = (CLONE / "build/host-windows-Release/gt4_host.exe").read_bytes()
    dirty = [k for k in (b"N3 orient", b"N3 footprint", b"N3 v4:", b"F2b drop:", b"F2b gather totals", b"GT4_R10_SKIPTEX",
                         b"N3 candidate") if k in exe]
    if dirty:
        raise SystemExit(f"the built host still holds debug code {dirty}: not clean")
    shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
log(f"clean host {host.name}")


def replay(name, cap, frames, args, env=None):
    locks = take_slot(f"F2b close: {name}")
    try:
        r = subprocess.run([str(host), "--headless", "--replay", str(cap), "--frames", str(frames), "--fixed-step", *args],
                           capture_output=True, text=True, creationflags=NW, env=dict(os.environ, **(env or {})))
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    (OUT / f"{name}.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    return r.stdout + r.stderr


# 1. ORIG
orig_ok = True
for cap, frames, every in (("race7", 100, 1), ("race_grid43", 700, 1), ("race_boot", 4914, 10)):
    d = OUT / f"orig_{cap}"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir()
    replay(f"orig_{cap}", LANE_B / f"{cap}.cap", frames,
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
    orig_ok &= ok
    print(f"ORIG {cap}: {len(want)} frames listed, {len(want) - len(missing) - len(diff)} identical, {len(diff)} differ, "
          f"{len(missing)} missing: {'pass' if ok else 'FAIL ' + str((diff + missing)[:8])}", flush=True)
    shutil.rmtree(d, ignore_errors=True)
print("ORIG:", "pass" if orig_ok else "FAIL", flush=True)

# 2. records off
common = ["--size", "640x360", "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"]
text = replay("off_lapc_night_original", CAPS / "lap_csr5.cap", 9000, common + ["--set", "lighting.night=original"])
m = re.search(r"scene cache: (\d+) instances", text)
g = re.search(r"glow class: (\d+) materials judged", text)
lamps = re.search(r"F2b course lamps: .*", text)
split = re.search(r"scene front end: [^;]*", text)
print(f"records off (lap_csr5, night original): scene cache instances {m.group(1) if m else 'no line'}; glow class judged "
      f"{g.group(1) if g else 'no line'}; lamp pass {'RAN: ' + lamps.group(0)[:160] if lamps else 'no line (0 executions)'}; "
      f"{split.group(0)[:120] if split else 'no scene front-end line'}", flush=True)
text = replay("off_sections_night_enhanced", CAPS / "r5_sections.cap", 600, common + ["--set", "lighting.night=enhanced"])
lamps = re.search(r"F2b course lamps: (\d+) night executions, (\d+) billboard candidates: (\d+) lamps", text)
print(f"no records in the capture (r5_sections, night enhanced): "
      f"{'executions %s, candidates %s, lamps %s' % lamps.groups() if lamps else 'no lamp line (0 executions)'}", flush=True)

# 3. B6's thread CPU on the night capture
text = replay("cache_lapc", CAPS / "lap_csr5.cap", 9000, common + ["--set", "lighting.night=enhanced"],
              {"GT4_SCENE_CACHE_LOG": "1", "GT4_HOST_RECORD_SCENE": "1"})
cpu = [float(x) for x in re.findall(r"scene cache: vblank \d+: .*?\(cpu ([\d.]+) us\)", text)]
skip = 600   # the start and the re-upload window
if cpu:
    xs = sorted(cpu[skip:] if len(cpu) > skip else cpu)
    p95 = xs[min(len(xs) - 1, int(0.95 * len(xs)))]
    print(f"B6 on lap_csr5 (night enhanced): {len(cpu)} scene-frame lines, after {skip}: mean {statistics.mean(xs):.1f} us, "
          f"p95 {p95:.1f} us, max {xs[-1]:.1f} us; margin under 500 us at p95 {500 - p95:.1f} us", flush=True)
else:
    print("B6: no 'scene cache: vblank' lines; check the log's format", flush=True)

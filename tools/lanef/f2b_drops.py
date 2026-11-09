#!/usr/bin/env python3
"""f2b_drops.py CAP FRAMES TAG [BUILD_PID]: which billboard candidates F2b drops, and when. The debug host
(gt4_host_n3orient.exe with the local drop log) replays CAP with lighting.night = enhanced, scene records on and
GT4_F2B_DROPLOG=1; prints, per material dropped: the count, the frames it spans, and how many fall in the first 300
frames (a replay's textures not yet re-uploaded). Waits for BUILD_PID first if given. One heavy slot."""
import ctypes
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

cap, frames, tag = sys.argv[1], sys.argv[2], sys.argv[3]
if len(sys.argv) > 4:
    h = ctypes.windll.kernel32.OpenProcess(0x00100000, False, int(sys.argv[4]))
    if h:
        ctypes.windll.kernel32.WaitForSingleObject(h, 0xFFFFFFFF)
        ctypes.windll.kernel32.CloseHandle(h)
out = CLONE / "build" / "sessions" / f"f2b_drops_{tag}"
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
locks = take_slot(f"F2b drops: {tag}")
try:
    r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "320x180", "--replay", cap,
                        "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, GT4_HOST_RECORD_SCENE="1", GT4_F2B_DROPLOG="1", GT4_R10_LOG=str(out / "r10.log")))
    (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    log(f"replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
by = {}
first = None
for l in (out / "host.log").read_text(encoding="utf-8", errors="replace").splitlines():
    m = re.match(r"F2b drop: frame (\d+) vblank (\d+) material (\d+)", l)
    if m:
        f, mat = int(m.group(2)), int(m.group(3))   # (by VBlank: replay frame = VBlank - the first one + 1)
        first = f if first is None else min(first, f)
        by.setdefault(mat, []).append(f)
    elif l.startswith(("F2b course lamps", "F2b glow class")):
        print(l[:300])
    elif l.startswith("F2b gather totals"):
        gtot = l
    elif l.startswith("F2b gather dropped"):
        gdrop = gdrop + 1 if 'gdrop' in dir() else 1
print(globals().get("gtot", "no gather totals"), "| executions with a drop:", globals().get("gdrop", 0))
v0 = int((out / "r10.log").read_text(encoding="utf-8").split(None, 1)[0])   # the VBlank of replay frame 1
print(f"{sum(len(v) for v in by.values())} drops in {len(by)} materials (by VBlank; replay frame = VBlank - {v0} + 1)")
for mat, fs in sorted(by.items(), key=lambda kv: -len(kv[1])):
    print(f"  material {mat}: {len(fs)} drops, VBlanks {min(fs)}-{max(fs)}, replay frames {min(fs) - v0 + 1}-{max(fs) - v0 + 1}")

#!/usr/bin/env python3
"""build_pair.py: a release game and a Release host from the same commit (the checkout's HEAD, which must be clean):
the game into build/game_release (an existing one is moved aside as game_release_<its game sha prefix>), the host
in build/host-windows-Release, copied to build/lanef_run/gt4_host_main_<sha>.exe. Game build: decomp-release lock,
--jobs 4; host build: one heavy slot, --parallel 6. Children start without a console."""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE, _mkdir_lock, free_gb  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW


def git(*a):
    return subprocess.run(["git", "-C", str(CLONE), *a], capture_output=True, text=True, creationflags=NW, check=True).stdout.strip()


assert not git("status", "--short"), "tree not clean"
sha = git("rev-parse", "--short", "HEAD")
log(f"building game and host at {sha}")

# the game
out = CLONE / "build" / "game_release"
stamp = out / "BUILD-STAMP.json"
if stamp.exists():
    old = json.loads(stamp.read_text())
    keep = CLONE / "build" / f"game_release_{old.get('game_sha256', 'old')[:8]}"
    if not keep.exists():
        out.rename(keep)
        log(f"previous game kept as {keep.name}")
sys.path.insert(0, str(CLONE / "tools"))
import slot  # noqa: E402  (the shared runner: the game slot, stale locks healed)
lock = slot.take(["game"], f"release game at {sha} (--jobs 4)", min_gb=7.0)[0]   # (the coordinator's floor for all)
try:
    r = subprocess.run([sys.executable, str(CLONE / "tools/release_game.py"), "--decomp", "C:/Users/julie/gt4-decomp",
                        "--out", str(out), "--jobs", "4"], cwd=CLONE, capture_output=True, text=True, creationflags=NW)
    log(f"release game: exit {r.returncode}")
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-2000:])
        raise SystemExit(1)
finally:
    shutil.rmtree(lock, ignore_errors=True)

# the host
cmd = os.path.join(os.environ["TEMP"], "lanef_pair_build.cmd")
Path(cmd).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
locks = take_slot(f"host build at {sha}")
try:
    b = subprocess.run(["cmd.exe", "/d", "/c", cmd], capture_output=True, text=True, creationflags=NW)
    log(f"host build: exit {b.returncode}")
    if b.returncode:
        print("\n".join(l for l in b.stdout.splitlines() if "error" in l.lower())[:3000])
        raise SystemExit(1)
    dst = CLONE / "build" / "lanef_run" / f"gt4_host_main_{sha}.exe"
    shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", dst)
    log(f"host copied to {dst.name}")
finally:
    for l in locks:
        shutil.rmtree(l, ignore_errors=True)
log("done; tree: " + (git("status", "--short") or "clean"))

#!/usr/bin/env python3
"""lapboot.py: lap_ssr5r_boot, the night lap recorded from game start (C10; lane C's R7 test, N3's v4 check, the
Odin's band reference). A release game (tools/release_game.py, the game slot) and a host from an empty folder (one
heavy slot) at this clone's HEAD (tree clean), then run_save_job.py's record job with that pair: the approved save
saves-ss-r5 (a fresh copy, deleted after), Race of NA Sports race 4 in B-spec (lap_ssr5r's route), GT4_HOST_RECORD
from VBlank 0 for 20,200 VBlanks (lap_ssr5r ended near 20,126), scene records on, preset original, 1280 x 720, a
live shot every 100 VBlanks from the race (frames in build/sessions/lap_ssr5r_boot/frames), in a heavy-both slot."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
sys.path.insert(0, str(T))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW


def git(*a):
    return subprocess.run(["git", "-C", str(CLONE), *a], capture_output=True, text=True).stdout.strip()


if git("status", "--porcelain", "--untracked-files=no"):
    raise SystemExit("the tree isn't clean")
head = git("rev-parse", "--short=7", "HEAD")
game_dir = CLONE / "build" / f"game_release_{head}"
host = CLONE / "build" / "lanef_run" / f"gt4_host_boot_{head}.exe"

if not (game_dir / "gt4_game.exe").exists():
    sys.path.insert(0, str(CLONE / "tools"))
    import slot  # noqa: E402
    locks = slot.take(["game"], f"lane F: release game at {head} for lap_ssr5r_boot", min_gb=7.0)
    try:
        r = subprocess.run([sys.executable, str(CLONE / "tools/release_game.py"), "--decomp", "C:/Users/julie/gt4-decomp",
                            "--out", str(game_dir), "--jobs", "4"], cwd=CLONE, capture_output=True, text=True,
                           creationflags=NW)
        (CLONE / "build" / f"game_release_{head}.log").write_text(r.stdout + r.stderr, encoding="utf-8")
        log(f"release game exit {r.returncode}")
        if r.returncode:
            print((r.stdout + r.stderr)[-3000:])
            raise SystemExit(1)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)

if not host.exists():
    bdir = f"build\\host-boot-{head}"
    cmd = os.path.join(os.environ["TEMP"], "lanef_boot_host.cmd")
    Path(cmd).write_text(rf"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
if exist {bdir} rmdir /s /q {bdir}
cmake -S . -B {bdir} -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=OFF || exit /b 1
cmake --build {bdir} --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    locks = take_slot(f"lane F: host from an empty folder at {head} for lap_ssr5r_boot")
    try:
        b = subprocess.run(["cmd.exe", "/d", "/c", cmd], capture_output=True, text=True, creationflags=NW)
        (CLONE / "build" / f"host-boot-{head}.log").write_text(b.stdout + b.stderr, encoding="utf-8", errors="replace")
        log(f"host build exit {b.returncode}")
        if b.returncode:
            print((b.stdout + b.stderr)[-3000:])
            raise SystemExit(1)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    shutil.copy2(CLONE / "build" / f"host-boot-{head}" / "gt4_host.exe", host)
log(f"pair: {game_dir.name}/gt4_game.exe, {host.name}")

if git("rev-parse", "--short=7", "HEAD") != head:
    raise SystemExit("HEAD moved during the builds")
env = {k: v for k, v in os.environ.items() if k != "LANEF_SETTINGS"}   # preset original, as C10 has it
env.update(LANEF_HOST=str(host), LANEF_GAME=str(game_dir / "gt4_game.exe"), LANEF_RECORD_FROM_BOOT="1",
           LANEF_SLOT="heavy-both")
r = subprocess.run([sys.executable, "-u", str(T / "run_save_job.py"), "record", "lap_ssr5r_boot", "--cap",
                    "lap_ssr5r_boot", "--count", "20200", "--course", "ssr5b", "--after", "0", "--tail", "10400",
                    "--shots", "100", "--size", "1280x720"], env=env, creationflags=NW)
log(f"record exit {r.returncode}")
cap = CLONE / "build" / "caps" / "lap_ssr5r_boot.cap"
if cap.exists():
    log(f"{cap.name}: {cap.stat().st_size / 1e9:.2f} GB")

#!/usr/bin/env python3
"""R10 in one process: the clean acceptance of HEAD first (the R10 log reverted), then a host built with
r10_log.patch (local, never committed) and copied aside, the patch reverted at once, and a replay of the sections
capture with GT4_R10_LOG writing the grading and fog per frame. Children start without a console."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
T = CLONE / "build" / "lanef_tools"
CAP = CLONE / "build" / "caps" / "r5_ss_ab.cap"
TAG = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--tag=")), "")
OUT = CLONE / "build" / "sessions" / ("r10_ab" + (("_" + TAG) if TAG else ""))


def git(*args, check=True):
    return subprocess.run(["git", "-C", str(CLONE), *args], capture_output=True, text=True, creationflags=NW, check=check)


PATCHED = ["platform/gsnative/gs_frontend.cpp", "platform/gs/stream.cpp"]
git("checkout", "--", *PATCHED)
assert not git("status", "--short").stdout.strip(), "tree not clean"
if "--no-accept" not in sys.argv:
    r = subprocess.run([sys.executable, "-W", "ignore", str(T / "accept.py")], capture_output=True, text=True,
                       creationflags=NW)
    print(r.stdout, flush=True)

git("apply", str(T / "r10_log.patch"))
git("apply", str(T / "r10_batches.patch"))
try:
    build = os.path.join(os.environ["TEMP"], "lanef_r10_build.cmd")
    Path(build).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    locks = take_slot("R10: host build with the local grading log")
    try:
        b = subprocess.run(["cmd.exe", "/d", "/c", build], capture_output=True, text=True, creationflags=NW)
        log(f"R10 host build exit {b.returncode}")
        if b.returncode:
            errors = [l for l in b.stdout.splitlines() if " error " in l or "error C" in l or "fatal error" in l]
            print("\n".join(errors[:30]) or b.stdout[-3000:])
            raise SystemExit(1)
        host = CLONE / "build" / "lanef_run" / "gt4_host_r10.exe"
        host.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", host)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
finally:
    git("checkout", "--", *PATCHED)
    log("R10 patches reverted; tree: " + (git("status", "--short").stdout.strip() or "clean"))

shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
locks = take_slot("R10: replay of r5_ss_ab.cap with the grading log")
try:
    batches = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--batches=")), "13759,13783")
    env = dict(os.environ, GT4_R10_LOG=str(OUT / "r10.log"), GT4_R10_BATCHES=f"{OUT / 'batches.log'},{batches}",
               GT4_R10_DRAWS=batches)
    drawlist = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--drawlist=")), "")
    if drawlist:
        env["GT4_R10_DRAWLIST"] = drawlist
    hold = next((a.split("=", 1)[1] for a in sys.argv if a.startswith("--hold=")), "")
    if hold:
        env["GT4_R10_HOLD"] = hold
    args = [str(host), "--headless", "--size", "320x180", "--replay", str(CAP), "--frames", "3600", "--fixed-step",
            "--preset", "original", "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
            "--capture", str(OUT / "f.ppm"), "--capture-every", "1"]
    r = subprocess.run(args, capture_output=True, text=True, env=env, creationflags=NW)
    for line in (r.stderr or "").splitlines():
        if line.startswith("R10 "):
            log(line)
    log(f"R10 replay exit {r.returncode}; log lines: {sum(1 for _ in open(OUT / 'r10.log'))}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
    log("slot released")

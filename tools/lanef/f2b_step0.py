#!/usr/bin/env python3
"""f2b_step0.py [--no-build] [ENV=VALUE ...]: F2b step 0. A host built with r10_log_v6.patch (staged in the tree, local,
never committed; reverted right after the build) copied to build/lanef_run/gt4_host_f2b0.exe, then a replay of
r5_ss_ab.cap to frame 2530 with GT4_R10_DRAWLIST over VBlanks 15774 and 15782 (replay 2514 and 2522: R11's frames),
every draw's state and model jobs, and frames 2510-2526 captured at 1280x720. Extra ENV=VALUE pairs go to the replay
(GT4_R10_SKIPTEX=... to image a texture's draws away). Output in build/sessions/f2b0[_TAG] (--tag=TAG).
One heavy slot per step; children start without a console."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
OPT = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
CAP = Path(OPT.get("cap", str(CLONE / "build" / "caps" / "r5_ss_ab.cap")))
HOST = CLONE / "build" / "lanef_run" / "gt4_host_f2b0.exe"
TAG = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--tag=")), "")
EXTRA = dict(a.split("=", 1) for a in sys.argv[1:] if "=" in a and not a.startswith("--"))
OUT = CLONE / "build" / "sessions" / ("f2b0" + (("_" + TAG) if TAG else ""))


def git(*args, check=True):
    return subprocess.run(["git", "-C", str(CLONE), *args], capture_output=True, text=True, creationflags=NW, check=check)


if "--no-build" not in sys.argv:
    try:
        st = git("status", "--short").stdout
        if "M  platform/gsnative/gs_frontend.cpp" not in st:
            git("apply", "--3way", str(CLONE / "build/lanef_tools/r10_log_v6.patch"))
        build = os.path.join(os.environ["TEMP"], "lanef_f2b0_build.cmd")
        Path(build).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
        locks = take_slot("F2b step 0: host build with the local draw-list log")
        try:
            b = subprocess.run(["cmd.exe", "/d", "/c", build], capture_output=True, text=True, creationflags=NW)
            log(f"F2b0 host build exit {b.returncode}")
            if b.returncode:
                errors = [l for l in b.stdout.splitlines() if " error " in l or "error C" in l or "fatal error" in l]
                print("\n".join(errors[:30]) or b.stdout[-3000:])
                raise SystemExit(1)
            HOST.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", HOST)
        finally:
            for lock in locks:
                shutil.rmtree(lock, ignore_errors=True)
    finally:
        git("reset", "-q", "--", "platform/gsnative/gs_frontend.cpp", "platform/gs/stream.cpp")
        git("checkout", "--", "platform/gsnative/gs_frontend.cpp", "platform/gs/stream.cpp")
        log("F2b0 patch reverted; tree: " + (git("status", "--short").stdout.strip() or "clean"))

shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
env = dict(os.environ, GT4_R10_LOG=str(OUT / "r10.log"), GT4_R10_DRAWLIST=OPT.get("drawlist", "15774,15782,1,100000"), **EXTRA)
locks = take_slot("F2b step 0: replay with the draw list")
try:
    r = subprocess.run([str(HOST), "--headless", "--size", "1280x720", "--replay", str(CAP), "--frames", OPT.get("frames", "2530"),
                        "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off", "--set",
                        "texture.hd_pack=off", *(["--set", "lighting.night=" + OPT["night"]] if "night" in OPT else []), "--capture-frames", OPT.get("capture", "2510-2526"), "--capture-dir", str(OUT)],
                       capture_output=True, text=True, env=env, creationflags=NW)
    (OUT / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
    log(f"F2b0 replay exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
draws = OUT / "r10.log.draws"
print(f"draw list: {draws} ({draws.stat().st_size if draws.exists() else 0} bytes); frames:",
      len(list(OUT.glob("*.ppm"))))

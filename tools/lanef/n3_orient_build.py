#!/usr/bin/env python3
"""n3_orient_build.py: builds the local N3 debug host from the tree as it stands (r10_log_v6 + n3_dbg_full + n3_local_log3
+ the orientation log, applied by hand; never committed), copies it to build/lanef_run/gt4_host_n3orient.exe, then
reverts the four debug files. One heavy slot."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
FILES = ["platform/gs/gs_host.cpp", "platform/gsnative/gs_frontend.cpp", "platform/gsnative/gs_frontend.h",
         "platform/render/modern/glow_classifier.cpp", "platform/render/modern/course_lamp_pass.cpp", "platform/render/gsn_scene.cpp"]


def git(*args):
    return subprocess.run(["git", "-C", str(CLONE), *args], capture_output=True, text=True, creationflags=NW)


try:
    build = os.path.join(os.environ["TEMP"], "lanef_n3orient_build.cmd")
    Path(build).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    locks = take_slot("N3 step 0: debug host build (orientation log)")
    try:
        b = subprocess.run(["cmd.exe", "/d", "/c", build], capture_output=True, text=True, creationflags=NW)
        log(f"n3orient host build exit {b.returncode}")
        warn = [l for l in b.stdout.splitlines() if "warning C" in l or " error " in l or "error C" in l or "fatal error" in l]
        print("\n".join(warn[:30]))
        if b.returncode:
            print(b.stdout[-3000:])
            raise SystemExit(1)
        exe = (CLONE / "build/host-windows-Release/gt4_host.exe").read_bytes()
        missing = [k for k in (b"N3 orient", b"N3 footprint", b"N3 set upload", b"N3 v4:", b"F2b drop:", b"F2b gather totals", b"N3 candidate", b"N3 host texture", b"GT4_R10_SKIPTEX") if k not in exe]
        if missing:
            print(f"the built host lacks {missing}: the debug patches were not in the tree; not copied")
            raise SystemExit(1)
        shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", CLONE / "build/lanef_run/gt4_host_n3orient.exe")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
finally:
    git("checkout", "--", *FILES)
    log("debug files reverted; tree: " + (git("status", "--short").stdout.strip() or "clean"))

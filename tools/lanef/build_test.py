#!/usr/bin/env python3
"""build_test.py TEST_REGEX TARGET...: build the targets in build/host-windows-Release (one heavy slot, --parallel 6),
then ctest -R TEST_REGEX there. Prints the compiler's errors and warnings for the targets, and ctest's output."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log  # noqa: E402

regex, targets = sys.argv[1], sys.argv[2:]
cmd = os.path.join(os.environ["TEMP"], "lanef_build_test.cmd")
Path(cmd).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target """ + " ".join(targets) + r""" || exit /b 1
cd build\host-windows-Release
ctest -R "^(""" + regex + r""")$" --output-on-failure || exit /b 2
""", encoding="ascii")
locks = take_slot("build and test " + " ".join(targets))
try:
    r = subprocess.run(["cmd.exe", "/d", "/c", cmd], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    lines = r.stdout.splitlines()
    issues = [l for l in lines if " error " in l or " warning " in l or "error C" in l or "warning C" in l]
    print("\n".join(issues[:40]))
    if r.returncode and not issues:
        print(r.stdout[-4000:], r.stderr[-2000:])
    print("\n".join(l for l in lines if "Test" in l or "tests passed" in l or "FAILED" in l or "Passed" in l)[-3000:])
    log(f"exit {r.returncode}")
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)

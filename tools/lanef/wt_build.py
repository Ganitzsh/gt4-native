#!/usr/bin/env python3
"""wt_build.py SRC NAME: a host from an empty folder in worktree SRC (build/host-NAME, BUILD_TESTING on) and the full
ctest, in one heavy slot; copies the host to build/lanef_run/gt4_host_NAME.exe. Lane F, 2026-11-10."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, CLONE  # noqa: E402

SRC, NAME = Path(sys.argv[1]), sys.argv[2]
OUT = CLONE / "build/sessions" / f"wt_build_{NAME}"
shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
cmd = OUT / "build.cmd"
cmd.write_text(rf"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d {SRC}
if exist build\host-{NAME} rmdir /s /q build\host-{NAME}
cmake -S . -B build\host-{NAME} -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON || exit /b 1
cmake --build build\host-{NAME} --parallel 6 || exit /b 1
ctest --test-dir build\host-{NAME} --output-on-failure || exit /b 1
""", encoding="ascii")
head = subprocess.run(["git", "-C", str(SRC), "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True).stdout.strip()
locks = take_slot(f"lane F: host from an empty folder + ctest, {NAME} (worktree at {head} + local changes)")
try:
    b = subprocess.run(["cmd.exe", "/d", "/c", str(cmd)], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
(OUT / "build.log").write_text(b.stdout + b.stderr, encoding="utf-8", errors="replace")
tests = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", b.stdout)
warn = [l for l in b.stdout.splitlines() if re.search(r"(lit_road|scene_cache|gs_frontend|gsn_scene)\.(cpp|h)\(\d+\): warning", l)
        and "C4996" not in l]
print(f"{NAME}: build + ctest exit {b.returncode}; {tests.group(0) if tests else 'no ctest summary'}; "
      f"warnings in the touched files {len(warn)}", *warn[:5], sep="\n  ", flush=True)
if b.returncode:
    print((b.stdout + b.stderr)[-3000:])
    raise SystemExit(1)
shutil.copy2(SRC / f"build/host-{NAME}/gt4_host.exe", CLONE / "build/lanef_run" / f"gt4_host_{NAME}.exe")

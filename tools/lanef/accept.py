#!/usr/bin/env python3
"""Lane F acceptance in one process: a heavy slot (>= 6 GB free), the host from an empty build folder
(tools/build_windows.cmd steps, --parallel 6) with the full ctest, then light_list and lights_cluster under the
validation layer with sync validation. Children start without a console (CREATE_NO_WINDOW)."""
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log  # noqa: E402

CLONE = Path("C:/Users/julie/gt4-lane-F")
SCRIPT = Path(os.environ["TEMP"]) / "lanef_clean.cmd"
BUILD_LOG = CLONE / "build" / "lanef_tools" / "clean_build.log"

CMD = r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
if exist build\host-windows-Release rmdir /s /q build\host-windows-Release
cmake -S . -B build\host-windows-Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON || exit /b 1
cmake --build build\host-windows-Release --parallel 6 || exit /b 1
ctest --test-dir build\host-windows-Release --output-on-failure
"""


def main():
    SCRIPT.write_text(CMD, encoding="ascii")
    locks = take_slot("clean host build + ctest + validation")
    try:
        t0 = time.time()
        with open(BUILD_LOG, "wb") as out:
            rc = subprocess.run(["cmd.exe", "/d", "/c", str(SCRIPT)], stdout=out, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW).returncode
        log(f"build + ctest exit {rc} in {time.time() - t0:.0f} s")
        text = BUILD_LOG.read_text(errors="replace")
        for line in text.splitlines():
            if "tests passed" in line or "tests failed" in line or "***" in line or "error C" in line or "error LNK" in line:
                print(line)
        env = dict(os.environ, VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation", VK_LAYER_VALIDATE_SYNC="1")
        for exe in ("gt4_light_list_test.exe", "gt4_lights_cluster_test.exe"):
            if not (CLONE / "build/host-windows-Release" / exe).exists():
                print(f"{exe}: not built")
                continue
            r = subprocess.run([str(CLONE / "build/host-windows-Release" / exe)], env=env, capture_output=True, text=True,
                               creationflags=subprocess.CREATE_NO_WINDOW)
            for line in r.stdout.splitlines():
                if any(k in line for k in ("ms GPU", "validation messages", "FAIL", "all checks", "skipped")):
                    print(f"{exe}: {line}")
            print(f"{exe}: exit {r.returncode}")
        # replays of local captures named on the command line: frames every 100 VBlanks, each must show a picture
        for cap in sys.argv[1:]:
            out = CLONE / "build" / "sessions" / ("replay_" + Path(cap).stem)
            import shutil as _sh
            _sh.rmtree(out, ignore_errors=True); out.mkdir(parents=True)
            frames = int(os.environ.get("LANEF_REPLAY_FRAMES", "600"))
            args = [str(CLONE / "build/host-windows-Release/gt4_host.exe"), "--headless", "--size", "1280x720",
                    "--replay", str(cap), "--frames", str(frames), "--fixed-step", "--preset", "original",
                    "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                    "--capture", str(out / "f.ppm"), "--capture-every", "100"]
            r = subprocess.run(args, capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
            shots = sorted(out.glob("*.ppm"))
            nonzero = []
            for p in shots:
                data = p.read_bytes(); body = data.split(maxsplit=4)[4]
                nonzero.append(sum(1 for b in body[::97] if b) / max(1, len(body[::97])))
            print(f"replay {Path(cap).name}: exit {r.returncode}, {len(shots)} frames, nonzero share per frame "
                  + ", ".join(f"{z:.2f}" for z in nonzero))
    finally:
        import shutil
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
        log("slot released")


if __name__ == "__main__":
    main()

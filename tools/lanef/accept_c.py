#!/usr/bin/env python3
"""accept_c.py: lane C's mid-run recorder fixes (edb1cce) on lane F's route. A host built from edb1cce (the tree goes
back to main at once), a fresh B-spec recording with r5_ss_ab's trigger (--after 3400; 3720 VBlanks so the live
bursts reach replay frame 3465) and its live frames, replays of the new capture and of r5_ss_ab.cap with that host,
then the live / replay difference per frame. Children start without a console; heavy steps through take_slot."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
COMMIT = "edb1cce"
HOST = CLONE / "build" / "lanef_run" / f"gt4_host_c_{COMMIT}.exe"
CAPS = CLONE / "build" / "caps"
S = CLONE / "build" / "sessions"
COUNT = 3720


def git(*args, check=True):
    return subprocess.run(["git", "-C", str(CLONE), *args], capture_output=True, text=True, creationflags=NW, check=check)


def build():
    assert not git("status", "--short").stdout.strip(), "tree not clean"
    branch = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    cmd = os.path.join(os.environ["TEMP"], "lanef_c_build.cmd")
    Path(cmd).write_text(r"""@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d C:\Users\julie\gt4-lane-F
cmake --build build\host-windows-Release --parallel 6 --target gt4_host || exit /b 1
""", encoding="ascii")
    locks = take_slot(f"lane C acceptance: host build at {COMMIT}")
    try:
        git("checkout", "-q", "--detach", COMMIT)
        b = subprocess.run(["cmd.exe", "/d", "/c", cmd], capture_output=True, text=True, creationflags=NW)
        log(f"host build at {COMMIT}: exit {b.returncode}")
        if b.returncode:
            print("\n".join(l for l in b.stdout.splitlines() if "error" in l.lower())[:3000])
            raise SystemExit(1)
        HOST.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(CLONE / "build/host-windows-Release/gt4_host.exe", HOST)
    finally:
        git("checkout", "-q", branch)
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
        log(f"tree back on {branch}: " + (git("status", "--short").stdout.strip() or "clean"))


GAME_OUT = CLONE / "build" / "game_release_c"


def game():
    """A release game from the same commit as the host (the host-game link version must match): the decomp-release
    lock, --jobs 4, the tree on COMMIT only while release_game.py reads it."""
    import time
    from r5_session import _mkdir_lock, free_gb
    assert not git("status", "--short").stdout.strip(), "tree not clean"
    branch = git("rev-parse", "--abbrev-ref", "HEAD").stdout.strip()
    while True:
        lock = _mkdir_lock("gt4-decomp-release", f"lane C acceptance: release game at {COMMIT} (--jobs 4)")
        if lock is not None and free_gb() >= 6.0:
            break
        if lock is not None:
            shutil.rmtree(lock, ignore_errors=True)
        time.sleep(30)
    log(f"decomp-release lock, {free_gb():.1f} GB available")
    try:
        git("checkout", "-q", "--detach", COMMIT)
        r = subprocess.run([sys.executable, str(CLONE / "tools/release_game.py"), "--decomp", "C:/Users/julie/gt4-decomp",
                            "--out", str(GAME_OUT), "--jobs", "4"], cwd=CLONE, capture_output=True, text=True,
                           creationflags=NW)
        log(f"release game at {COMMIT}: exit {r.returncode}")
        if r.returncode:
            print(r.stdout[-3000:], r.stderr[-2000:])
            raise SystemExit(1)
    finally:
        git("checkout", "-q", branch)
        shutil.rmtree(lock, ignore_errors=True)
        log(f"tree back on {branch}: " + (git("status", "--short").stdout.strip() or "clean"))


def record():
    os.environ["LANEF_GAME"] = str(GAME_OUT / "gt4_game.exe")   # read by r5_session (the child inherits it)
    src, run = CLONE / "build" / "saves-ss-r5" / "saves", CLONE / "build" / "saves-ss-r5-run"
    shutil.rmtree(run, ignore_errors=True)
    shutil.copytree(src, run)     # a fresh copy: the original is never written
    # r5_session copies build/host-windows-Release/gt4_host.exe, which is the edb1cce build here
    r = subprocess.run([sys.executable, "-W", "ignore", str(CLONE / "build/lanef_tools/r5_session.py"), "record",
                        "rec_ss_ab2", "--course", "ssr5b", "--saves-dir", str(run), "--size", "320x180",
                        "--cap", "r5_ss_ab2", "--count", str(COUNT), "--after", "3400", "--bursts"],
                       capture_output=True, text=True, creationflags=NW)
    print(r.stdout[-2500:], r.stderr[-1500:], flush=True)
    # the live frames by host frame: live/f_<frame - first>.ppm
    d = S / "rec_ss_ab2"
    ev = [json.loads(l) for l in open(d / "events.jsonl", encoding="utf-8", errors="replace") if '"capture"' in l]
    bursts = sorted((e["frame"], e["file"]) for e in ev if e["file"].startswith("L"))
    if not bursts:
        raise SystemExit("no live frames: the session did not reach the recording (see host.log)")
    live = d / "live"
    live.mkdir(exist_ok=True)
    first = bursts[0][0]
    gaps = 0
    for k, (f, name) in enumerate(bursts):
        gaps += k and f != bursts[k - 1][0] + 1
        src = d / "frames" / name
        if src.exists():
            src.replace(live / f"f_{f - first:06d}.ppm")
    log(f"live frames: {len(bursts)}, host frames {first}..{bursts[-1][0]}, gaps {gaps}")


def replay(cap, out, frames):
    out = S / out
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    locks = take_slot(f"lane C acceptance: replay of {cap}")
    try:
        r = subprocess.run([str(HOST), "--headless", "--size", "320x180", "--replay", str(CAPS / cap), "--frames",
                            str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--capture", str(out / "f.ppm"), "--capture-every", "1"],
                           capture_output=True, text=True, creationflags=NW)
        log(f"replay {cap}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)


if __name__ == "__main__":
    steps = sys.argv[1:] or ["build", "game", "record", "replay"]
    if "build" in steps:
        build()
    if "game" in steps:
        game()
    if "record" in steps:
        record()
    if "replay" in steps:
        replay("r5_ss_ab2.cap", "replay_r5_ss_ab2_c", COUNT)
        replay("r5_ss_ab.cap", "replay_r5_ss_ab_c", 3600)
    log("done")

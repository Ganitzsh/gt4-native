#!/usr/bin/env python3
"""n3_probe.py COURSE...: N3's second night course. For each City course, a fresh copy of build/saves-ss-r5 (the edited
save, every course open; never the original tgz) in build/saves-ss-r5-run, then r5_session.py gridprobe with the game
and host built together from main (build/game_release, build/lanef_run/gt4_host_main_<sha>.exe): the grid, 5 s of
driving, a shot. The shots are put side by side in build/sessions/n3_probe.png. One live session at a time."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

CLONE = Path("C:/Users/julie/gt4-lane-F")
TOOLS = CLONE / "build" / "lanef_tools"
sys.path.insert(0, str(TOOLS))
from r5_session import read_ppm  # noqa: E402

host = max((CLONE / "build" / "lanef_run").glob("gt4_host_main_*.exe"), key=lambda p: p.stat().st_mtime)
shots = []
for course in sys.argv[1:]:
    run = CLONE / "build" / "saves-ss-r5-run"
    shutil.rmtree(run, ignore_errors=True)
    shutil.copytree(CLONE / "build" / "saves-ss-r5", run)
    name = f"n3probe_{course}"
    r = subprocess.run([sys.executable, str(TOOLS / "r5_session.py"), "gridprobe", name, "--course", course, "--saves-dir",
                        str(run / "saves"), "--throttle", "r2", "--size", "640x360"],
                       capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                       env=dict(os.environ, LANEF_HOST=str(host), LANEF_GAME=str(CLONE / "build/game_release/gt4_game.exe")))
    print(course, "exit", r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:] if (r.stdout + r.stderr).strip() else "")
    shot = CLONE / "build" / "sessions" / name / "probe_drive.ppm"
    if shot.exists():
        shots.append((course, read_ppm(shot).resize((640, 360))))
shutil.rmtree(CLONE / "build" / "saves-ss-r5-run", ignore_errors=True)
if shots:
    sheet = Image.new("RGB", (640 * len(shots), 360))
    for i, (_, im) in enumerate(shots):
        sheet.paste(im, (640 * i, 0))
    sheet.save(CLONE / "build" / "sessions" / "n3_probe.png")
    print("sheet:", [c for c, _ in shots])

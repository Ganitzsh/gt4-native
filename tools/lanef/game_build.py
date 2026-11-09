#!/usr/bin/env python3
"""game_build.py: a release game from the checkout's HEAD into build/game_release (the old one kept aside by its game
sha), under the decomp-release lock, --jobs 4. release_game.py refuses uncommitted changes in the game's inputs; other
uncommitted files (platform/, cmake/) do not matter to it."""
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import log, CLONE, _mkdir_lock, free_gb  # noqa: E402

NW = subprocess.CREATE_NO_WINDOW
out = CLONE / "build" / "game_release"
stamp = out / "BUILD-STAMP.json"
if stamp.exists():
    keep = CLONE / "build" / f"game_release_{json.loads(stamp.read_text()).get('game_sha256', 'old')[:8]}"
    if not keep.exists():
        out.rename(keep)
        log(f"previous game kept as {keep.name}")
    else:
        shutil.rmtree(out)
while True:
    lock = _mkdir_lock("gt4-decomp-release", "release game at HEAD (--jobs 4)")
    if lock is not None and free_gb() >= 6.0:
        break
    if lock is not None:
        shutil.rmtree(lock, ignore_errors=True)
    time.sleep(30)
try:
    r = subprocess.run([sys.executable, str(CLONE / "tools/release_game.py"), "--decomp", "C:/Users/julie/gt4-decomp",
                        "--out", str(out), "--jobs", "4"], cwd=CLONE, capture_output=True, text=True, creationflags=NW)
    log(f"release game: exit {r.returncode}")
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-2000:])
finally:
    shutil.rmtree(lock, ignore_errors=True)

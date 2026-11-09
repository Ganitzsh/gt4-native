#!/usr/bin/env python3
"""run_save_job.py ARGS...: r5_session.py ARGS with a fresh copy of build/saves-ss-r5 in build/saves/run-ss-r5 (the
coordinator's standing rule: a copy per run, deleted after), the a75a6c2 pair, scene records on; --saves-dir added."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
B = Path("C:/Users/julie/gt4-lane-F/build")
env = dict(os.environ, GT4_HOST_RECORD_SCENE="1")
env.setdefault("LANEF_HOST", str(B / "lanef_run/gt4_host_main_a75a6c2.exe"))   # a given pair (LANEF_HOST/GAME) wins
env.setdefault("LANEF_GAME", str(B / "game_release/gt4_game.exe"))
run = B / "saves" / "run-ss-r5"
shutil.rmtree(run, ignore_errors=True)
shutil.copytree(B / "saves-ss-r5", run)
try:
    r = subprocess.run([sys.executable, "-u", str(T / "r5_session.py"), *sys.argv[1:], "--saves-dir", str(run / "saves")],
                       env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"exit {r.returncode}", flush=True)
finally:
    shutil.rmtree(run, ignore_errors=True)

#!/usr/bin/env python3
"""n3_record2.py: the two recordings for N3 step 0 and F2b's second course, with the a75a6c2 pair (scene records on):
Seoul (day) and a new Clubman Stage Route 5 night run, each on a fresh copy of build/saves-ss-r5 (R2 throttle).
r5_session takes the slot itself. Captures: build/caps/n3_seoul.cap, build/caps/f2b_csr5.cap."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
B = Path("C:/Users/julie/gt4-lane-F/build")
env = dict(os.environ, GT4_HOST_RECORD_SCENE="1", LANEF_HOST=str(B / "lanef_run/gt4_host_main_a75a6c2.exe"),
           LANEF_GAME=str(B / "game_release/gt4_game.exe"))
jobs = [("n3rec_seoul", "seoul", "n3_seoul", "3600"), ("f2brec_csr5", "csr5", "f2b_csr5", "4800")]
only = sys.argv[1:]
for name, course, cap, count in jobs:
    if only and course not in only:
        continue
    run = B / "saves" / "run-ss-r5"   # (a fresh copy per run in build/saves, deleted after: coordinator's standing rule)
    shutil.rmtree(run, ignore_errors=True)
    shutil.copytree(B / "saves-ss-r5", run)
    r = subprocess.run([sys.executable, str(T / "r5_session.py"), "record", name, "--cap", cap, "--count", count,
                        "--course", course, "--saves-dir", str(run / "saves"), "--throttle", "r2"],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"== {name}: exit {r.returncode}\n{r.stdout[-3000:]}\n{r.stderr[-2000:]}", flush=True)
    shutil.rmtree(run, ignore_errors=True)

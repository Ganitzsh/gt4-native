#!/usr/bin/env python3
"""n3_flag_checks.py TAG CAP FRAMES DENSE: for one kFrameNight candidate's lap, the v4 hunt imaged against the base
(n3_v4hunt2.py) and the never-dark check (n3_dark.py), one after the other; outputs in build/sessions/n3v4b_TAG.txt and
n3dark_TAG.txt."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
tag, cap, frames, dense = sys.argv[1:5]
for name, args in (("n3v4b", ["n3_v4hunt2.py", tag, cap, frames, dense]), ("n3dark", ["n3_dark.py", tag, cap, frames])):
    with open(S / f"{name}_{tag}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / args[0]), *args[1:]], stdout=f, stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{name} {tag}: exit {r.returncode}", flush=True)

#!/usr/bin/env python3
"""n3_resume.py: N3 step 0 after the quiet window, in order: the debug host with the per-texture draw count
(f2b_step0.py builds it with r10_log_v6.patch and reverts the tree), Clubman's labels again (its labels reused, the base
replay gives the draw counts), then Route 5 Reverse's remaining looks (the held-out check), then the evaluation."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
NW = subprocess.CREATE_NO_WINDOW


def run(*args):
    r = subprocess.run([sys.executable, *map(str, args)], capture_output=True, text=True, creationflags=NW)
    print(r.stdout[-6000:], r.stderr[-1500:], flush=True)
    return r.returncode


run(T / "f2b_step0.py", "--tag=v6build", "--cap=C:/Users/julie/gt4-lane-F/build/caps/n3_csr5.cap", "--frames=40",
    "--drawlist=0,0,1,1", "--capture=30-30")
run(T / "n3_label.py", "csr5", "C:/Users/julie/gt4-lane-F/build/caps/n3_csr5.cap", "1200")
run(T / "n3_label.py", "r5rev", "C:/Users/julie/gt4-lane-A/build/caps/a5b_r5.cap", "1100", "--groups")
run(T / "n3_eval.py")

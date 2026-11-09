#!/usr/bin/env python3
"""n3_chain.py: one after another (each takes its own slot): the n3orient debug host build (the debug patches must be
applied in the tree first), the Clubman night recording (n3_record2.py csr5), the unseen-label sweep (n3_unseen.py),
then the orientation replays (n3_orient_all.py). Each step's output is kept in build/sessions/n3_chain_<step>.txt."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
OUT = Path("C:/Users/julie/gt4-lane-F/build/sessions")
steps = (("build", ["n3_orient_build.py"]), ("record", ["n3_record2.py", "csr5"]), ("unseen", ["n3_unseen.py"]),
         ("orient", ["n3_orient_all.py"]))
only = sys.argv[1:]   # step names to run (default: all)
for name, args in steps:
    if only and name not in only:
        continue
    with open(OUT / f"n3_chain_{name}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / args[0]), *args[1:]], stdout=f, stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{name}: exit {r.returncode}", flush=True)
    if name == "build" and r.returncode:
        break

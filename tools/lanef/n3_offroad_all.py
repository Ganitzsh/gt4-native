#!/usr/bin/env python3
"""n3_offroad_all.py: the off-road check in order: Route 5 Reverse's a5b_r5 with the fence forced in (the positive
control), then the orientation logs of both B-spec lap captures (n3_orient.py), then the off-road check over each lap
(the fence look forced in where the capture has it). Each step's output in build/sessions/n3_offall_<step>.txt."""
import shutil
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
C = "C:/Users/julie/gt4-lane-F/build/caps/"
FENCE = "64x64 (69, 69, 69)"
shutil.rmtree(S / "n3off_r5rev", ignore_errors=True)
steps = [
    ("off_r5rev", ["n3_offroad.py", "r5rev", "C:/Users/julie/gt4-lane-A/build/caps/a5b_r5.cap", "1100", "--control",
                   "1301,3042,4166,4247,4439"]),
    ("orient_lapc", ["n3_orient.py", "lapc", C + "lap_csr5.cap", "9000"]),
    ("orient_laps", ["n3_orient.py", "laps", C + "lap_ssr5r.cap", "10200"]),
    ("off_lapc", ["n3_offroad.py", "lapc", C + "lap_csr5.cap", "9000", "--every", "100", "--control-look", FENCE]),
    ("off_laps", ["n3_offroad.py", "laps", C + "lap_ssr5r.cap", "10200", "--every", "100", "--control-look", FENCE]),
]
for name, args in steps:
    if len(sys.argv) > 1 and name not in sys.argv[1:]:
        continue
    with open(S / f"n3_offall_{name}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / args[0]), *args[1:]], stdout=f, stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{name}: exit {r.returncode}", flush=True)

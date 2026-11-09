#!/usr/bin/env python3
"""n3_v4hunt_all.py: n3_v4hunt.py on both B-spec laps, one after the other; output in build/sessions/n3v4_<tag>.txt.
Start/finish crossings: Clubman's lap ends near frame 8030 of its capture, Route 5 Reverse's near 5840."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
C = "C:/Users/julie/gt4-lane-F/build/caps/"
for tag, cap, frames, dense in (("lapc", C + "lap_csr5.cap", "9000", "7800-8300/10"),
                                ("laps", C + "lap_ssr5r.cap", "10200", "5600-6100/10")):
    with open(S / f"n3v4_{tag}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / "n3_v4hunt.py"), tag, cap, frames, dense], stdout=f,
                           stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{tag}: exit {r.returncode}", flush=True)

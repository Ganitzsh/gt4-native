#!/usr/bin/env python3
"""n3_image_all.py: every v3 look imaged at its frame of largest change, skip on against off (n3_offroad.py, resumable:
looks already done are kept): Clubman (n3_csr5.cap, a frame every 25 from 300), Route 5 Reverse (a5b_r5.cap, every 25),
both B-spec laps (every 100). Waits for the Route 5 lap's orientation log first."""
import subprocess
import sys
import time
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
C = "C:/Users/julie/gt4-lane-F/build/caps/"
steps = [("csr5", C + "n3_csr5.cap", "1200", "25"), ("r5rev", "C:/Users/julie/gt4-lane-A/build/caps/a5b_r5.cap", "1100", "25"),
         ("lapc", C + "lap_csr5.cap", "9000", "100"), ("laps", C + "lap_ssr5r.cap", "10200", "100")]
for tag, cap, frames, every in steps:
    if len(sys.argv) > 1 and tag not in sys.argv[1:]:
        continue
    while not (S / f"n3o_{tag}" / "orient.tsv").exists():
        time.sleep(30)
    with open(S / f"n3_image_{tag}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / "n3_offroad.py"), tag, cap, frames, "--every", every], stdout=f,
                           stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{tag}: exit {r.returncode}", flush=True)

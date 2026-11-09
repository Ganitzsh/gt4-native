#!/usr/bin/env python3
"""n3_laps2.py [COURSE...]: the laps kFrameNight's choice needs, scene records on, the a75a6c2 pair, a fresh copy of
saves-ss-r5 per run (run_save_job.py): C.S. Route 5 Reverse in B-spec (Beginner Events > Sunday Cup race 4, 2 laps:
7,500 VBlanks covers a lap and the start stretch), S.S. Route 5 forward and Hong Kong at dusk from the Arcade City row
(Time Trial, throttle only, R2: 9,000 VBlanks; the car rides the walls, so the coverage is reported, not assumed).
Captures build/caps/lap_csr5r.cap, lap_ssr5f.cap, lap_hk.cap."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
jobs = (("lap_csr5r", "csr5rb", "7500", []), ("lap_ssr5f", "ssr5", "9000", ["--throttle", "r2"]),
        ("lap_hk", "hongkong", "9000", ["--throttle", "r2"]))
for name, course, count, extra in jobs:
    if len(sys.argv) > 1 and course not in sys.argv[1:]:
        continue
    r = subprocess.run([sys.executable, "-u", str(T / "run_save_job.py"), "record", name, "--cap", name, "--count", count,
                        "--course", course, *extra], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"== {name}: exit {r.returncode}\n{r.stdout[-1500:]}\n{r.stderr[-800:]}", flush=True)

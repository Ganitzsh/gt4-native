#!/usr/bin/env python3
"""n3_laps.py: N3's full-lap recordings in B-spec (the game drives), scene records on, the a75a6c2 pair, a fresh save
copy per run (run_save_job.py): Clubman Cup race 4 (Clubman Stage Route 5) and Race of NA Sports race 4 (Special Stage
Route 5 Reverse), recording from the race start for more than a lap, so the start area comes round again after the
garbage frames. Captures build/caps/lap_csr5.cap and lap_ssr5r.cap (delete after the checks)."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
for name, course, count in (("lap_csr5", "csr5b", "9000"), ("lap_ssr5r", "ssr5b", "10200")):
    if len(sys.argv) > 1 and course not in sys.argv[1:]:
        continue
    r = subprocess.run([sys.executable, "-u", str(T / "run_save_job.py"), "record", name, "--cap", name, "--count", count,
                        "--course", course], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"== {name}: exit {r.returncode}\n{r.stdout[-2500:]}\n{r.stderr[-1500:]}", flush=True)

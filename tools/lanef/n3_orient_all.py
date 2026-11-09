#!/usr/bin/env python3
"""n3_orient_all.py: n3_orient.py on the three courses, one after another (Clubman night with its labels, Route 5 Reverse
with its look labels, Seoul day without labels). Outputs in build/sessions/n3o_<tag>/report.txt."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
for tag, cap, frames, labels in (("csr5", "C:/Users/julie/gt4-lane-F/build/caps/n3_csr5.cap", "1200", ["--labels", "csr5"]),
                                 ("r5rev", "C:/Users/julie/gt4-lane-A/build/caps/a5b_r5.cap", "1100", ["--labels", "r5rev"]),
                                 ("seoul", "C:/Users/julie/gt4-lane-F/build/caps/n3_seoul.cap", "3600", []),
                                 ("csr5new", "C:/Users/julie/gt4-lane-F/build/caps/f2b_csr5.cap", "4800", []),
                                 ("lapc", "C:/Users/julie/gt4-lane-F/build/caps/lap_csr5.cap", "9000", []),
                                 ("laps", "C:/Users/julie/gt4-lane-F/build/caps/lap_ssr5r.cap", "10200", [])):
    if len(sys.argv) > 1 and tag not in sys.argv[1:]:
        continue
    if not Path(cap).exists() or Path(cap).stat().st_size == 0:
        print(f"{tag}: no capture", flush=True)
        continue
    r = subprocess.run([sys.executable, str(T / "n3_orient.py"), tag, cap, frames, *labels], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    (S / f"n3o_{tag}").mkdir(parents=True, exist_ok=True)
    (S / f"n3o_{tag}" / "report.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    print(f"{tag}: exit {r.returncode}", flush=True)

#!/usr/bin/env python3
"""hall_probes.py: the Beginner and Professional halls' event pages (r5_session.py hallprobe, 8 events each, a fresh
save copy per run), then a contact sheet per hall of the event pages: build/sessions/hall_<hall>/sheet_events.png."""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
for hall in ("beginner", "professional"):
    r = subprocess.run([sys.executable, "-u", str(T / "run_save_job.py"), "hallprobe", f"hall_{hall}", "--hall", hall,
                        "--count", "8", "--size", "640x360"], capture_output=True, text=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"== {hall}: exit {r.returncode}\n{r.stdout[-800:]}\n{r.stderr[-600:]}", flush=True)
    f = S / f"hall_{hall}" / "frames"
    shots = [f / "h_hall.ppm"] + [f / f"h_event{e}.ppm" for e in range(1, 9)]
    shots = [str(p) for p in shots if p.exists()]
    if shots:
        subprocess.run([sys.executable, str(T / "sheet.py"), str(S / f"hall_{hall}" / "sheet_events.png"), *shots[:9]],
                       creationflags=subprocess.CREATE_NO_WINDOW)

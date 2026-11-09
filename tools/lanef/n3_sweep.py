#!/usr/bin/env python3
"""n3_sweep.py PHASE: N3's (a) and (d) (the architect, 2026-11-09): n3_dark.py per lap and variant on the knobs
host (gt4_host_n3knobs.exe), one variant after another (each two heavy-slot replays; the GS path cached per lap).
Output appended to build/sessions/n3_sweep.txt.
  phase 1: lap_csr5 (the clean lap for R7), frames 600-5,000 every 100: base, (d), points 30x6, 30x12, 45x12, spots
           (120 degrees) 30x6, 30x12
  phase 2 VARIANT...: the named variants on lap_ssr5r and lap_csr5r, and lap_csr5 in full"""
import subprocess
import sys
from pathlib import Path

T = Path(__file__).resolve().parent
CLONE = Path("C:/Users/julie/gt4-lane-F")
CAPS = CLONE / "build" / "caps"
HOST = CLONE / "build" / "lanef_run" / ("gt4_host_n3knobs2.exe" if sys.argv[1] in ("3", "4") else "gt4_host_n3knobs.exe")
VARIANTS = {
    "base": ("", ""),
    "d": ("", "GT4_N3_V4NARROW=20"),
    "p30x6": ("GT4_N3_GLOW_RANGE=30", ""),
    "p30x12": ("GT4_N3_GLOW_RANGE=30;GT4_N3_GLOW_INTENSITY=12", ""),
    "p45x12": ("GT4_N3_GLOW_RANGE=45;GT4_N3_GLOW_INTENSITY=12", ""),
    "s30x6": ("GT4_N3_GLOW_SPOT=0.5;GT4_N3_GLOW_RANGE=30", ""),
    "s30x12": ("GT4_N3_GLOW_SPOT=0.5;GT4_N3_GLOW_RANGE=30;GT4_N3_GLOW_INTENSITY=12", ""),
    # phase 3 (knobs2 host): (a3) batwing at theta_c 55/60/65, range 40, I0 6 (straight down equals the point lamp);
    # (a2) source radius 9 and 15 m, intensity scaled by (81 + r^2) / 81 so the road under a 9 m lamp keeps its level
    "bw55": ("GT4_N3_GLOW_BATWING=55;GT4_N3_GLOW_RANGE=40", ""),
    "bw60": ("GT4_N3_GLOW_BATWING=60;GT4_N3_GLOW_RANGE=40", ""),
    "bw65": ("GT4_N3_GLOW_BATWING=65;GT4_N3_GLOW_RANGE=40", ""),
    "r9x12": ("GT4_N3_GLOW_RADIUS=9;GT4_N3_GLOW_RANGE=40;GT4_N3_GLOW_INTENSITY=12", ""),
    "r15x23": ("GT4_N3_GLOW_RADIUS=15;GT4_N3_GLOW_RANGE=40;GT4_N3_GLOW_INTENSITY=23", ""),
}
LAPS = {"lapc": ("lap_csr5.cap", 9000), "laps": ("lap_ssr5r.cap", 10200), "lapcr": ("lap_csr5r.cap", 7500)}

phase = sys.argv[1]
if phase == "1":
    jobs = [("lapc", 5000, v) for v in list(VARIANTS)[:7]]
elif phase in ("3", "4"):   # 3: the phase-3 set on lap_csr5 600-5000; 4 VARIANT...: given variants there (knobs2 host)
    jobs = [("lapc", 5000, v) for v in (sys.argv[2:] or ["bw55", "bw60", "bw65", "r9x12", "r15x23"])]
else:
    jobs = [(lap, frames, v) for v in sys.argv[2:] for lap, (_, frames) in LAPS.items()]
summary = CLONE / "build" / "sessions" / "n3_sweep.txt"
for lap, frames, v in jobs:
    env, benv = VARIANTS[v]
    cap = CAPS / LAPS[lap][0]
    args = [sys.executable, "-u", str(T / "n3_dark.py"), f"{lap}_{v}_{frames}", str(cap), str(frames), "--every", "100",
            "--host", str(HOST)]
    if env:
        args += ["--env", env]
    if benv:
        args += ["--benv", benv]
    r = subprocess.run(args, capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    keep = [l for l in r.stdout.splitlines() if not l.startswith(("slot:", "["))]
    with open(summary, "a", encoding="utf-8") as f:
        f.write(f"=== {lap} {v} (frames 600-{frames}/100; B+C {env or '-'}; B {benv or '-'}) exit {r.returncode}\n")
        f.write("\n".join(keep) + "\n" + (r.stderr[-1500:] if r.returncode else ""))
    print(f"{lap} {v}: exit {r.returncode}", flush=True)

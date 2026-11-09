#!/usr/bin/env python3
"""n3_v4hunt2_all.py BUILD_PID: waits for the host build (pid) to end, then n3_v4hunt2.py on both B-spec laps, then
Clubman's per-look images (n3_image_all.py csr5). Output in build/sessions/n3v4b_<tag>.txt."""
import ctypes
import subprocess
import sys
import time
from pathlib import Path

T = Path(__file__).resolve().parent
S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
C = "C:/Users/julie/gt4-lane-F/build/caps/"
pid = int(sys.argv[1])
SYNCHRONIZE = 0x00100000
h = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, pid)
if h:
    ctypes.windll.kernel32.WaitForSingleObject(h, 0xFFFFFFFF)
    ctypes.windll.kernel32.CloseHandle(h)
for tag, cap, frames, dense in (("lapc", C + "lap_csr5.cap", "9000", "7800-8300/10"),
                                ("laps", C + "lap_ssr5r.cap", "10200", "5600-6100/10")):
    with open(S / f"n3v4b_{tag}.txt", "w", encoding="utf-8") as f:
        r = subprocess.run([sys.executable, "-u", str(T / "n3_v4hunt2.py"), tag, cap, frames, dense], stdout=f,
                           stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f"{tag}: exit {r.returncode}", flush=True)
r = subprocess.run([sys.executable, "-u", str(T / "n3_image_all.py"), "csr5"], creationflags=subprocess.CREATE_NO_WINDOW)
print(f"images csr5: exit {r.returncode}", flush=True)

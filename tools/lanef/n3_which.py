#!/usr/bin/env python3
"""n3_which.py TAG CAP FRAME: which of n3v4_TAG's skipped textures are drawn around FRAME (lists FRAME-1 .. FRAME+2),
from one replay with GT4_R10_TEXCOUNT (gt4_host_n3orient.exe), with their looks and widths from n3o_TAG."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, CLONE  # noqa: E402

tag, cap, frame = sys.argv[1], sys.argv[2], int(sys.argv[3])
S = CLONE / "build" / "sessions"
d = S / f"n3_which_{tag}_{frame}"
shutil.rmtree(d, ignore_errors=True)
d.mkdir(parents=True)
env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1", GT4_R10_TEXCOUNT=str(d / "texcount.txt"))
locks = take_slot(f"N3 which texture {tag} @{frame}")
try:
    subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "320x180", "--replay", cap,
                    "--frames", str(frame + 3), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                    "--set", "texture.hd_pack=off"], capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
finally:
    for lock in locks:
        shutil.rmtree(lock, ignore_errors=True)
skipped = set((S / f"n3v4_{tag}" / "skipped.txt").read_text(encoding="ascii").strip().split(","))
drawn = {}
for line in (d / "texcount.txt").read_text(encoding="utf-8").splitlines():
    lst, tid, cnt = line.split()
    if frame - 1 <= int(lst) <= frame + 2:
        drawn[tid] = drawn.get(tid, 0) + int(cnt)
look = {}
for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = l.split("\t")
    look[c[0]] = f"{c[11]} {c[12]} vc {c[13]}"
widths = {}
for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 footprint: host texture (\d+) pieces \d+ area (\S+) width (\S+)", l)
    if m and float(m.group(2)) > 0:
        widths.setdefault(m.group(1), []).append(round(float(m.group(3)), 2))
for tid in sorted(set(drawn) & skipped, key=int):
    print(f"texture {tid}: {drawn[tid]} draws; {look.get(tid, '?')}; item widths {sorted(widths.get(tid, []))[:12]}")
print("-- every v3 texture drawn there (skipped or not):")
for tid in sorted(set(drawn) & set(look), key=int):
    print(f"  {tid}: {drawn[tid]} draws; {look[tid]}; widths {sorted(widths.get(tid, []))[-6:]}; {'skipped' if tid in skipped else ''}")

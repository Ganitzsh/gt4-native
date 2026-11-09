#!/usr/bin/env python3
"""n3_locrun.py: the locator check (the fallback; no tool built). The debug host (gt4_host_n3orient.exe, with the set
log) replays Clubman's B-spec lap with GT4_N3_SETLOG=1; each sent texture set's first 32 bytes are searched for in
crs/route5_club (inflated by vol_cat.py into build/vol_out), and the sets holding the pool textures (n3o_lapc's
amber and white looks, by host id) are reported with their offset in the course file. One heavy slot."""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

S = CLONE / "build" / "sessions"
out = S / "n3_loc"
out.mkdir(parents=True, exist_ok=True)
logf = out / "host.log"
if "--no-replay" not in sys.argv:
    locks = take_slot("N3 locator check: set log over Clubman's lap")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "320x180", "--replay",
                            str(CLONE / "build/caps/lap_csr5.cap"), "--frames", "9000", "--fixed-step", "--preset", "original",
                            "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off", "--set",
                            "lighting.night=enhanced"], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                           env=dict(os.environ, GT4_N3_SETLOG="1", GT4_HOST_RECORD_SCENE="1"))
        logf.write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"replay exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
lines = logf.read_text(encoding="utf-8", errors="replace").split("\n")
course = (CLONE / "build" / "vol_out" / "route5_club.bin").read_bytes()
sets = {}
for l in lines:
    m = re.match(r"N3 set: id (\d+) hash (\w+) lo (\d+) uploads (\d+) bytes (\d+) first (\w+)", l)
    if m and m.group(2) not in sets:
        first = bytes.fromhex(m.group(6))
        hits = []
        at = course.find(first)
        while at >= 0 and len(hits) < 4:
            hits.append(at)
            at = course.find(first, at + 1)
        sets[m.group(2)] = dict(bytes=int(m.group(5)), uploads=int(m.group(4)), hits=hits)
found = sum(1 for s in sets.values() if s["hits"])
print(f"{len(sets)} sets sent; {found} found in crs/route5_club by their first 32 bytes")
for h, s in list(sets.items())[:12]:
    print(f"  set {h}: {s['bytes']} bytes, {s['uploads']} uploads, at {[hex(x) for x in s['hits']]}")
# the pool textures: host ids of the amber / white looks in n3o_lapc's orient.tsv, then their texid lines
pools = set()
for l in (S / "n3o_lapc" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = l.split("\t")
    if f"{c[11]} {c[12]}" in ("128x128 (108, 67, 35)", "256x256 (127, 127, 127)"):
        pools.add(c[0])
for l in lines:
    m = re.match(r"N3 texid: id (\d+) key (\w+) set (\w+) setbase (\d+) tbp (\d+) cbp (\d+) psm (\d+) cpsm (\d+) tw (\d+) th (\d+)", l)
    if m and m.group(1) in pools:
        s = sets.get(m.group(3), {})
        print(f"pool texture id {m.group(1)}: set {m.group(3)} (in the course file at {[hex(x) for x in s.get('hits', [])]}), "
              f"tbp {int(m.group(5)) - int(m.group(4))} cbp {int(m.group(6)) - int(m.group(4))} in the set, psm {m.group(7)} "
              f"cpsm {m.group(8)} size {1 << int(m.group(9))}x{1 << int(m.group(10))}")

#!/usr/bin/env python3
"""n3_place.py TAG CAP FRAMES [--no-replay]: each v3 look of n3o_TAG mapped to the disc texture it really is: one replay
with GT4_N3_SETLOG=1 (gt4_host_n3orient.exe: the same host ids as n3o_TAG) gives every host texture id's set content
hash and place in the set (tbp, cbp, psm); looks are grouped by place, and each group lists its looks with their image
labels (n3_footprint.LABELS plus the checker), item counts and widths. A look without an image that shares a place with
a labelled look is that texture (its summary taken before the texels arrived). One heavy slot."""
import os
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

tag, cap, frames = sys.argv[1], sys.argv[2], sys.argv[3]
S = CLONE / "build" / "sessions"
out = S / f"n3_place_{tag}"
out.mkdir(parents=True, exist_ok=True)
logf = out / "host.log"
if "--no-replay" not in sys.argv:
    locks = take_slot(f"N3 texture places: {tag}")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "320x180", "--replay", cap,
                            "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"], capture_output=True, text=True,
                           creationflags=subprocess.CREATE_NO_WINDOW, env=dict(os.environ, GT4_N3_SETLOG="1", GT4_HOST_RECORD_SCENE="1"))
        logf.write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"replay exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
LABELS = {"128x128 (108, 67, 35)": "pool (amber)", "256x256 (127, 127, 127)": "pool (white)",
          "128x64 (109, 88, 70)": "marking (lanes)", "128x64 (103, 106, 71)": "marking (edge lines)",
          "128x128 (70, 77, 97)": "marking (start/finish checker)"}
place = {}
for l in logf.read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 texid: id (\d+) key \w+ set (\w+) setbase (\d+) tbp (\d+) cbp (\d+) psm (\d+)", l)
    if m:
        base = int(m.group(3))
        place[m.group(1)] = f"set {m.group(2)} tbp {int(m.group(4)) - base} cbp {int(m.group(5)) - base} psm {m.group(6)}"
look = {}
for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = l.split("\t")
    if c[3] == "1" and float(c[4]) >= 0.9 and float(c[9]) > 0:
        look[c[0]] = f"{c[11]} {c[12]}"
widths = {}
for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 footprint: host texture (\d+) pieces \d+ area (\S+) width (\S+)", l)
    if m and float(m.group(2)) > 0:
        widths.setdefault(m.group(1), []).append(float(m.group(3)))
groups = {}
for tid, k in look.items():
    groups.setdefault(place.get(tid, "no place logged"), {}).setdefault(k, []).append(tid)
print(f"{tag}: {len(look)} v3 texture ids in {len(groups)} places")
for p, looks in sorted(groups.items(), key=lambda kv: -sum(len(v) for v in kv[1].values())):
    labels = sorted({LABELS[k] for k in looks if k in LABELS})
    ws = [w for ids in looks.values() for t in ids for w in widths.get(t, [])]
    print(f"{p}: {sum(len(v) for v in looks.values())} ids, {len(looks)} looks; labels {labels or '-'}; widths "
          f"{(f'{min(ws):.2f}/{statistics.median(ws):.2f}/{max(ws):.2f}' if ws else '-')}")
    for k, ids in sorted(looks.items(), key=lambda kv: -len(kv[1]))[:6]:
        print(f"     {k:26} {len(ids):4} ids  {LABELS.get(k, '')}")

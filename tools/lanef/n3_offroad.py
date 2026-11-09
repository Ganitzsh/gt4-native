#!/usr/bin/env python3
"""n3_offroad.py TAG CAP FRAMES [--every N] [--from F] [--control IDS] [--looks-from OTAG]: N3 step 0, the architect's
point (2)/(c). The v3 set (v2 + at least 0.9 of the area at |n.up| >= 0.9, from build/sessions/n3o_OTAG/orient.tsv,
OTAG defaults to TAG), grouped by look (size, texture mean, vertex colour). Per look one replay of CAP with its
textures skipped (GT4_R10_SKIPTEX, debug host gt4_host_f2b0.exe) against one base replay, 640 x 360, a frame every N
(default 50) from F (default 300: the frames before show garbage textures). Changed pixels (any channel > 2) counted
on the road region and off it. Road region: the lower 45 % of the frame plus the mirror box; the HUD's boxes are left
out of both counts. Every look is imaged once at its frame of largest change (base | skipped | changes: red off-road,
yellow on-road) in build/sessions/n3off_TAG/. --control IDS adds a look that must be flagged (the positive control).
A look is flagged when any frame has more than LIMIT (default 150) changed pixels off the road. Resumable
(results.partial.tsv). One heavy slot per replay."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, log, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402


def opt(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


tag, cap, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
EVERY, FROM, LIMIT = int(opt("--every", "50")), int(opt("--from", "300")), int(opt("--limit", "150"))
otag = opt("--looks-from", tag)
S = CLONE / "build" / "sessions"
out = S / f"n3off_{tag}"
out.mkdir(parents=True, exist_ok=True)

looks = {}
for line in (S / f"n3o_{otag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = line.split("\t")
    if c[3] == "1" and float(c[4]) >= 0.9 and float(c[9]) > 0:
        looks.setdefault(f"{c[11]} {c[12]} vc {c[13]}", []).append(c[0])
groups = [(k, ",".join(v)) for k, v in sorted(looks.items())]
if "--control" in sys.argv:
    groups.append(("CONTROL (forced in)", opt("--control", "")))
if "--control-look" in sys.argv:   # e.g. "64x64 (69, 69, 69)": every texture of that size and mean in orient.tsv
    want = opt("--control-look", "")
    ids = [l.split("\t")[0] for l in (S / f"n3o_{otag}" / "orient.tsv").read_text(encoding="utf-8").splitlines()
           if f"{l.split(chr(9))[11]} {l.split(chr(9))[12]}" == want]
    if ids:
        groups.append((f"CONTROL (forced in: {want})", ",".join(ids)))
    else:
        log(f"no texture with the control look {want} in this capture")
log(f"{tag}: {len(groups)} looks ({sum(len(v.split(',')) for _, v in groups)} textures)")

W, H = 640, 360
road = np.zeros((H, W), bool)
road[int(H * 0.55):, :] = True
road[15:63, 222:418] = True           # the mirror box (in-car view)
hud = np.zeros((H, W), bool)
hud[255:, 200:440] = True             # speedometer and tachometer
hud[0:110, 0:120] = True              # position, lap
hud[0:110, 520:] = True               # best and last lap
hud[330:, :] = True                   # the bottom strip (pad hints)
road0, hud0 = road, hud


def replay(name, skip):
    d = out / name
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    env = dict(os.environ, GT4_R10_LOG=str(d / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip:
        env["GT4_R10_SKIPTEX"] = skip
    locks = take_slot(f"N3 off-road check {tag}: {name}")
    try:
        subprocess.run([os.environ.get("N3_HOST", str(CLONE / "build/lanef_run/gt4_host_n3orient.exe")),   # the host the looks' ids came from "--headless", "--size", f"{W}x{H}", "--replay", cap,
                        "--frames", str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--capture-frames", f"{FROM}-{frames}/{EVERY}", "--capture-dir", str(d)],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    return dict(fm.frame_files(str(d)))   # paths: frames are read one pair at a time (memory while waiting)


def attached(m, seed):
    """The changed pixels connected (8-neighbours, through changed pixels) to the seed: the road's change grown outward."""
    g = seed & m
    while True:
        d = g.copy()
        d[1:, :] |= g[:-1, :]
        d[:-1, :] |= g[1:, :]
        d[:, 1:] |= g[:, :-1]
        d[:, :-1] |= g[:, 1:]
        d[1:, 1:] |= g[:-1, :-1]
        d[:-1, :-1] |= g[1:, 1:]
        d[1:, :-1] |= g[:-1, 1:]
        d[:-1, 1:] |= g[1:, :-1]
        d &= m
        if (d == g).all():
            return g
        g = d


partial = out / "results.partial.tsv"
done = {l.split("\t")[1]: l for l in partial.read_text(encoding="utf-8").splitlines()} if partial.exists() else {}
base = None
for i, (look, ids) in enumerate(groups, 1):
    if ids in done:
        print(done[ids])
        continue
    if base is None:
        base = replay("base", None)
    got = replay(f"g{i}", ids)
    worst_off, worst_apart, total_on, total_off, total_apart, best = 0, 0, 0, 0, 0, None
    for n, bp in base.items():
        if n not in got:
            continue
        b, gn = fm.read_ppm(bp), fm.read_ppm(got[n])
        if b.shape[:2] != road.shape:   # (a host that captures at another size: the masks scaled to it)
            ys = (np.arange(b.shape[0]) * H // b.shape[0])[:, None]
            xs = (np.arange(b.shape[1]) * W // b.shape[1])[None, :]
            road, hud = road0[ys, xs], hud0[ys, xs]
        m = (np.abs(gn.astype(np.int16) - b).max(axis=2) > 2) & ~hud
        on, off = int((m & road).sum()), int((m & ~road).sum())
        # off the road and not connected to the road's change: the count that flags (the crude count kept beside it)
        apart = int((m & ~road & ~attached(m, road)).sum()) if off else 0
        total_on += on
        total_off += off
        total_apart += apart
        worst_off = max(worst_off, off)
        worst_apart = max(worst_apart, apart)
        if m.sum() and (best is None or m.sum() > best[1]):
            best = (n, int(m.sum()), b, gn, m)
    png = "-"
    if best:
        n, c, b, g, m = best
        img = (b * 0.6).astype(np.uint8)
        img[m & road] = [255, 220, 0]
        img[m & ~road] = [255, 140, 0]
        img[m & ~road & ~attached(m, road)] = [255, 40, 40]
        png = str(out / f"{i:03d}_frame{n}.png")
        Image.fromarray(np.concatenate([b.astype(np.uint8), g.astype(np.uint8), img], axis=1)).save(png)
    flag = "FLAG" if worst_apart > LIMIT else "ok"
    line = (f"{flag}\t{ids}\t{look}\ton {total_on}\toff {total_off}\tworst-off {worst_off}\tapart {total_apart}\t"
            f"worst-apart {worst_apart}\t{len(base)} frames\t{png}")
    with partial.open("a", encoding="utf-8") as pf:
        pf.write(line + "\n")
    print(line, flush=True)
    best = None
    shutil.rmtree(out / f"g{i}", ignore_errors=True)

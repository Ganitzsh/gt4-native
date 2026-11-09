#!/usr/bin/env python3
"""n3_unseen.py: N3 step 0, the architect's point (4). Every v2 match labelled 'unseen' (not drawn in the label sweep's
sampled frames) is labelled again at frames where it is drawn: from the base replay's per-list draw counts
(label/base/texcount.txt; list n is about frame n - 1), up to three frames with the most draws, at least 40 apart. One
base replay per course capturing all those frames, then one replay per texture (or look) with it skipped
(GT4_R10_SKIPTEX, debug host gt4_host_f2b0.exe, the host the labels were made with), 640 x 360. Per frame: the road
region's mean luminance change (lower 40 %, centre 60 %), changed pixels on and off the road; the frame with the most
changed pixels saved as base | changes in red (build/sessions/n3_TAG/unseen/<n>.png). Label as n3_label.py: 'pool'
when the road's summed change < -0.5, 'decal' when > 0.5, else 'none' (drawn, no road change) or 'never' (no draws in
the whole replay). Resumable: results go to n3_TAG/unseen.partial.tsv as they come. One heavy slot per replay."""
import os
import re
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

ROOT = CLONE / "build" / "sessions"
CAPS = {"csr5": ("C:/Users/julie/gt4-lane-F/build/caps/n3_csr5.cap", False),
        "r5rev": ("C:/Users/julie/gt4-lane-A/build/caps/a5b_r5.cap", True)}
pat = re.compile(r"N3 candidate: texture (\w+) (\d+)x(\d+) mean (\d+),(\d+),(\d+) centre/rim (\S+) core (\S+) \| renderer (\d+) "
                 r"vertices (\d+) colour mean (\S+),(\S+),(\S+) a (\S+) sd")
lumc = lambda c: 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
v2 = lambda p: lumc(p["vc"]) > 16 and p["cr"] < 3 and p["va"] >= 100
lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def props_of(tag):
    lines = (ROOT / f"n3_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")
    host, props = {}, {}
    for l in lines:
        m = re.match(r"N3 host texture (\d+) for (\w+)", l)
        if m:
            host[m.group(2)] = int(m.group(1))
    for l in lines:
        m = pat.match(l)
        if m and host.get(m.group(1)) and host[m.group(1)] not in props:
            g = m.groups()
            props[host[g[0]]] = dict(w=int(g[1]), h=int(g[2]), mean=(int(g[3]), int(g[4]), int(g[5])), cr=float(g[6]),
                                     vc=(float(g[10]), float(g[11]), float(g[12])), va=float(g[13]))
    return props


def replay(tag, cap, name, frames, skip):
    out = ROOT / f"n3_{tag}" / "unseen" / name
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    env = dict(os.environ, GT4_R10_LOG=str(out / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip:
        env["GT4_R10_SKIPTEX"] = skip
    locks = take_slot(f"N3 unseen {tag}: {name}")
    try:
        subprocess.run([str(CLONE / "build/lanef_run/gt4_host_f2b0.exe"), "--headless", "--size", "640x360", "--replay", cap,
                        "--frames", str(max(frames) + 2), "--fixed-step", "--preset", "original", "--set",
                        "display.frame_interpolation=off", "--set", "texture.hd_pack=off",
                        "--capture-frames", ",".join(map(str, sorted(frames))), "--capture-dir", str(out)],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    return {n: fm.read_ppm(p).astype(float) for n, p in fm.frame_files(str(out))}


for tag, (cap, groups) in CAPS.items():
    root = ROOT / f"n3_{tag}"
    props = props_of(tag)
    rows = [l.split("\t") for l in (root / ("labels_groups.tsv" if groups else "labels.tsv")).read_text(encoding="utf-8").splitlines()]
    todo = [r[0] for r in rows if r[1] == "unseen" and props.get(int(r[0].split(",")[0])) and v2(props[int(r[0].split(",")[0])])]
    per_list = {}
    for line in (root / "label" / "base" / "texcount.txt").read_text(encoding="utf-8").splitlines():
        lst, tid, cnt = map(int, line.split())
        per_list.setdefault(tid, {})
        per_list[tid][lst] = per_list[tid].get(lst, 0) + cnt
    pick = {}
    for t in todo:
        counts = {}
        for x in t.split(","):
            for lst, c in per_list.get(int(x), {}).items():
                counts[lst] = counts.get(lst, 0) + c
        lists = []
        for lst, c in sorted(counts.items(), key=lambda kv: -kv[1]):
            if all(abs(lst - g) >= 40 for g in lists):
                lists.append(lst)
            if len(lists) == 3:
                break
        # list n shows in frame n - 1 or n - 2 (n3_label.py samples lists n .. n + 2 for frame n): all three frames
        chosen = sorted({f for lst in lists for f in (lst - 2, lst - 1, lst) if f >= 1})
        pick[t] = (chosen, sum(counts.values()), len(counts))
    log(f"{tag}: {len(todo)} unseen v2 matches; frames {sorted({f for c, _, _ in pick.values() for f in c})}")
    if "--dry" in sys.argv:
        for t, (c, d, n) in pick.items():
            print(f"  {t[:50]}: {d} draws in {n} lists; frames {c}")
        continue
    partial = root / "unseen.partial.tsv"
    done = {l.split("\t")[0] for l in partial.read_text(encoding="utf-8").splitlines()} if partial.exists() else set()
    if all(t in done for t in todo):
        continue
    allframes = sorted({f for c, _, _ in pick.values() for f in c})
    base = replay(tag, cap, "base", allframes, None) if allframes else {}
    for i, t in enumerate(todo, 1):
        if t in done:
            continue
        chosen, draws, lists = pick[t]
        if not chosen:
            line = f"{t}\tnever\t0.000\t0\t0\t0\t0\t-"
        else:
            got = replay(tag, cap, f"u{i}", chosen, t)
            droad, on, off, best = 0.0, 0, 0, None
            for n in chosen:
                if n not in got or n not in base:
                    continue
                b, g = base[n], got[n]
                h, w = b.shape[:2]
                road = np.zeros((h, w), bool)
                road[int(h * 0.6):, int(w * 0.2):int(w * 0.8)] = True
                m = np.abs(g - b).max(axis=2) > 2
                droad += float(lum(g)[road].mean() - lum(b)[road].mean())
                on += int((m & road).sum())
                off += int((m & ~road).sum())
                if m.sum() and (best is None or m.sum() > best[1]):
                    best = (n, int(m.sum()), b, m)
            label = "pool" if droad < -0.5 else ("decal" if droad > 0.5 else "none")
            png = "-"
            if best:
                n, c, b, m = best
                img = (b * 0.6).astype(np.uint8)
                img[m] = [255, 40, 40]
                png = str(root / "unseen" / f"{i}_frame{n}.png")
                Image.fromarray(np.concatenate([b.astype(np.uint8), img], axis=1)).save(png)
            shutil.rmtree(root / "unseen" / f"u{i}", ignore_errors=True)
            line = f"{t}\t{label}\t{droad:.3f}\t{on}\t{off}\t{draws}\t{lists}\t{','.join(map(str, chosen))}\t{png}"
        with partial.open("a", encoding="utf-8") as pf:
            pf.write(line + "\n")
        print(f"{tag} {i}/{len(todo)}: {line}", flush=True)

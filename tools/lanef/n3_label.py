#!/usr/bin/env python3
"""n3_label.py TAG CAP FRAMES: ground truth for N3's property. For every layer-state candidate texture of
build/sessions/n3_TAG (host ids from the N3 log), one replay of CAP with only that texture's model jobs skipped
(GT4_R10_SKIPTEX, the debug host gt4_host_f2b0.exe), 320 x 180, a frame every 100; against a base replay, the road
region's mean luminance change (lower 40 %, centre 60 %) summed over the frames, and the whole frame's changed pixels.
Label: 'pool' when the road gets darker by > 0.5 summed, 'decal' when it gets brighter by > 0.5, else 'none'. Writes
build/sessions/n3_TAG/labels.tsv. One heavy slot per replay (tools/slot.py)."""
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
from r5_session import take_slot, CLONE  # noqa: E402
import frame_metrics as fm  # noqa: E402

tag, cap, frames = sys.argv[1], sys.argv[2], int(sys.argv[3])
# --from N: frames below N are left out (a replay's first ~300 frames show garbage textures, before the game re-uploads)
FROM = int(sys.argv[sys.argv.index("--from") + 1]) if "--from" in sys.argv else 0
root = CLONE / "build" / "sessions" / f"n3_{tag}"
work = root / "label"
log = (root / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")
host = {}
for l in log:
    m = re.match(r"N3 host texture (\d+) for (\w+)", l)
    if m:
        host[m.group(2)] = int(m.group(1))
# --groups: one replay per coarse look (all its host ids skipped together; size, mean, centre/rim and core rounded,
# vertex colour and alpha), for a course whose textures repeat under fresh identities
groups = "--groups" in sys.argv
looks = {}
pat = re.compile(r"N3 candidate: texture (\w+) (\d+x\d+) mean (\S+) centre/rim (\S+) core (\S+) \| renderer \d+ vertices \d+ "
                 r"colour mean (\S+) a (\S+) sd")
for l in log:
    m = pat.match(l)
    if m and host.get(m.group(1)):
        key = (m.group(2), m.group(3), round(float(m.group(4))), round(float(m.group(5)), 1), m.group(6), m.group(7)) if groups \
            else (host[m.group(1)],)
        looks.setdefault(key, [])
        if host[m.group(1)] not in looks[key]:
            looks[key].append(host[m.group(1)])
ids = [",".join(map(str, v)) for v in looks.values()]


def replay(name, skip):
    out = work / name
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    env = dict(__import__("os").environ, GT4_R10_LOG=str(out / "r10.log"), GT4_R10_DRAWLIST="0,0,1,1")
    if skip is not None:
        env["GT4_R10_SKIPTEX"] = str(skip)
    else:
        env["GT4_R10_TEXCOUNT"] = str(out / "texcount.txt")   # (the base: each texture's draws per list)
    locks = take_slot(f"N3 labels {tag}: {name}")
    try:
        subprocess.run([str(CLONE / "build/lanef_run/gt4_host_f2b0.exe"), "--headless", "--size", "320x180", "--replay", cap,
                        "--frames", str(frames), "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                        "--set", "texture.hd_pack=off", "--capture", str(out / "f.ppm"), "--capture-every", "100"],
                       capture_output=True, text=True, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
    return {n: fm.read_ppm(p).astype(float) for n, p in fm.frame_files(str(out))}


lum = lambda a: 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
# resumable: each result is appended to a partial file as it comes; a rerun skips what is there
partial = root / ("labels_groups.partial.tsv" if groups else "labels.partial.tsv")
done = {}
if partial.exists():
    for line in partial.read_text(encoding="utf-8").splitlines():
        t, lab, d, c = line.split("\t")
        done[t] = (t, lab, float(d), int(c))
base = replay("base", None)
# draws per texture within one list of each sampled frame (list numbers count from 1; frame n's list is about n + 1)
sampled = set()
base = {n: b for n, b in base.items() if n >= FROM}
for n in base:
    sampled.update((n, n + 1, n + 2))
drawn = {}
for line in (work / "base" / "texcount.txt").read_text(encoding="utf-8").splitlines():
    lst, tid, cnt = map(int, line.split())
    if lst in sampled:
        drawn[tid] = drawn.get(tid, 0) + cnt
draws_of = lambda tid: sum(drawn.get(int(x), 0) for x in tid.split(","))
rows = []
for i, tid in enumerate(ids, 1):
    if tid in done:
        t0, lab0, d0, c0 = done[tid]
        rows.append((t0, "unseen" if lab0 == "none" and not draws_of(tid) else lab0, d0, c0, draws_of(tid)))
        continue
    got = replay(f"t{i}", tid)
    droad, changed = 0.0, 0
    for n, b in base.items():
        if n not in got:
            continue
        h, w = b.shape[:2]
        r = (slice(int(h * 0.6), h), slice(int(w * 0.2), int(w * 0.8)))
        droad += float(lum(got[n])[r].mean() - lum(b)[r].mean())
        changed += int((np.abs(got[n] - b).max(axis=2) > 2).sum())
    label = "pool" if droad < -0.5 else ("decal" if droad > 0.5 else ("none" if draws_of(tid) else "unseen"))
    rows.append((tid, label, droad, changed, draws_of(tid)))
    with partial.open("a", encoding="utf-8") as pf:
        pf.write(f"{tid}\t{label}\t{droad:.3f}\t{changed}\n")
    print(f"{i}/{len(ids)} texture {tid[:40]}: road {droad:+.2f} summed, {changed} pixels changed, {draws_of(tid)} draws: {label}", flush=True)
    shutil.rmtree(work / f"t{i}", ignore_errors=True)
(root / ("labels_groups.tsv" if groups else "labels.tsv")).write_text("".join(f"{t}\t{l}\t{d:.3f}\t{c}\t{n}\n" for t, l, d, c, n in rows), encoding="utf-8")
print("labels:", {k: sum(1 for r in rows if r[1] == k) for k in ("pool", "decal", "none", "unseen")})

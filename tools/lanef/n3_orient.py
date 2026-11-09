#!/usr/bin/env python3
"""n3_orient.py TAG CAP FRAMES [--labels LTAG] [--no-replay]: N3 step 0, the architect's point (1). The local debug host
gt4_host_n3orient.exe replays CAP (in place) with GT4_N3_LOG=1 and lighting.night = enhanced (scene split and B6's
classifier on); its 'N3 orient' lines give each lit-road-layer item's triangle area by |n| per world axis (at the
item's first sight, its instance's objectToWorld applied) and how far that transform's rotation is from identity.
Per texture (summed over its items): the area share with |n . up| >= 0.9, and the area-weighted mean |n . up|. Joined
with n3_LTAG's labels (Clubman: per texture; Route 5 Reverse: per look) and v2's verdict from the N3 candidate lines.
Output: build/sessions/n3o_TAG/host.log, orient.tsv; a table per label on stdout. One heavy slot."""
import math
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r5_session import take_slot, log, CLONE  # noqa: E402

tag, cap, frames = sys.argv[1], sys.argv[2], sys.argv[3]
ltag = sys.argv[sys.argv.index("--labels") + 1] if "--labels" in sys.argv else None
ROOT = CLONE / "build" / "sessions"
out = ROOT / f"n3o_{tag}"
if "--no-replay" not in sys.argv:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    locks = take_slot(f"N3 step 0 orientation: {tag}")
    try:
        r = subprocess.run([str(CLONE / "build/lanef_run/gt4_host_n3orient.exe"), "--headless", "--size", "640x360", "--replay", cap,
                            "--frames", frames, "--fixed-step", "--preset", "original", "--set", "display.frame_interpolation=off",
                            "--set", "texture.hd_pack=off", "--set", "lighting.night=enhanced"],
                           capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW,
                           env=dict(os.environ, GT4_N3_LOG="1", GT4_HOST_RECORD_SCENE="1"))
        (out / "host.log").write_text(r.stdout + r.stderr, encoding="utf-8", errors="replace")
        log(f"replay {tag}: exit {r.returncode}")
    finally:
        for lock in locks:
            shutil.rmtree(lock, ignore_errors=True)
lines = (out / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")

host = {}
for l in lines:
    m = re.match(r"N3 host texture (\d+) for (\w+)", l)
    if m:
        host[m.group(2)] = int(m.group(1))
pat = re.compile(r"N3 candidate: texture (\w+) (\d+)x(\d+) mean (\d+),(\d+),(\d+) centre/rim (\S+) core (\S+) \| renderer (\d+) "
                 r"vertices (\d+) colour mean (\S+),(\S+),(\S+) a (\S+) sd")
props = {}
for l in lines:
    m = pat.match(l)
    if m and host.get(m.group(1)) and host[m.group(1)] not in props:
        g = m.groups()
        props[host[g[0]]] = dict(size=f"{g[1]}x{g[2]}", mean=(int(g[3]), int(g[4]), int(g[5])), cr=float(g[6]),
                                 vc=(float(g[10]), float(g[11]), float(g[12])), va=float(g[13]))
lumc = lambda c: 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
v2 = lambda p: lumc(p["vc"]) > 16 and p["cr"] < 3 and p["va"] >= 100

# the same host ids as the labels' runs? (identity -> id against n3_LTAG's log)
if ltag:
    old = {}
    for l in (ROOT / f"n3_{ltag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
        m = re.match(r"N3 host texture (\d+) for (\w+)", l)
        if m:
            old[m.group(2)] = int(m.group(1))
    common = [k for k in host if k in old]
    same = sum(host[k] == old[k] for k in common)
    print(f"host ids against n3_{ltag}: {same} of {len(common)} identities have the same id")

opat = re.compile(r"N3 orient: host texture (\d+) flags (\w+) rotdev (\S+) scale (\S+),(\S+),(\S+) triangles (\d+) area (\S+) "
                  r"axis (\S+),(\S+),(\S+) binsx (\S+) binsy (\S+) binsz (\S+)")
tex = {}
rot = {"items": 0, "identity": 0, "maxdev": 0.0, "flags": {}}
for l in lines:
    m = opat.match(l)
    if not m:
        continue
    g = m.groups()
    tid, area, dev = int(g[0]), float(g[7]), float(g[2])
    rot["items"] += 1
    rot["identity"] += dev < 0.01
    rot["maxdev"] = max(rot["maxdev"], dev)
    rot["flags"][g[1]] = rot["flags"].get(g[1], 0) + 1
    t = tex.setdefault(tid, dict(area=0.0, axis=[0.0] * 3, bins=[[0.0] * 10 for _ in range(3)], items=0, dev=0.0, mins=[]))
    t["items"] += 1
    t["area"] += area
    t["dev"] = max(t["dev"], dev)
    for q in range(3):
        t["axis"][q] += area * float(g[8 + q])
        for j, x in enumerate(g[11 + q].split(",")):
            t["bins"][q][j] += area * float(x)
    t["mins"].append((area, [float(g[8]), float(g[9]), float(g[10])]))
print(f"items: {rot['items']}; rotation identity (deviation < 0.01) {rot['identity']}, largest deviation {rot['maxdev']:.3f}; "
      f"instance flags {rot['flags']}")
tot = [sum(t["axis"][q] for t in tex.values()) / max(1e-9, sum(t["area"] for t in tex.values())) for q in range(3)]
print(f"all items: area-weighted mean |n| per axis x {tot[0]:.3f} y {tot[1]:.3f} z {tot[2]:.3f}")
UP = int(os.environ.get("N3_UP", "1"))   # world axis taken as up (1: y); checked against the axis means above

labels = {}
if ltag:
    f = ROOT / f"n3_{ltag}" / ("labels_groups.tsv" if (ROOT / f"n3_{ltag}" / "labels_groups.tsv").exists() else "labels.tsv")
    for line in f.read_text(encoding="utf-8").splitlines():
        c = line.split("\t")
        for x in c[0].split(","):
            labels[int(x)] = (c[1], c[0])
    u = ROOT / f"n3_{ltag}" / "unseen.partial.tsv"   # point (4)'s labels at frames where drawn replace 'unseen'
    if u.exists():
        for line in u.read_text(encoding="utf-8").splitlines():
            c = line.split("\t")
            for x in c[0].split(","):
                labels[int(x)] = (c[1] + "*", c[0])
rows = []
for tid, t in tex.items():
    a = t["area"] or 1e-9
    share = t["bins"][UP][9] / a                # |n . up| >= 0.9 (within about 26 degrees of flat)
    share8 = (t["bins"][UP][8] + t["bins"][UP][9]) / a
    mean = t["axis"][UP] / a
    worst = min(v[UP] for _, v in t["mins"])   # the steepest item's mean |n . up|
    p = props.get(tid)
    lab, look = labels.get(tid, ("-", str(tid)))
    rows.append(dict(tid=tid, look=look, lab=lab, v2=bool(p and v2(p)), p=p, share=share, share8=share8, mean=mean, worst=worst,
                     items=t["items"], area=t["area"], dev=t["dev"]))
with (out / "orient.tsv").open("w", encoding="utf-8") as f:
    for r in sorted(rows, key=lambda r: (r["lab"], -r["share"])):
        p = r["p"] or {}
        f.write(f"{r['tid']}\t{r['look']}\t{r['lab']}\t{int(r['v2'])}\t{r['share']:.3f}\t{r['share8']:.3f}\t{r['mean']:.3f}\t"
                f"{r['worst']:.3f}\t{r['items']}\t{r['area']:.0f}\t{r['dev']:.3f}\t{p.get('size', '?')}\t{p.get('mean', '?')}\t"
                f"{tuple(round(x) for x in p.get('vc', ()))}\n")
deg = lambda c: math.degrees(math.acos(max(-1.0, min(1.0, c))))
for lab in sorted({r["lab"] for r in rows}):
    rs = sorted([r for r in rows if r["lab"] == lab], key=lambda r: r["share"])
    v = [r for r in rs if r["v2"]]
    print(f"== {lab}: {len(rs)} textures ({len(v)} v2 matches); share |n.up| >= 0.9, v2 matches: "
          f"{', '.join(f'{r['share']:.2f}' for r in v) or '-'}")
    for r in v if lab.startswith("pool") else []:
        print(f"     pool {r['tid']}: share {r['share']:.3f} mean |n.up| {r['mean']:.3f} ({deg(r['mean']):.1f} deg), steepest item "
              f"{r['worst']:.3f} ({deg(r['worst']):.1f} deg), {r['items']} items")
v2rows = [r for r in rows if r["v2"]]
print(f"v2 matches with items: {len(v2rows)} of {sum(1 for p in props.values() if v2(p))} v2 materials in the log")

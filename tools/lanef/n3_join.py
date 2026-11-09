#!/usr/bin/env python3
"""n3_join.py TAG: N3 step 0's labels (build/sessions/n3_TAG/labels.tsv) joined with each texture's logged properties
(texture summary, first item's vertex colour, renderer), sorted by label then by the road change, with each pool's
share of all pools' road change. Labels: pool (the road darker without it), decal (brighter), none."""
import re
import sys

tag = sys.argv[1]
root = f"C:/Users/julie/gt4-lane-F/build/sessions/n3_{tag}/"
log = open(root + "host.log", encoding="utf-8", errors="replace").read().split("\n")
host = {}
for l in log:
    m = re.match(r"N3 host texture (\d+) for (\w+)", l)
    if m:
        host[m.group(2)] = int(m.group(1))
pat = re.compile(r"N3 candidate: texture (\w+) (\d+)x(\d+) mean (\d+),(\d+),(\d+) centre/rim (\S+) core (\S+) \| renderer (\d+) "
                 r"vertices (\d+) colour mean (\S+),(\S+),(\S+) a (\S+) sd \S+ a \S+ tfx (\d+) tcc (\d+)")
props = {}
for l in log:
    m = pat.match(l)
    if m:
        g = m.groups()
        tid = host.get(g[0])
        if tid and tid not in props:
            props[tid] = dict(size=f"{g[1]}x{g[2]}", mean=(int(g[3]), int(g[4]), int(g[5])), cr=float(g[6]), core=float(g[7]),
                              renderer=int(g[8]), vc=(float(g[10]), float(g[11]), float(g[12])), va=float(g[13]))
labels = []
for line in open(root + "labels.tsv", encoding="utf-8"):
    t, lab, d, c = line.rstrip("\n").split("\t")
    labels.append((int(t), lab, float(d), int(c)))
total = sum(-d for _, lab, d, _ in labels if lab == "pool") or 1.0
order = {"pool": 0, "decal": 1, "none": 2}
print(f"{tag}: {len(labels)} textures; pools' road change {-total:.2f} summed")
for t, lab, d, c in sorted(labels, key=lambda r: (order[r[1]], r[2])):
    p = props.get(t, {})
    tm, vc = p.get("mean", (0, 0, 0)), p.get("vc", (0, 0, 0))
    added = sum(w * tm[k] * vc[k] / 128 for k, w in enumerate((0.2126, 0.7152, 0.0722)))
    share = f"{100 * -d / total:5.1f} %" if lab == "pool" else "       "
    if lab != "none" or c:
        print(f"  {lab:5} {t:5d} road {d:+7.2f} {share} px {c:6d} | {p.get('size', '?'):>7} mean {tm} c/r {p.get('cr', 0):6.2f} "
              f"core {p.get('core', 0):.3f} vc {tuple(round(x) for x in vc)} va {p.get('va', 0):.0f} r{p.get('renderer', '?')} "
              f"added {added:.1f}")
print(f"  (and {sum(1 for r in labels if r[1] == 'none' and not r[3])} textures that change nothing)")

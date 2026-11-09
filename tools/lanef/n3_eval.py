#!/usr/bin/env python3
"""n3_eval.py: N3 step 0's candidate rules against the labels of both courses (Clubman per texture: labels.tsv; Route 5
Reverse per look: labels_groups[.partial].tsv). For each rule: pools caught (and their share of the road's change),
pools missed (and their share), non-pools caught (false positives)."""
import re
from pathlib import Path

ROOT = Path("C:/Users/julie/gt4-lane-F/build/sessions")
pat = re.compile(r"N3 candidate: texture (\w+) (\d+)x(\d+) mean (\d+),(\d+),(\d+) centre/rim (\S+) core (\S+) \| renderer (\d+) "
                 r"vertices (\d+) colour mean (\S+),(\S+),(\S+) a (\S+) sd")


def course(tag, groups):
    log = (ROOT / f"n3_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")
    host, props = {}, {}
    for l in log:
        m = re.match(r"N3 host texture (\d+) for (\w+)", l)
        if m:
            host[m.group(2)] = int(m.group(1))
    for l in log:
        m = pat.match(l)
        if m and host.get(m.group(1)) and host[m.group(1)] not in props:
            g = m.groups()
            props[host[g[0]]] = dict(w=int(g[1]), h=int(g[2]), mean=(int(g[3]), int(g[4]), int(g[5])), cr=float(g[6]), core=float(g[7]),
                                     vc=(float(g[10]), float(g[11]), float(g[12])), va=float(g[13]))
    name = "labels_groups" if groups else "labels"
    f = ROOT / f"n3_{tag}" / f"{name}.tsv"
    if not f.exists():
        f = ROOT / f"n3_{tag}" / f"{name}.partial.tsv"
    before = ROOT / f"n3_{tag}" / "labelled_before_freeze.txt"
    frozen = set(before.read_text(encoding="ascii").split()) if before.exists() else None
    rows = []
    for line in f.read_text(encoding="utf-8").splitlines():
        cols = line.split("\t")
        t, lab, d = cols[0], cols[1], float(cols[2])
        first = int(t.split(",")[0])
        rows.append(dict(p=props.get(first, {}), lab=lab, d=d, n=len(t.split(",")), draws=int(cols[4]) if len(cols) > 4 else -1,
                         held=frozen is not None and t not in frozen))
    return rows


lum = lambda c: 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
rules = {
    "v1 added light > 60, flat, core < 5 %, va >= 100": lambda p: sum(w * p["mean"][k] * p["vc"][k] / 128 for k, w in enumerate((0.2126, 0.7152, 0.0722))) > 60
    and p["cr"] < 3 and p["core"] < 0.05 and p["va"] >= 100,
    "v2 vertex colour lum > 16, flat, va >= 100": lambda p: lum(p["vc"]) > 16 and p["cr"] < 3 and p["va"] >= 100,
}
sets = []
for tag, groups in (("csr5", False), ("r5rev", True)):
    allrows = course(tag, groups)
    sets.append((tag, allrows))
    held = [r for r in allrows if r["held"]]
    if held:
        sets.append((tag + " held-out (labelled after v2 was frozen)", held))
for tag, rows in sets:
    total = sum(-r["d"] for r in rows if r["lab"] == "pool") or 1
    print(f"== {tag}: {len(rows)} labelled ({'looks' if tag.startswith('r5rev') else 'textures'}), {sum(r['lab'] == 'pool' for r in rows)} pools, "
          f"{sum(r['lab'] == 'decal' for r in rows)} decals")
    for name, rule in rules.items():
        hit = [r for r in rows if r["p"] and rule(r["p"])]
        tp = [r for r in hit if r["lab"] == "pool"]
        fp = [r for r in hit if r["lab"] != "pool"]
        miss = [r for r in rows if r["lab"] == "pool" and not (r["p"] and rule(r["p"]))]
        print(f"  {name}: pools caught {len(tp)} ({100 * sum(-r['d'] for r in tp) / total:.1f} % of the change), missed {len(miss)} "
              f"({100 * sum(-r['d'] for r in miss) / total:.1f} %), non-pools caught {len(fp)} "
              f"({', '.join(sorted(set(r['lab'] for r in fp))) or '-'}; road {sum(r['d'] for r in fp):+.2f})")
        for r in fp[:40]:
            p = r["p"]
            print(f"      fp {r['lab']} d {r['d']:+.2f} draws {r['draws']}: {p['w']}x{p['h']} mean {p['mean']} c/r {p['cr']:.2f} core {p['core']:.3f} vc "
                  f"{tuple(round(x) for x in p['vc'])} va {p['va']:.0f}")

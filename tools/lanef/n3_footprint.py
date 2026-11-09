#!/usr/bin/env python3
"""n3_footprint.py TAG...: N3 v4's table from build/sessions/n3o_TAG/host.log (the footprint host's 'N3 footprint' lines,
one per lit-road-layer item at first sight). Per v3 look (orient.tsv: v2 + flat), labelled from the images: the items'
footprint width (2 x area / perimeter of each connected up-facing piece, area-weighted per item; and the PCA width),
and the texture's world size in world units per texel along u and v (|dP/ds| / width, |dP/dt| / height). Per look:
the items' minimum, median and maximum, so the gap between pools and markings shows."""
import re
import statistics
import sys
from pathlib import Path

S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
# looks by image (skip on against off, full-res zooms): key = "size (mean)"
LABELS = {
    "128x128 (108, 67, 35)": "pool (amber)",
    "256x256 (127, 127, 127)": "pool (white)",
    "128x64 (109, 88, 70)": "marking (lane dashes, edge lines)",
    "128x64 (103, 106, 71)": "marking (edge lines)",
}
SEOUL = "marking (Seoul: crossing, lane lines; group image)"
fp = re.compile(r"N3 footprint: host texture (\d+) pieces (\d+) area (\S+) width (\S+) pcawidth (\S+) longest (\S+) dPds (\S+) dPdt (\S+)")
rows = []
for tag in [a for a in sys.argv[1:] if not a.startswith("--")]:
    looks = {}
    for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
        c = l.split("\t")
        if c[3] == "1" and float(c[4]) >= 0.9 and float(c[9]) > 0:
            looks[c[0]] = (f"{c[11]} {c[12]}", c[11], c[13])
    for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
        m = fp.match(l)
        if not m or m.group(1) not in looks or float(m.group(3)) <= 0:
            continue
        key, size, vc = looks[m.group(1)]
        w, h = map(int, size.split("x"))
        label = SEOUL if tag == "seoul" else LABELS.get(key, "? (not imaged)")
        rows.append(dict(tag=tag, key=key, label=label, width=float(m.group(4)), pca=float(m.group(5)),
                         u=float(m.group(7)) / w, v=float(m.group(8)) / h, area=float(m.group(3))))
groups = {}
for r in rows:
    groups.setdefault((r["label"], r["key"], r["tag"]), []).append(r)
fmt = lambda xs: f"{min(xs):7.3f} {statistics.median(xs):7.3f} {max(xs):7.3f}"


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


print(f"{'label':36} {'look':24} {'capture':7} {'items':>5}   width min/med/max     p05     p95  material  "
      f"pca width min/med/max  u per texel min/med/max  v per texel min/med/max")
for (label, key, tag), rs in sorted(groups.items()):
    ws = [r["width"] for r in rs]
    mat = sum(r["width"] * r["area"] for r in rs) / max(1e-9, sum(r["area"] for r in rs))   # area-weighted, per material
    print(f"{label[:36]:36} {key:24} {tag:7} {len(rs):5}  {fmt(ws)} {pct(ws, .05):7.3f} {pct(ws, .95):7.3f} {mat:8.3f}  "
          f"{fmt([r['pca'] for r in rs])}  {fmt([r['u'] for r in rs])}  {fmt([r['v'] for r in rs])}")
T = float(next((a.split("=")[1] for a in sys.argv if a.startswith("--threshold=")), "0.8"))
for kind in ("pool", "marking"):
    rs = [r for r in rows if r["label"].startswith(kind)]
    wrong = [r for r in rs if (r["width"] < T) == (kind == "pool")]
    print(f"items of {kind} looks on the wrong side of {T} m: {len(wrong)} of {len(rs)} "
          f"({sum(r['area'] for r in wrong):.1f} of {sum(r['area'] for r in rs):.1f} area)"
          + (f"; e.g. {[(r['tag'], r['key'], round(r['width'], 3), round(r['area'], 2)) for r in sorted(wrong, key=lambda r: r['width'])[:6]]}" if wrong else ""))

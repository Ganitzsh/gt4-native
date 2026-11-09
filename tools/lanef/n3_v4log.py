#!/usr/bin/env python3
"""n3_v4log.py TAG: what v4 skipped in a hunt (build/sessions/n3v4b_TAG/v4.log): items judged, items skipped, and the
skipped textures grouped by look (n3o_TAG's orient.tsv by host id; n3_place_TAG's set log for the place, when there),
with item counts, widths and texel sizes; also the 'keep' items that came closest (width >= 0.9, texel < 0.03)."""
import re
import sys
from pathlib import Path

S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
tag = sys.argv[1]
look = {}
for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
    c = l.split("\t")
    look[c[0]] = f"{c[11]} {c[12]} vc {c[13]}"
place = {}
pl = S / f"n3_place_{tag}" / "host.log"
if pl.exists():
    for l in pl.read_text(encoding="utf-8", errors="replace").split("\n"):
        m = re.match(r"N3 texid: id (\d+) key \w+ set (\w+) setbase (\d+) tbp (\d+) cbp (\d+)", l)
        if m:
            place[m.group(1)] = f"set {m.group(2)[:8]} tbp {int(m.group(4)) - int(m.group(3))} cbp {int(m.group(5)) - int(m.group(3))}"
pat = re.compile(r"N3 v4: host texture (\d+) identity (\w+) vc (\S+) a (\S+) c/r (\S+) share (\S+) width (\S+) texel (\S+) -> (\w+)")
rows = [m.groups() for m in map(pat.match, (S / f"n3v4b_{tag}" / "v4.log").read_text(encoding="utf-8").splitlines()) if m]
skip = [r for r in rows if r[8] == "skip"]
print(f"{tag}: {len(rows)} items judged, {len(skip)} skipped, {len({r[1] for r in skip})} identities skipped")
byident = {}   # identity -> look, from the candidate lines (keyed by identity: stable, unlike debug host ids)
for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 candidate: texture (\w+) (\d+x\d+) mean (\d+),(\d+),(\d+) .* colour mean (\S+),(\S+),(\S+) a", l)
    if m and m.group(1) not in byident:
        byident[m.group(1)] = f"{m.group(2)} ({m.group(3)}, {m.group(4)}, {m.group(5)}) vc ({round(float(m.group(6)))}, {round(float(m.group(7)))}, {round(float(m.group(8)))})"
groups = {}
for r in skip:
    groups.setdefault(byident.get(r[1], "? identity " + r[1]), []).append(r)
for k, rs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
    ws = [float(r[6]) for r in rs]
    ts = [float(r[7]) for r in rs]
    places = sorted({place.get(r[0], "-") for r in rs})
    print(f"  {k:44} {len(rs):5} items, {len({r[1] for r in rs}):3} identities; width {min(ws):.2f}-{max(ws):.2f}; "
          f"texel {min(ts):.3f}-{max(ts):.3f}; places {places[:3]}{' ...' if len(places) > 3 else ''}")
near = [r for r in rows if r[8] == "keep" and float(r[6]) >= 0.9 and float(r[5]) >= 0.9 and float(r[2]) > 16]
print(f"kept although wide, flat and lit (texel < 0.03 or c/r or alpha): {len(near)}")
for r in near[:12]:
    print(f"    id {r[0]} {byident.get(r[1], '?')}: width {r[6]} texel {r[7]} c/r {r[4]} a {r[3]}")

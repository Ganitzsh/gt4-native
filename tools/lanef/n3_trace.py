#!/usr/bin/env python3
"""n3_trace.py TAG: the architect's point (2). For each v4 identity judged on a memory-path texture (no set place) with a
look that isn't a known texture's, the same item geometry (identical width and texel size to 3 decimals) judged under a
set-served identity, with which came first in the log (time order). Prints a few examples and a count."""
import re
import sys
from pathlib import Path

S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
tag = sys.argv[1]
byident = {}
for l in (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 candidate: texture (\w+) (\d+x\d+) mean (\d+),(\d+),(\d+) ", l)
    if m and m.group(1) not in byident:
        byident[m.group(1)] = f"{m.group(2)} ({m.group(3)}, {m.group(4)}, {m.group(5)})"
setident = set()
for l in (S / f"n3_place_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 texid: id \d+ key (\w+) set", l)
    if m:
        setident.add(m.group(1).lstrip("0") or "0")
pat = re.compile(r"N3 v4: host texture (\d+) identity (\w+) vc \S+ a \S+ c/r \S+ share \S+ width (\S+) texel (\S+) -> (\w+)")
rows = [(i, m.group(2), m.group(3), m.group(4), m.group(5))
        for i, m in enumerate(map(pat.match, (S / f"n3v4b_{tag}" / "v4.log").read_text(encoding="utf-8").splitlines())) if m]
norm = lambda h: h.lstrip("0") or "0"
real = {}
for i, ident, w, t, verdict in rows:
    if norm(ident) in setident:
        real.setdefault((w, t), []).append((i, ident, verdict))
odd = [(i, ident, w, t, verdict) for i, ident, w, t, verdict in rows if norm(ident) not in setident and verdict == "skip"]
matched = [(o, real[(o[2], o[3])]) for o in odd if (o[2], o[3]) in real]
print(f"{tag}: {len(odd)} skipped items on memory-path identities; {len(matched)} share their exact geometry with a "
      f"set-served identity; {len({o[1] for o in odd})} memory-path identities")
before = sum(1 for o, rs in matched if o[0] < min(r[0] for r in rs))
print(f"  the memory-path judgement came first in {before} of {len(matched)}")
for o, rs in matched[:5]:
    r = min(rs)
    print(f"  line {o[0]}: identity {o[1]} ({byident.get(o[1], '?')}), width {o[2]} texel {o[3]} -> {o[4]}; "
          f"line {r[0]}: identity {r[1]} ({byident.get(r[1], '?')}) -> {r[2]}")

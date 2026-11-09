#!/usr/bin/env python3
"""which_course.py LOG: which crs/ file the texture sets of a set log (GT4_N3_SETLOG) come from: each set's first 32
bytes searched in the inflated course files in build/vol_out; prints the count found per file."""
import re
import sys
from pathlib import Path

files = {p.stem: p.read_bytes() for p in Path("C:/Users/julie/gt4-lane-F/build/vol_out").glob("*.bin")}
seen, hits = set(), {k: 0 for k in files}
only = {}   # sets found in exactly one direction (forward files only, or reverse files only)
for l in Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").split("\n"):
    m = re.match(r"N3 set: id \d+ hash (\w+) lo \d+ uploads \d+ bytes \d+ first (\w+)", l)
    if m and m.group(1) not in seen:
        seen.add(m.group(1))
        first = bytes.fromhex(m.group(2))
        found = [k for k, b in files.items() if first in b]
        for k in found:
            hits[k] += 1
        dirs = {"reverse" if k.startswith("r_") else "forward" for k in found}
        if len(dirs) == 1:
            d = dirs.pop()
            only[d] = only.get(d, 0) + 1
print(f"{len(seen)} sets:", ", ".join(f"{k} {v}" for k, v in sorted(hits.items(), key=lambda kv: -kv[1])))
print("found in one direction only:", only or "none")

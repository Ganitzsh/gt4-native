#!/usr/bin/env python3
"""A3b step 0: the camera object / entry changes in an a3b_<session>.txt log (py -I a3b_cams.py FILE)."""
import collections
import re
import sys

rows = []
for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"\[(\d+\.\d+)\].*a3b cam (\S+) entry (-?\d+)", line)
    if m:
        rows.append((float(m.group(1)), m.group(2), int(m.group(3))))
print(len(rows), "cam lines;", len({r[1] for r in rows}), "camera objects;",
      dict(collections.Counter(r[2] for r in rows)))
prev = None
for t, cam, entry in rows:
    if prev and (cam != prev[1] or entry != prev[2]):
        what = "object" if cam != prev[1] else "entry"
        print(f"  {t:.3f}: {what} {prev[1]}/{prev[2]} -> {cam}/{entry}")
    prev = (t, cam, entry)

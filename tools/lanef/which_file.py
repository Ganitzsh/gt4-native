#!/usr/bin/env python3
"""which_file.py LOG...: per set log, the sets found in exactly one of the four Route 5 course files (by their first 32
bytes, and by their first 4 KiB where the log has only 32: a 32-byte prefix can repeat); prints counts per file."""
import re
import sys
from pathlib import Path

files = {p.stem: p.read_bytes() for p in Path("C:/Users/julie/gt4-lane-F/build/vol_out").glob("*.bin")}
for log in sys.argv[1:]:
    seen, single = set(), {}
    for l in Path(log).read_text(encoding="utf-8", errors="replace").split("\n"):
        m = re.match(r"N3 set: id \d+ hash (\w+) lo \d+ uploads \d+ bytes \d+ first (\w+)", l)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            found = [k for k, b in files.items() if bytes.fromhex(m.group(2)) in b]
            if len(found) == 1:
                single[found[0]] = single.get(found[0], 0) + 1
    print(f"{Path(log).parent.name}: {len(seen)} sets; found in one file only: {single or 'none'}")

#!/usr/bin/env python3
"""crs_peek.py FILE: the first 256 bytes, and every 4-character ASCII tag-like run (magic words) with its offset."""
import re
import sys

b = open(sys.argv[1], "rb").read()
print(b[:256].hex(" ", 4))
tags = {}
for m in re.finditer(rb"[A-Za-z][A-Za-z0-9_]{3}", b):
    s = m.group().decode()
    tags.setdefault(s, []).append(m.start())
common = sorted(((len(v), k, v[:4]) for k, v in tags.items() if 2 <= len(v) <= 400), reverse=True)[:40]
for n, k, offs in common:
    print(f"{k!r} x{n} at {[hex(o) for o in offs]}")

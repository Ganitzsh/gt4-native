#!/usr/bin/env python3
"""n3_looks.py TAG...: for each capture's v3 looks (n3o_TAG/orient.tsv), the texture summary from its N3 candidate lines:
size, mean, centre/rim, alpha core, vertex colour and alpha, texture function; one line per distinct summary."""
import re
import sys
from pathlib import Path

S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
pat = re.compile(r"N3 candidate: texture (\w+) (\d+x\d+) mean (\S+) centre/rim (\S+) core (\S+) \| renderer \d+ vertices \d+ "
                 r"colour mean (\S+) a (\S+) sd (\S+) a (\S+) tfx (\d+) tcc (\d+)")
for tag in sys.argv[1:]:
    log = (S / f"n3o_{tag}" / "host.log").read_text(encoding="utf-8", errors="replace").split("\n")
    host = {}
    for l in log:
        m = re.match(r"N3 host texture (\d+) for (\w+)", l)
        if m:
            host[m.group(2)] = m.group(1)
    v3 = set()
    for l in (S / f"n3o_{tag}" / "orient.tsv").read_text(encoding="utf-8").splitlines():
        c = l.split("\t")
        if c[3] == "1" and float(c[4]) >= 0.9 and float(c[9]) > 0:
            v3.add(c[0])
    seen = {}
    for l in log:
        m = pat.match(l)
        if m and host.get(m.group(1)) in v3:
            g = m.groups()
            key = (g[1], g[2], g[3], g[4], g[5], g[6], g[9], g[10])
            seen.setdefault(key, []).append(host[g[0]])
    print(f"== {tag}")
    for k, ids in sorted(seen.items()):
        print(f"  {k[0]:8} mean {k[1]:12} c/r {k[2]:5} core {k[3]:5} vc {k[4]:16} a {k[5]:6} tfx {k[6]} tcc {k[7]}  ids {','.join(ids[:6])}")

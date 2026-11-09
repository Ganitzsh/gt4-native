#!/usr/bin/env python3
"""n3_rule.py TAG...: N3 step 0's rule on the 'N3 candidate' lines of build/sessions/n3_TAG/host.log, per course: the
materials in and the closest ones out on each condition. Rule: added light (luminance of texture mean x vertex
colour / 128) > 60, centre/rim < 3, opaque core < 5 %, vertex alpha >= 100."""
import re
import sys

ROOT = "C:/Users/julie/gt4-lane-F/build/sessions/"
pat = re.compile(r"N3 candidate: texture (\w+) (\d+)x(\d+) mean (\d+),(\d+),(\d+) centre/rim (\S+) core (\S+) \| renderer (\d+) "
                 r"vertices (\d+) colour mean (\S+),(\S+),(\S+) a (\S+) sd (\S+),(\S+),(\S+) a (\S+)")
for tag in sys.argv[1:]:
    text = open(ROOT + f"n3_{tag}/host.log", encoding="utf-8", errors="replace").read().split("\n")
    host = {}
    for l in text:
        m = re.match(r"N3 host texture (\d+) for (\w+)", l)
        if m:
            host[m.group(2)] = int(m.group(1))
    rows = []
    for l in text:
        m = pat.match(l)
        if not m:
            continue
        g = m.groups()
        tm = [int(g[3]), int(g[4]), int(g[5])]
        vc = [float(g[10]), float(g[11]), float(g[12])]
        prod = [tm[k] * vc[k] / 128 for k in range(3)]
        rows.append(dict(id=host.get(g[0], 0), size=f"{g[1]}x{g[2]}", mean=tuple(tm), vc=tuple(vc), lum=0.2126 * prod[0] + 0.7152 * prod[1] + 0.0722 * prod[2],
                         cr=float(g[6]), core=float(g[7]), va=float(g[13]), sd=float(g[14]), renderer=int(g[8])))
    rule = lambda r: r["lum"] > 60 and r["cr"] < 3 and r["core"] < 0.05 and r["va"] >= 100
    inn = [r for r in rows if rule(r)]
    out = [r for r in rows if not rule(r)]
    print(f"== {tag}: {len(rows)} candidates, {len(inn)} in")
    seen = set()
    for r in sorted(inn, key=lambda r: r["id"]):
        k = (r["size"], r["mean"], r["vc"], round(r["cr"], 2))
        if k in seen:
            continue
        seen.add(k)
        print(f"   in  id {r['id']:5d} {r['size']:>7} mean {r['mean']} vertex {tuple(round(x) for x in r['vc'])} "
              f"added {r['lum']:.1f} c/r {r['cr']:.2f} core {r['core']:.3f} va {r['va']:.0f} renderer {r['renderer']}")
    def closest(cond, key, best):
        c = [r[key] for r in out if cond(r)]
        return (best(c), len(c)) if c else (None, 0)
    a = closest(lambda r: r["cr"] < 3 and r["core"] < 0.05 and r["va"] >= 100, "lum", max)
    b = closest(lambda r: r["lum"] > 60 and r["core"] < 0.05 and r["va"] >= 100, "cr", min)
    c = closest(lambda r: r["lum"] > 60 and r["cr"] < 3 and r["va"] >= 100, "core", min)
    d = closest(lambda r: r["lum"] > 60 and r["cr"] < 3 and r["core"] < 0.05, "va", max)
    print(f"   out, flat coreless opaque-vertex: max added light {a[0]} over {a[1]}")
    print(f"   out, bright coreless opaque-vertex: min centre/rim {b[0]} over {b[1]}")
    print(f"   out, bright flat opaque-vertex: min core {c[0]} over {c[1]}")
    print(f"   out, bright flat coreless: max vertex alpha {d[0]} over {d[1]}")
    print(f"   in: added light {min(r['lum'] for r in inn):.1f}..{max(r['lum'] for r in inn):.1f}, centre/rim "
          f"{min(r['cr'] for r in inn):.2f}..{max(r['cr'] for r in inn):.2f}" if inn else "   in: none")

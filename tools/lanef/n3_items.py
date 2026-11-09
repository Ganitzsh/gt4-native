#!/usr/bin/env python3
"""n3_items.py TAG [IDS]: N3 step 0, per (texture, item) for the layer-state items of build/sessions/n3_TAG/host.log:
per texture, the items lit (any vertex above the item's darkest by more than 4) and unlit, the lit fraction of the lit
items' vertices, whether their base colour equals the unlit items' commonest base, their peak colours, vertices over
0x80 (MODULATE brightens), and screen area. IDS: comma-separated host texture ids to show (default: the 12 with the
most lit items)."""
import collections
import re
import sys

tag = sys.argv[1]
want = [int(x) for x in sys.argv[2].split(",")] if len(sys.argv) > 2 else None
pat = re.compile(r"N3 item: host texture (\d+) vertices (\d+) above-base (\d+) over-0x80 (\d+) base (\w+) peak (\w+) "
                 r"lum (\S+)\.\.(\S+) mean (\S+) area (\S+)")
items = collections.defaultdict(list)
for l in open(f"C:/Users/julie/gt4-lane-F/build/sessions/n3_{tag}/host.log", encoding="utf-8", errors="replace"):
    m = pat.match(l)
    if m:
        g = m.groups()
        items[int(g[0])].append(dict(n=int(g[1]), above=int(g[2]), over=int(g[3]), base=g[4], peak=g[5], lo=float(g[6]),
                                     hi=float(g[7]), area=float(g[9])))
rank = sorted(items, key=lambda k: -sum(1 for r in items[k] if r["above"]))
for tid in (want or rank[:12]):
    rs = items.get(tid, [])
    lit = [r for r in rs if r["above"]]
    unlit = [r for r in rs if not r["above"]]
    common = collections.Counter(r["base"] for r in unlit).most_common(1)
    cbase = common[0][0] if common else None
    frac = [r["above"] / r["n"] for r in lit if r["n"]]
    same = sum(1 for r in lit if r["base"] == cbase)
    peaks = collections.Counter(r["peak"] for r in lit).most_common(3)
    over = sum(r["over"] for r in lit)
    print(f"texture {tid:5d}: items {len(rs)}, lit {len(lit)}, unlit {len(unlit)}; unlit base {cbase}; lit items with that "
          f"base {same}/{len(lit)}; lit vertex fraction "
          f"{(min(frac) if frac else 0):.2f}..{(max(frac) if frac else 0):.2f} (mean {(sum(frac) / len(frac) if frac else 0):.2f}); "
          f"peaks {peaks}; lit vertices over 0x80 {over}; lit lum {min((r['hi'] for r in lit), default=0):.0f}.."
          f"{max((r['hi'] for r in lit), default=0):.0f}; area lit {sum(r['area'] for r in lit):.0f} unlit {sum(r['area'] for r in unlit):.0f}")

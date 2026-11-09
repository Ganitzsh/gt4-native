#!/usr/bin/env python3
"""f2b_draws.py DRAWS_FILE [VBLANK]: summarise a GT4_R10_DRAWLIST dump for F2b step 0. Per draw group (blend,
texture, target, z test/write, modeled): draws, vertices, first and last draw number; the additive and blended groups
first (A B C D: 0 2 x 1 adds the source, the glow candidates)."""
import collections
import re
import sys

path = sys.argv[1]
want = sys.argv[2] if len(sys.argv) > 2 else None
pat = re.compile(r"^(\d+) draw (\d+) modeled (\d) prim (\w+) blend (\d)(\d)(\d)(\d) fix (\d+) atst (\S+) ztst (\S+) zw (\d) "
                 r"tex (\d+):(\d+):(\d+) target (\d+) count (\d+) rgba (\S+) max (\S+)")
groups = collections.OrderedDict()
for line in open(path, encoding="utf-8", errors="replace"):
    m = pat.match(line)
    if not m:
        continue
    vb, n, modeled, prim, a, b, c, d, fix, atst, ztst, zw, tk, tid, tv, target, count, rgba, mx = m.groups()
    if want and vb != want:
        continue
    blended = (int(prim, 16) >> 6) & 1
    key = (vb, f"{a}{b}{c}{d}" if blended else "----", fix if blended and c == "2" else "", tk, tid, target, ztst, zw,
           modeled, prim)
    g = groups.setdefault(key, [0, 0, int(n), int(n), rgba, mx])
    g[0] += 1
    g[1] += int(count)
    g[3] = int(n)


def rank(item):
    k = item[0]
    additive = k[1][1] == "2" and k[1][3] == "1"
    return (k[0], 0 if additive else 1 if k[1] != "----" else 2, item[1][2])


print("vb      blend fix tex(kind:id) target ztst zw mod prim | draws verts first-last | rgba mean / max")
for k, g in sorted(groups.items(), key=rank):
    print(f"{k[0]} {k[1]:>5} {k[2]:>3} {k[3]}:{k[4]:>6} t{k[5]:>4} z{k[6]:>4} {k[7]} m{k[8]} {k[9]} | "
          f"{g[0]:4} {g[1]:6} {g[2]:4}-{g[3]:<4} | {g[4]} / {g[5]}")

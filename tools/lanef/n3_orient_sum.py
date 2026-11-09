#!/usr/bin/env python3
"""n3_orient_sum.py: the share table from build/sessions/n3o_*/orient.tsv. v2 matches only, per look (Route 5 Reverse:
the label's id group; Clubman: per texture), area-weighted over the look's textures: share of area with |n . up| >= 0.9,
mean |n . up|, steepest item. Labels ending in '*' come from n3_unseen.py (frames where drawn); frames below 300 show
garbage textures (the replay before the game re-uploads), marked 'early'."""
import math
from pathlib import Path

S = Path("C:/Users/julie/gt4-lane-F/build/sessions")
deg = lambda c: math.degrees(math.acos(max(-1.0, min(1.0, c))))
early = {}
for tag in ("csr5", "r5rev"):
    u = S / f"n3_{tag}" / "unseen.partial.tsv"
    if u.exists():
        for line in u.read_text(encoding="utf-8").splitlines():
            c = line.split("\t")
            frames = [int(x) for x in c[7].split(",") if x.isdigit()]
            early[(tag, c[0])] = bool(frames) and max(frames) < 300
for tag in ("csr5", "r5rev", "seoul", "csr5new"):
    f = S / f"n3o_{tag}" / "orient.tsv"
    if not f.exists():
        continue
    looks = {}
    for line in f.read_text(encoding="utf-8").splitlines():
        c = line.split("\t")
        if c[3] != "1":
            continue
        L = looks.setdefault(c[1], dict(lab=c[2], area=0.0, share=0.0, mean=0.0, worst=1.0, items=0, desc=f"{c[11]} {c[12]} vc {c[13]}"))
        a = float(c[9])
        L["area"] += a
        L["share"] += a * float(c[4])
        L["mean"] += a * float(c[6])
        L["worst"] = min(L["worst"], float(c[7]))
        L["items"] += int(c[8])
    print(f"==== {tag}: {len(looks)} v2-matching looks with items")
    for lab in sorted({L["lab"] for L in looks.values()}):
        rs = sorted(((k, L) for k, L in looks.items() if L["lab"] == lab), key=lambda kv: kv[1]["share"] / max(kv[1]["area"], 1e-9))
        print(f"  {lab}: {len(rs)}")
        for k, L in rs:
            a = max(L["area"], 1e-9)
            e = " early" if early.get((tag, k)) else ""
            print(f"    {k[:24]:24} share {L['share'] / a:.3f} mean {L['mean'] / a:.3f} ({deg(L['mean'] / a):4.1f} deg) steepest item "
                  f"{deg(L['worst']):4.1f} deg, {L['items']} items, area {L['area']:.0f}{e}  {L['desc']}")

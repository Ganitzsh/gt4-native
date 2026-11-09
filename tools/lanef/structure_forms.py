#!/usr/bin/env python3
"""structure_forms.py: the architect's four forms of section 2's structure measure (band_measure.steps() on
difference(device, reference)): rows 45-72 % or 55-72 %, statistic median or mean. Per pair set, per form: the ratio
(boundary over elsewhere) and the boundary statistic, min / median / max over the set's pairs. Sets:
  race7 Odin/desktop        the Odin's race7 30, 60, 90 against lane C's desktop references (cross-GPU, no bands)
  pe2prog original Odin/desktop   race_pe2_prog 3600-4600 every 100, the same
  pe2prog ultra Odin/desktop      (when lane B's desktop ultra frames are given: ULTRA_REF below)
  strips L on desktop       a desktop reference frame with add_strips(L) against itself clean (L = 1, 2, 3, 5)
  strips L on Odin          the Odin's frame with add_strips(L) against the desktop reference: bands on top of the
                            cross-GPU noise, the realistic case
The margin per form: the smallest strip-set value against the largest clean-pair value, for the ratio and the
boundary statistic. Frames read in place; nothing written but the table."""
import os
import sys
from pathlib import Path

sys.path.insert(0, "C:/Users/julie/gt4-lane-F/tools")
import band_measure as b  # noqa: E402

ODIN = Path("C:/Users/julie/odin_clip_20261109")
REF = Path("C:/Users/julie/gt4-lane-C/build/caps/device_data/ref")
ULTRA_REF = os.environ.get("ULTRA_REF")   # lane B's desktop ultra frames of race_pe2_prog, when given


def ref_of(d, stem):
    for e in (".png", ".ppm"):
        if (d / (stem + e)).exists():
            return d / (stem + e)
    return None


pairs = {"race7 Odin/desktop": [], "pe2prog original Odin/desktop": [], "pe2prog ultra Odin/desktop": []}
for p in sorted((ODIN / "race7_odin").glob("*.ppm")):
    r = ref_of(REF / "race7", p.stem)
    if r:
        pairs["race7 Odin/desktop"].append((p, r))
for p in sorted((ODIN / "prog_odin" / "original").glob("*.ppm")):
    r = ref_of(REF / "race_pe2_prog", p.stem)
    if r:
        pairs["pe2prog original Odin/desktop"].append((p, r))
if ULTRA_REF:
    for p in sorted((ODIN / "prog_odin" / "ultra").glob("*.ppm")):
        r = ref_of(Path(ULTRA_REF), p.stem)
        if r:
            pairs["pe2prog ultra Odin/desktop"].append((p, r))
FORMS = [((0.45, 0.72), "median"), ((0.55, 0.72), "median"), ((0.45, 0.72), "mean"), ((0.55, 0.72), "mean")]
cache = {}


def frame(p):
    if p not in cache:
        cache[p] = b.read_frame(str(p))
    return cache[p]


diffs = {}   # set name -> list of (w, h, difference bytes)
for name, ps in pairs.items():
    for dev, ref in ps:
        w, h, a = frame(dev)
        rw, rh, c = frame(ref)
        if (w, h) == (rw, rh):
            diffs.setdefault(name, []).append((w, h, b.difference(a, c)))
clean_refs = [ref for ps in pairs.values() for _, ref in ps]
for L in (1, 2, 3, 5):
    for ref in clean_refs:
        w, h, c = frame(ref)
        diffs.setdefault(f"strips {L} on desktop", []).append((w, h, b.difference(b.add_strips(w, c, L), c)))
    for name, ps in pairs.items():
        for dev, ref in ps:
            w, h, a = frame(dev)
            rw, rh, c = frame(ref)
            if (w, h) == (rw, rh):
                diffs.setdefault(f"strips {L} on Odin", []).append((w, h, b.difference(b.add_strips(w, a, L), c)))

table = {}
for name, ds in diffs.items():
    for rows, stat in FORMS:
        rs = [b.steps(w, h, d, rows=rows, stat=stat) for w, h, d in ds]
        table[(name, rows, stat)] = ([r["ratio"] if r["ratio"] is not None else float("inf") for r in rs],
                                     [r["boundary_median"] for r in rs])
fmt = lambda v: "inf" if v == float("inf") else f"{v:.2f}"
print("set (pairs): per form 'ratio min/med/max | boundary stat min/med/max'")
for name, ds in diffs.items():
    print(f"{name} ({len(ds)})")
    for rows, stat in FORMS:
        ra, bs = table[(name, rows, stat)]
        sr, sb = sorted(ra), sorted(bs)
        print(f"    rows {rows[0]:.2f}-{rows[1]:.2f} {stat:6}: ratio {fmt(sr[0])} / {fmt(sr[len(sr) // 2])} / "
              f"{fmt(sr[-1])} | boundary {sb[0]:.2f} / {sb[len(sb) // 2]:.2f} / {sb[-1]:.2f}")
print("margins (strip sets' smallest against clean pairs' largest; boundary statistic, and ratio):")
clean = [n for n in diffs if "Odin/desktop" in n]
for L in (1, 2, 3, 5):
    for where in ("desktop", "Odin"):
        sname = f"strips {L} on {where}"
        if sname not in diffs:
            continue
        parts = []
        for rows, stat in FORMS:
            cb = max(max(table[(n, rows, stat)][1]) for n in clean) if clean else 0.0
            cr = max(max(table[(n, rows, stat)][0]) for n in clean) if clean else 0.0
            sb, sr = min(table[(sname, rows, stat)][1]), min(table[(sname, rows, stat)][0])
            parts.append(f"{rows[0]:.2f}/{stat}: boundary {sb:.2f} vs {cb:.2f}, ratio {fmt(sr)} vs {fmt(cr)}")
        print(f"  {sname}: " + "; ".join(parts))

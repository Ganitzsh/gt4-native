#!/usr/bin/env python3
"""n3_texel.py TAG...: among items at least 0.9 m wide (n3_footprint.py's rows), the texture's world size per texel,
max(u, v), per look: min / median / max, with the label from the images. The candidate second property: pools are
coarse light textures stretched over metres; painted markings (the start/finish checker) carry fine texels."""
import statistics
import sys

sys.argv = [sys.argv[0]] + sys.argv[1:]
import n3_footprint as fp  # noqa: E402  (prints its own table on import; the rows are what is used here)

CHECKER = {"128x128 (70, 77, 97)", "128x128 (107, 126, 135)", "128x128 (96, 113, 124)"}
print("\n== items >= 0.9 m wide: max(u, v) per texel (m)")
groups = {}
for r in fp.rows:
    if r["width"] < 0.9:
        continue
    label = "marking (start/finish checker)" if r["key"] in CHECKER else r["label"]
    groups.setdefault((label, r["key"]), []).append(max(r["u"], r["v"]))
for (label, key), xs in sorted(groups.items(), key=lambda kv: min(kv[1])):
    print(f"  {label[:34]:34} {key:24} {len(xs):5} items  {min(xs):.3f} {statistics.median(xs):.3f} {max(xs):.3f}")

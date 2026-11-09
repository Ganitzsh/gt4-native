#!/usr/bin/env python3
"""ORIG on another device class (PLAN section 2): a device's frames against the x64 reference's, on pixel values.

    py tools/orig_compare.py A B [--baseline SHARE] [--baseline-over SHARE] [--content X0-X1] [--json]
    py tools/orig_compare.py DIR_A DIR_B [...]     (the frames of DIR_A with a frame of the same name in DIR_B)
    py tools/orig_compare.py --self-test

A is the device's frame, B the reference's (PPM P6 or PNG, 8-bit; as gt4_host --capture-frames and device_run.py
write them). Per pair, section 2's interim rule (until GS-exact texel sampling, B24; then at most 2 levels on every
pixel):
- the max-channel difference histogram (shares of the pixels at 0, 1-2, 3-8, 9-32 and over 32 levels), the worst
  pixel (its difference and position), and the pixels over 2 levels (their share, how many touch another, the most
  in one 8x8 block): a pixel over 32 levels fails;
- the capture's baselines for the device class (its every-frame run sets them; --baseline the 1-2 share,
  --baseline-over the over-2 share of its highest frame, in percent): the 1-2 share more than 5 points above its
  baseline fails, the over-2 share above twice its baseline fails; without baselines they are reported, not judged;
- the structure test (the night bands' signature): section 2's measure, band_measure.steps() (lane F's) on
  band_measure.difference(A, B), rows 45 % to 72 % of the height, the picture's columns (the whole width unless
  --content gives them, for a picture between side bars): the median step at the 32-GS-pixel boundaries against the
  median elsewhere. It fails when the boundary median is at least 0.25 levels (FLOOR) and over 5 times the one
  elsewhere (RATIO_MAX; an elsewhere of 0, which steps() reports as ratio None, is unbounded: over 5 times). Clean
  day pairs (the Odin against the desktop): at most 0.05 levels, a ratio of 2.3; 1-level strips: 0.96, 48.
numpy is used when present; without it the same numbers come from plain Python (the Odin may have no numpy).
Frames are game frames: they stay on the owner's machines and are never committed.

Output: one line per pair, or with --json one JSON object per line. Exit code 0 when every pair passes, 1 when one
fails, 2 on a usage error.
"""
import json
import os
import sys
from collections import Counter
from operator import sub

try:
    import numpy as np
except ImportError:   # the Odin may not have numpy
    np = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import band_measure  # noqa: E402  (section 2's structure measure, and PPM and PNG in plain Python)
from band_measure import read_frame  # noqa: E402

ROWS = (0.45, 0.72)               # section 2's rows for the structure measure
MAX_LEVELS = 2                    # section 2: the rounding bound (the strict rule, after GS-exact sampling)
WORST_MAX = 32                    # interim: no pixel over this many levels
OVER_FACTOR = 2.0                 # interim: a frame's over-2 share fails over this times its capture's baseline
SHARE_MARGIN = 5.0                # points over the capture's baseline 1-2 share
RATIO_MAX = 5.0                   # the structure test: the boundary median over this times the median elsewhere
FLOOR = 0.25                      # levels: the boundary median under it passes (noise)
BUCKETS = ("0", "1-2", "3-8", "9-32", ">32")


def structure(w, h, a, b, content=None):
    """Section 2's structure measure on one pair: band_measure.steps() on the difference (band_measure keeps its
    numpy use in step with this file's: the module's np is set to this one's, so the self-test's plain-Python run
    covers it too)."""
    saved = band_measure.np
    band_measure.np = np
    try:
        s = band_measure.steps(w, h, band_measure.difference(a, b), rows=ROWS, content=content)
    finally:
        band_measure.np = saved
    return s


def compare(w, h, a, b, content=None):
    """The numbers for one pair (RGB bytes of the same size)."""
    if len(a) != w * h * 3 or len(b) != w * h * 3:
        raise ValueError("frames of different sizes")
    if a == b:
        hist = [w * h, 0, 0, 0, 0]
        worst, at = 0, None
    elif np is not None:
        fa = np.frombuffer(a, np.uint8).reshape(h, w, 3).astype(np.int16)
        fb = np.frombuffer(b, np.uint8).reshape(h, w, 3).astype(np.int16)
        d = fa - fb
        m = np.abs(d).max(axis=2)
        hist = [int((m == 0).sum()), int(((m >= 1) & (m <= 2)).sum()), int(((m >= 3) & (m <= 8)).sum()),
                int(((m >= 9) & (m <= 32)).sum()), int((m > 32).sum())]
        worst = int(m.max())
        k = int(m.argmax())
        at = [k % w, k // w]
    else:
        dr = list(map(sub, a[0::3], b[0::3]))
        dg = list(map(sub, a[1::3], b[1::3]))
        db = list(map(sub, a[2::3], b[2::3]))
        m = list(map(max, map(abs, dr), map(abs, dg), map(abs, db)))
        hist = [0] * 5
        for value, n in Counter(m).items():   # (one pass, in C)
            hist[0 if value == 0 else 1 if value <= 2 else 2 if value <= 8 else 3 if value <= 32 else 4] += n
        worst = max(m)
        k = m.index(worst)
        at = [k % w, k // w]
    # The pixels over 2 levels (HUD glyphs and textured 3D, lane B's Odin pairs: 0.02-0.49 % at original): how many,
    # whether they touch each other (4-neighbours) and how many share the busiest 8x8 block.
    if worst <= MAX_LEVELS:
        over = []
    elif np is not None:
        over = [int(i) for i in np.flatnonzero(m.reshape(-1) > MAX_LEVELS)]
    else:
        over = [i for i, v in enumerate(m) if v > MAX_LEVELS]
    over_set = set(over)
    touching = sum(1 for i in over if (i % w and i - 1 in over_set) or (i % w != w - 1 and i + 1 in over_set) or
                   i - w in over_set or i + w in over_set)
    blocks = {}
    for i in over:
        key = (i % w // 8, i // w // 8)
        blocks[key] = blocks.get(key, 0) + 1
    n = float(w * h)
    shares = {name: round(100.0 * c / n, 3) for name, c in zip(BUCKETS, hist)}
    if a == b:
        s = {"boundary_median": 0.0, "elsewhere_median": 0.0, "ratio": None, "boundary_max": 0.0}
    else:
        s = structure(w, h, a, b, content)
    return {"width": w, "height": h, "histogram": shares, "worst": worst, "worst_at": at,
            "over_2": len(over), "over_2_share": round(100.0 * len(over) / n, 4), "over_32": hist[4],
            "over_2_touching": touching, "over_2_busiest_8x8": max(blocks.values()) if blocks else 0,
            "steps_at_boundaries": s["boundary_median"], "steps_elsewhere": s["elsewhere_median"],
            "ratio": s["ratio"], "boundary_max": s["boundary_max"]}


def judge(result, baseline=None):
    """(passed, reasons, shares_judged) for one pair's numbers by section 2's rule. `baseline`: the capture's
    baselines for the device class, {"1-2": percent, "over_2": percent (its every-frame run's highest frame)}; a
    share without one is reported, not judged."""
    baseline = baseline or {}
    reasons = []
    if result["worst"] > WORST_MAX:
        reasons.append(f"a pixel {result['worst']} levels off at {result['worst_at']}")
    over = baseline.get("over_2")
    if over is not None and result["over_2_share"] > OVER_FACTOR * over:
        reasons.append(f"{result['over_2']} pixels ({result['over_2_share']} %) over {MAX_LEVELS} levels, more than "
                       f"{OVER_FACTOR} x the baseline {over} %")
    share, base = result["histogram"]["1-2"], baseline.get("1-2")
    if base is not None and share > base + SHARE_MARGIN:
        reasons.append(f"1-2 share {share} % over the baseline {base} % + {SHARE_MARGIN}")
    ratio = result["ratio"]   # (None: nothing elsewhere, an unbounded ratio)
    if result["steps_at_boundaries"] >= FLOOR and (ratio is None or ratio > RATIO_MAX):
        reasons.append(f"32-GS-pixel steps {result['steps_at_boundaries']} against {result['steps_elsewhere']} "
                       f"(ratio {'unbounded' if ratio is None else ratio})")
    return not reasons, reasons, "1-2" in baseline and "over_2" in baseline


def compare_files(pa, pb, content=None, baseline=None):
    wa, ha, a = read_frame(pa)
    wb, hb, b = read_frame(pb)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"{pa} is {wa}x{ha}, {pb} is {wb}x{hb}")
    r = compare(wa, ha, a, b, content)
    ok, reasons, judged = judge(r, baseline)
    r.update({"pass": ok, "reasons": reasons, "share_judged": judged})
    return r


def self_test():
    import random
    rnd = random.Random(4)
    w, h = 320, 240   # 2 output pixels per GS pixel over 640 / 4: still 19 boundaries, 8 pixels a strip
    base = bytearray()
    for y in range(h):
        for x in range(w):
            v = 40 + (x * 150) // w + (y * 30) // h
            base += bytes((v, (v + 20) % 256, (v * 3) % 256))
    base = bytes(base)

    def varied(fn):
        out = bytearray(base)
        for i in range(0, len(out), 3):
            x = (i // 3) % w
            for c in range(3):
                out[i + c] = max(0, min(255, out[i + c] + fn(x, c)))
        return bytes(out)
    noise = varied(lambda x, c: rnd.choice((0, 0, 0, 1, -1, 2, -2)) if rnd.random() < 0.3 else 0)
    faint = band_measure.add_strips(w, noise, 1)    # every other 32-GS-pixel strip 1 level up: the faintest bands
    strips = band_measure.add_strips(w, base, 2)    # 2 levels: within the per-pixel bound, still structure
    bands = varied(lambda x, c: (x * 20 // w) % 4 + 2)   # each strip 2-5 levels off, as the owner's
    cases = (("identical", base, True), ("noise", noise, True), ("faint strips on noise", faint, False),
             ("strips", strips, False), ("bands", bands, False))
    global np
    have = np
    ok = True
    for name, frame, want in cases:
        results = []
        for use in ((have, None) if have is not None else (None,)):
            np = use
            r = compare(w, h, frame, base)
            results.append(r)
        np = have
        passed, reasons, _ = judge(results[0])
        same = all(abs(x["steps_at_boundaries"] - results[0]["steps_at_boundaries"]) < 1e-3 and
                   abs(x["steps_elsewhere"] - results[0]["steps_elsewhere"]) < 1e-3 and
                   x["histogram"] == results[0]["histogram"] and x["worst"] == results[0]["worst"] for x in results)
        good = passed == want and same
        ok = ok and good
        print(f"self-test {name}: {'pass' if passed else 'fail'} (expected {'pass' if want else 'fail'}), numpy and "
              f"plain Python {'agree' if same else 'DISAGREE'}: {results[0]['histogram']}, worst "
              f"{results[0]['worst']}, steps {results[0]['steps_at_boundaries']} / {results[0]['steps_elsewhere']}"
              f"{'; ' + '; '.join(reasons) if reasons else ''}")
    # the shares against a capture's baselines: the 1-2 share 6 points over fails, at its own share passes; scattered
    # over-2 pixels (a HUD glyph's edge, 0.05 %) pass with no baseline and at their own, fail over twice a smaller one
    r = compare(w, h, noise, base)
    over = judge(r, baseline={"1-2": r["histogram"]["1-2"] - SHARE_MARGIN - 1.0})[0]
    under = judge(r, baseline={"1-2": r["histogram"]["1-2"]})[0]
    glyph = bytearray(noise)
    for i in range(0, len(glyph), 3 * 2003):
        glyph[i] = (glyph[i] + 19) % 256   # 19 levels, as the Odin's HUD pixels
    g = compare(w, h, bytes(glyph), base)
    alone = judge(g)[0]
    own = judge(g, baseline={"over_2": g["over_2_share"]})[0]
    tight = judge(g, baseline={"over_2": g["over_2_share"] / (OVER_FACTOR * 1.5)})[0]
    print(f"self-test baselines: 1-2 share {'fails' if not over else 'passes'} 6 points over, "
          f"{'passes' if under else 'fails'} at its own; {g['over_2']} pixels over 2 ({g['over_2_share']} %, worst "
          f"{g['worst']}) {'pass' if alone else 'fail'} with no baseline, {'pass' if own else 'fail'} at their own, "
          f"{'fail' if not tight else 'pass'} over {OVER_FACTOR} x a smaller one")
    ok = ok and not over and under and alone and own and not tight
    print("self-test: OK" if ok else "self-test: FAILED")
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    args, as_json, baseline, content = [], False, {}, None
    it = iter(argv)
    for arg in it:
        if arg == "--json":
            as_json = True
        elif arg == "--baseline":
            baseline["1-2"] = float(next(it))
        elif arg == "--baseline-over":
            baseline["over_2"] = float(next(it))
        elif arg == "--content":
            x0, x1 = next(it).split("-")
            content = (int(x0), int(x1))
        else:
            args.append(arg)
    if len(args) != 2:
        print(__doc__)
        return 2
    a, b = args
    if os.path.isdir(a):
        names = sorted(f for f in os.listdir(a) if f.lower().endswith((".ppm", ".png")))
        pairs = []
        for f in names:
            stem = os.path.splitext(f)[0]
            match = [g for g in (stem + ".png", stem + ".ppm") if os.path.isfile(os.path.join(b, g))]
            if match:
                pairs.append((os.path.join(a, f), os.path.join(b, match[0])))
    else:
        pairs = [(a, b)]
    failed = 0
    for pa, pb in pairs:
        r = compare_files(pa, pb, content, baseline)
        failed += not r["pass"]
        if as_json:
            print(json.dumps(dict(r, frame=os.path.basename(pa))))
        else:
            hs = r["histogram"]
            print(f"{os.path.basename(pa)}: {'pass' if r['pass'] else 'FAIL'}; 0 {hs['0']} %, 1-2 {hs['1-2']} %, "
                  f"3-8 {hs['3-8']} %, 9-32 {hs['9-32']} %, >32 {hs['>32']} %; worst {r['worst']} at "
                  f"{r['worst_at']}; steps {r['steps_at_boundaries']} / {r['steps_elsewhere']}"
                  f"{'; ' + '; '.join(r['reasons']) if r['reasons'] else ''}")
    if not pairs:
        print("orig_compare: no pairs")
        return 2
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Vertical bands in a frame: a triage score for the owner's night bands (C6b's device item, 2026-11-09).

    py tools/band_measure.py FRAME_OR_DIR... [--json] [--rows 0.45-0.72] [--threshold 6] [--window 33]
    py tools/band_measure.py --steps FRAME_OR_DIR... --against REF_FRAME_OR_DIR [--json] [--grid 64] [--rows 0.45-0.72]
    py tools/band_measure.py --self-test

--steps --against: the night bands on the device, in detail. steps() below on difference() is PLAN section 2's
structure measure; tools/orig_compare.py calls it and gives the verdict (section 2's thresholds), and this mode
shows the detail: which boundaries, how big, how a step sits across frames. The Odin's frames of a replay against
the desktop's frames of the same replay, by file name: steps of the difference's column profile at every grid
boundary (--grid 64 output pixels at 1280 wide, 32 GS pixels at 2x, scaled with the width) against everywhere else.
The owner's bands are 2 to 5 level steps on that grid. The difference removes the scene, so only what the two GPUs
drew differently is left. A directory given to --against is paired by the frame's stem (the device's .ppm with a
reference's .png). --steps without --against measures a single frame, and that does NOT separate the bands from a
clean frame: GT4's own frames have steps on the same grid (desktop live frames, ratio of boundary to elsewhere
median 2.6; the owner's 132 clip frames 2.1, their video compression blurring the steps): for looking only.
--stat mean takes means where steps() takes medians by default; --content X0-X1 gives the picture's columns when it
sits between side bars.

Per frame, over the road (rows 45 % to 72 % of the height by default: below the horizon, above the gauges of
the bumper and chase views): the mean luma of each column (Rec.601 weights on the 8-bit values), less a running
median of that profile across x (33 columns at 1280 wide, scaled with the width and kept odd). A smooth frame
leaves a residual near zero; vertical bands leave runs of columns whose residual is far from it. Reported:
the share of columns whose |residual| is over the threshold (6 levels), and those columns' runs as x ranges
(pixels and fractions of the width), largest first. The median sees bands up to about half its window wide
(16 columns at 1280); wider ones need a larger --window, at the cost of more scene structure in the residual.

The threshold is a guess: no band had been seen when this was written (the desktop shows none at the owner's
settings). Real edges (a lamp post, a car's side) also make runs, so a score is never a verdict: a frame that
scores above the desktop's own frames of the same replay means "look at this frame". Once a real band is seen,
the threshold gets calibrated on it.

Frames: PPM (P6, 8-bit, as gt4_host's --capture-frames writes them) and PNG (8-bit RGB or RGBA, not interlaced,
as the dump key writes them) in plain Python; other kinds only with Pillow. numpy is used when present; without
it the same numbers come from plain Python (slower: about a second for a 1280 x 720 PNG). A directory gives every
*.ppm / *.png in it, sorted by name. Frames are game frames: they stay in build/ (or on the device) and are never
committed.

Output: one line per frame, or with --json one JSON object per line:
    {"frame": NAME, "width": W, "height": H, "score": SHARE, "columns": N, "runs": [[X0, X1, PEAK], ...]}
X1 is inclusive; PEAK is the run's largest |residual| in levels, signed.
"""
import json
import os
import sys

try:
    import numpy as np
except ImportError:   # the Odin may not have numpy
    np = None

WEIGHTS = (0.299, 0.587, 0.114)


def read_ppm(path):
    """(width, height, bytes) of a binary PPM (P6, maxval 255)."""
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def read_png(path):
    """(width, height, RGB bytes) of an 8-bit RGB or RGBA PNG, not interlaced (stb_image_write's, the dump key's),
    in plain Python: the IDAT chunks inflated, each row's filter (None, Sub, Up, Average, Paeth) undone, alpha
    dropped. Anything else (a palette, 16 bits, interlacing) raises ValueError."""
    import struct
    import zlib
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w = 8, [], None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: PNG depth {depth}, colour type {ctype}, interlace {interlace}: "
                                 "only 8-bit RGB or RGBA without interlacing")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if w is None:
        raise ValueError(f"{path}: no IHDR")
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    prev = bytearray(stride)
    out = bytearray()
    for y in range(h):
        base = y * (stride + 1)
        ftype, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ftype == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif ftype == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                upleft = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(left, prev[i], upleft)) & 255
        elif ftype != 0:
            raise ValueError(f"{path}: row {y}: filter {ftype}")
        prev = line
        if bpp == 3:
            out += line
        else:
            rgb = line[:]   # RGBA -> RGB on a copy: prev keeps the alpha for the next row's filter
            del rgb[3::4]
            out += rgb
    return w, h, bytes(out)


def read_frame(path):
    if path.lower().endswith(".ppm"):
        return read_ppm(path)
    if path.lower().endswith(".png"):
        try:
            return read_png(path)
        except ValueError:
            pass   # a PNG kind the plain reader doesn't take: Pillow, when present
    from PIL import Image   # other formats: Pillow only
    im = Image.open(path).convert("RGB")
    return im.width, im.height, im.tobytes()


def column_profile(w, h, rgb, rows):
    """Mean luma per column over rows [r0, r1) (fractions of the height)."""
    r0, r1 = int(h * rows[0]), max(int(h * rows[0]) + 1, int(h * rows[1]))
    if np is not None:
        a = np.frombuffer(rgb, np.uint8).reshape(h, w, 3)[r0:r1].astype(np.float64)
        return [float(v) for v in (a @ np.array(WEIGHTS)).mean(axis=0)]
    sums = [0.0] * w
    for y in range(r0, r1):
        line = rgb[y * w * 3:(y + 1) * w * 3]
        rs, gs, bs = line[0::3], line[1::3], line[2::3]
        for x in range(w):
            sums[x] += WEIGHTS[0] * rs[x] + WEIGHTS[1] * gs[x] + WEIGHTS[2] * bs[x]
    n = r1 - r0
    return [s / n for s in sums]


def running_median(values, window):
    """Median of each value's centred window, the ends clamped (the window shrinks to fit)."""
    half, n, out = window // 2, len(values), []
    for i in range(n):
        part = sorted(values[max(0, i - half):min(n, i + half + 1)])
        m = len(part)
        out.append(part[m // 2] if m % 2 else (part[m // 2 - 1] + part[m // 2]) / 2)
    return out


def window_for(width, base):
    """The median window scaled from 1280 wide, odd, at least 5."""
    k = max(5, int(round(base * width / 1280)))
    return k if k % 2 else k + 1


def measure(w, h, rgb, rows=(0.45, 0.72), threshold=6.0, window=33):
    prof = column_profile(w, h, rgb, rows)
    med = running_median(prof, window_for(w, window))
    res = [p - m for p, m in zip(prof, med)]
    over = [abs(r) > threshold for r in res]
    runs, x = [], 0
    while x < w:
        if over[x]:
            x0 = x
            while x < w and over[x]:
                x += 1
            peak = max(res[x0:x], key=abs)
            runs.append([x0, x - 1, round(peak, 1)])
        else:
            x += 1
    runs.sort(key=lambda r: -abs(r[2]))
    n = sum(over)
    return {"width": w, "height": h, "score": round(n / w, 4), "columns": n, "runs": runs}


def median(values):
    v = sorted(values)
    n = len(v)
    return 0.0 if not n else (v[n // 2] if n % 2 else (v[n // 2 - 1] + v[n // 2]) / 2)


def steps(w, h, rgb, rows=(0.45, 0.72), grid=64.0, k=8, slabs=6, stat="median", content=None):
    """The step mode (--steps): brightness steps on a fixed grid of columns, the bands GT4's strip-drawn passes leave
    when a GPU draws each strip a little differently (the Odin's night bands: 32-GS-pixel strips, every 64 output
    pixels at 1280 wide). The column profile's step at x is the mean of the k columns from x less the mean of the k
    before it, less the mean of the same step k columns either side (so a ramp, a lamp pool's slope, gives 0 and a
    hard edge keeps its height), taken in six bands of rows and their median kept (a band runs down every row; a
    car's edge, a post, covers only some). At each grid boundary (grid and k given at 1280 wide, scaled with the
    width) the largest |step| within one column of it; elsewhere, every x farther than 2k from a boundary (an edge's
    step reaches 2k). Reported: the median |step| at the boundaries and elsewhere, their ratio, the largest at a
    boundary, and the boundaries over 2 levels (a size to look at, not a bound). A frame with the grid has boundary
    steps well above the rest; edges in the scene fall anywhere and raise both.
    stat "mean" puts means in boundary_median and elsewhere_median (the keys keep their names; "stat" says which).
    content=(x0, x1): the picture's columns when it sits between side bars (a 4:3 picture at 1280 x 800): the grid
    starts at x0 and scales with x1 - x0 (64 at 1280 wide: a boundary every (x1 - x0) / 20), k too, and only the
    columns inside count; default the whole width.
    ratio is None when the elsewhere statistic is under 1e-6: an UNBOUNDED ratio, not a missing one. A difference
    that steps at the boundaries and nowhere else (add_strips on a clean twin) is exactly the bands' signature, so
    None with boundary_median over a floor means bands.
    This is PLAN section 2's structure measure: tools/orig_compare.py calls it on difference(device, reference)
    and applies section 2's thresholds to boundary_median and ratio; keep the keys stable."""
    x0, x1 = content if content else (0, w)
    x0, x1 = max(0, int(x0)), min(w, int(x1))
    cw = x1 - x0
    g, kk = grid * cw / 1280.0, max(2, int(round(k * cw / 1280.0)))
    slab_steps = []
    for j in range(slabs):   # the rows in `slabs` bands: a band runs down all of them, a car's edge only some
        r0 = rows[0] + (rows[1] - rows[0]) * j / slabs
        prof = column_profile(w, h, rgb, (r0, r0 + (rows[1] - rows[0]) / slabs))
        acc = [0.0]
        for v in prof:
            acc.append(acc[-1] + v)
        raw = {x: (acc[x + kk] - acc[x]) / kk - (acc[x] - acc[x - kk]) / kk for x in range(x0 + kk, x1 - kk + 1)}
        slab_steps.append({x: raw[x] - (raw[x - kk] + raw[x + kk]) / 2
                           for x in range(x0 + 2 * kk, x1 - 2 * kk + 1)})
    step = {x: median([s[x] for s in slab_steps]) for x in slab_steps[0]}
    bounds = [b for b in (x0 + int(round(i * g)) for i in range(1, int(cw / g) + 1))
              if x0 + 2 * kk + 1 <= b <= x1 - 2 * kk - 1]
    at = []
    for b in bounds:
        x = max((b - 1, b, b + 1), key=lambda x: abs(step[x]))
        at.append((b, round(step[x], 2)))
    far = [abs(s) for x, s in step.items() if min((abs(x - b) for b in bounds), default=w) > 2 * kk]
    summary = median if stat == "median" else (lambda v: sum(v) / len(v) if v else 0.0)
    mb, me = summary([abs(s) for _, s in at]), summary(far)
    over = [[b, s] for b, s in at if abs(s) > 2.0]
    return {"width": w, "height": h, "grid": round(g, 2), "boundary_median": round(mb, 2),
            "elsewhere_median": round(me, 2), "ratio": round(mb / me, 2) if me > 1e-6 else None,
            "boundary_max": round(max((abs(s) for _, s in at), default=0.0), 2), "boundaries": len(at),
            "over_2": sorted(over, key=lambda t: -abs(t[1])), "at": at, "stat": stat}


def add_strips(w, rgb, levels, grid=64.0, content=None):
    """A test frame: rgb with every other grid-wide strip of columns (grid at 1280 wide, scaled) raised by `levels`
    (clamped), so every boundary steps by `levels`: the owner's bands' shape, to measure against the clean frame.
    content=(x0, x1) as for steps(): the strips inside the picture's columns only."""
    x0, x1 = content if content else (0, w)
    g = grid * (x1 - x0) / 1280.0
    off = [int(round(levels)) if x0 <= x < x1 and int((x - x0) // g) % 2 else 0 for x in range(w)]
    out = bytearray(rgb)
    for i in range(len(out)):
        out[i] = max(0, min(255, out[i] + off[(i // 3) % w]))
    return bytes(out)


def difference(a, b):
    """RGB bytes 128 + a - b per channel (clamped): the step mode on it sees only where two renderings of the same
    frame differ (the Odin's replay frame against the desktop's), not the scene's own edges."""
    if np is not None:
        d = np.frombuffer(a, np.uint8).astype(np.int16) - np.frombuffer(b, np.uint8).astype(np.int16) + 128
        return d.clip(0, 255).astype(np.uint8).tobytes()
    return bytes(max(0, min(255, 128 + x - y)) for x, y in zip(a, b))


def steps_over_frames(results):
    """Across frames of one size: each boundary's median |step| (a band stays at its x, the scene's edges move), the
    median of those, and the boundaries whose median is over 2 levels."""
    per = {}
    for r in results:
        for b, s in r["at"]:
            per.setdefault(b, []).append(abs(s))
    meds = {b: median(v) for b, v in per.items()}
    return {"frames": len(results), "boundary_median": round(median(list(meds.values())), 2),
            "over_2": sorted([[b, round(m, 2)] for b, m in meds.items() if m > 2.0], key=lambda t: -t[1])}


def frames_in(paths):
    for p in paths:
        if os.path.isdir(p):
            for name in sorted(os.listdir(p)):
                if name.lower().endswith((".ppm", ".png")):
                    yield os.path.join(p, name)
        else:
            yield p


def png_round_trip():
    """Small RGB and RGBA images written as PNG with each row under a different filter (None, Sub, Up, Average,
    Paeth in turn) read back by read_png exactly, and as Pillow reads them when it is present."""
    import random
    import struct
    import tempfile
    import zlib

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    def filtered(ftype, line, prev, bpp):
        out = bytearray()
        for i, v in enumerate(line):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            pred = (0, left, up, (left + up) >> 1, paeth(left, up, upleft))[ftype]
            out.append((v - pred) & 255)
        return bytes(out)

    random.seed(11)
    ok = True
    for bpp, ctype in ((3, 2), (4, 6)):
        w, h = 23, 11
        pix = bytes(random.randrange(256) for _ in range(w * h * bpp))
        stride, raw, prev = w * bpp, bytearray(), bytes(w * bpp)
        for y in range(h):
            line = pix[y * stride:(y + 1) * stride]
            raw += bytes((y % 5,)) + filtered(y % 5, line, prev, bpp)
            prev = line
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(bytes(raw))[:40]) + chunk(b"IDAT", zlib.compress(bytes(raw))[40:])
               + chunk(b"IEND", b""))
        rgb = pix if bpp == 3 else bytes(b for i, b in enumerate(pix) if i % 4 != 3)
        fd, path = tempfile.mkstemp(suffix=".png")
        with os.fdopen(fd, "wb") as f:
            f.write(png)
        try:
            ok &= read_png(path) == (w, h, rgb)
            try:
                from PIL import Image
            except ImportError:
                Image = None
            if Image is not None:
                with Image.open(path) as im:
                    ok &= im.convert("RGB").tobytes() == rgb
        finally:
            os.remove(path)
    return ok


def self_test():
    """A smooth synthetic road frame (a gradient, a soft lamp pool, mild noise) scores 0; the same frame with
    five vertical bands of 6 to 16 columns, 10 to 20 levels brighter or darker, finds each band as one run at
    its place. numpy and plain Python give the same numbers."""
    import random
    random.seed(7)
    w, h = 1280, 120

    def frame(bands):
        out = bytearray()
        for y in range(h):
            for x in range(w):
                v = 40 + 60 * y / h + 50 * max(0.0, 1 - ((x - 640) / 360) ** 2) + random.uniform(-2, 2)
                for x0, x1, d in bands:
                    if x0 <= x <= x1:
                        v += d
                v = max(0, min(255, int(v)))
                out += bytes((v, v, v))
        return bytes(out)

    smooth = frame([])
    bands = [(80, 85, 15), (280, 293, -20), (600, 607, 12), (920, 930, 18), (1160, 1166, -10)]
    banded = frame(bands)
    import tempfile   # the PPM reader too: a commented header, as some writers make it
    fd, ppm = tempfile.mkstemp(suffix=".ppm")
    with os.fdopen(fd, "wb") as f:
        f.write(b"P6\n# band_measure self-test\n%d %d\n255\n" % (w, h) + banded)
    try:
        ok = read_frame(ppm) == (w, h, banded)
    finally:
        os.remove(ppm)
    png_ok = png_round_trip()
    ok &= png_ok
    r0 = measure(w, h, smooth)
    r1 = measure(w, h, banded)
    ok &= r0["columns"] == 0
    found = sorted(r1["runs"])
    for x0, x1, d in bands:
        hit = [r for r in found if r[0] <= x1 and r[1] >= x0]
        ok &= len(hit) == 1 and (hit[0][2] > 0) == (d > 0)
    global np
    saved, np = np, None
    try:
        r2 = measure(w, h, banded)
    finally:
        np = saved
    same = saved is None or (r2["columns"] == r1["columns"]
                             and sorted(r[:2] for r in r2["runs"]) == sorted(r[:2] for r in r1["runs"]))
    # the step mode: 64-pixel strips each 2 to 5 levels off their neighbours (the Odin's night bands) against the
    # smooth frame (a lamp pool's slope) and against hard edges off the grid (a car, a post)
    strips = frame([(i * 64, i * 64 + 63, (2 + i % 4) * (1 if i % 2 else -1)) for i in range(20)])
    edges = frame([(100, 140, 12), (500, 530, -10), (935, 990, 15)])
    s0, s1, s2 = steps(w, h, smooth), steps(w, h, strips), steps(w, h, edges)
    step_ok = (s0["boundary_median"] < 1.0 and s1["boundary_median"] >= 2.0 and (s1["ratio"] or 0) >= 3
               and len(s1["over_2"]) == s1["boundaries"] and s2["boundary_median"] < 1.0 and (s2["ratio"] or 0) < 2)
    # --against: the strips' frame against the same frame without them finds every boundary; a frame against itself,
    # edges and all, finds none
    d1 = steps(w, h, difference(strips, smooth))
    d0 = steps(w, h, difference(edges, edges))
    step_ok &= len(d1["over_2"]) == d1["boundaries"] and d0["boundary_max"] == 0.0
    # add_strips at 4 levels on the edges frame, against it, either statistic; and stat=mean keeps the keys
    s2l = add_strips(w, edges, 4)
    m1 = steps(w, h, difference(s2l, edges), stat="mean")
    d3 = steps(w, h, difference(s2l, edges))
    step_ok &= (m1["stat"] == "mean" and set(m1) == set(d3) and 3.5 <= m1["boundary_median"] <= 4.5
                and 3.5 <= d3["boundary_median"] <= 4.5)
    # a picture between side bars (columns 160-1120): strips on its own grid are found with content=, and the
    # whole-width grid (the wrong origin and spacing) sees far fewer of them
    bars = bytes(v if 160 <= (i // 3) % w < 1120 else 0 for i, v in enumerate(edges))
    sb = add_strips(w, bars, 4, content=(160, 1120))
    c1 = steps(w, h, difference(sb, bars), content=(160, 1120))
    c2 = steps(w, h, difference(sb, bars))
    step_ok &= c1["boundaries"] == 19 and len(c1["over_2"]) == 19 and len(c2["over_2"]) < c2["boundaries"] // 2
    saved2, np = np, None
    try:
        s3 = steps(w, h, strips)
        d2 = steps(w, h, difference(strips, smooth))
    finally:
        np = saved2
    step_ok &= (s3["over_2"] == s1["over_2"] and s3["boundary_median"] == s1["boundary_median"]
                and d2["over_2"] == d1["over_2"])
    ok &= step_ok
    print(f"steps: smooth boundary median {s0['boundary_median']} (ratio {s0['ratio']}); strips "
          f"{s1['boundary_median']}"
          f" (ratio {s1['ratio']}, {len(s1['over_2'])} of {s1['boundaries']} boundaries over 2); edges off the grid "
          f"{s2['boundary_median']} (ratio {s2['ratio']}); against a reference: strips {len(d1['over_2'])} of "
          f"{d1['boundaries']}, itself {d0['boundary_max']}: {'right' if step_ok else 'WRONG'}")
    print(f"PNG reader (RGB and RGBA, the five filters): {'right' if png_ok else 'WRONG'}")
    print(f"smooth: {r0['columns']} columns over; banded: score {r1['score']}, runs {found}")
    print(f"numpy {'and plain Python agree' if same else 'and plain Python DIFFER'}"
          if saved is not None else "plain Python only (no numpy)")
    print("self-test", "passed" if ok and same else "FAILED")
    return 0 if ok and same else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    rows, threshold, window, as_json, paths = None, 6.0, 33, False, []
    step_mode, grid, against, stat, content = False, 64.0, None, "median", None
    it = iter(argv)
    for a in it:
        if a == "--json":
            as_json = True
        elif a == "--steps":
            step_mode = True
        elif a == "--grid":
            grid = float(next(it))
        elif a == "--against":
            against = next(it)
        elif a == "--content":
            content = tuple(int(v) for v in next(it).split("-"))
        elif a == "--stat":
            stat = next(it)
            if stat not in ("median", "mean"):
                print("--stat is median or mean")
                return 2
        elif a == "--rows":
            rows = tuple(float(v) for v in next(it).split("-"))
        elif a == "--threshold":
            threshold = float(next(it))
        elif a == "--window":
            window = int(next(it))
        else:
            paths.append(a)
    if not paths:
        print(__doc__)
        return 2
    step_results = {}
    for p in frames_in(paths):
        w, h, rgb = read_frame(p)
        if step_mode and against:
            ref = against
            if os.path.isdir(against):   # paired by the name's stem: the device's .ppm against a reference's .png
                stem = os.path.splitext(os.path.basename(p))[0]
                ref = next((os.path.join(against, stem + e) for e in (".ppm", ".png", ".PPM", ".PNG")
                            if os.path.exists(os.path.join(against, stem + e))), os.path.join(against, stem))
            if not os.path.exists(ref):
                print(f"{os.path.basename(p)}: no reference frame {ref}", flush=True)
                continue
            rw, rh, rrgb = read_frame(ref)
            if (rw, rh) != (w, h):
                print(f"{os.path.basename(p)}: {w}x{h} against a {rw}x{rh} reference: skipped", flush=True)
                continue
            rgb = difference(rgb, rrgb)
        if step_mode:
            r = {"frame": os.path.basename(p),
                 **steps(w, h, rgb, rows or (0.45, 0.72), grid, stat=stat, content=content)}
            step_results.setdefault((w, h), []).append(r)
            r = {k: v for k, v in r.items() if k != "at"}
            if as_json:
                print(json.dumps(r), flush=True)
            else:
                top = ", ".join(f"x {b} {s:+.1f}" for b, s in r["over_2"][:6])
                print(f"{r['frame']}: steps on the {r['grid']}-pixel grid: median {r['boundary_median']} against "
                      f"{r['elsewhere_median']} elsewhere (ratio {r['ratio']}), largest {r['boundary_max']}; "
                      f"{len(r['over_2'])} of {r['boundaries']} boundaries over 2 levels{': ' + top if top else ''}",
                      flush=True)
            continue
        r = measure(w, h, rgb, rows or (0.45, 0.72), threshold, window)
        r = {"frame": os.path.basename(p), **r}
        if as_json:
            print(json.dumps(r), flush=True)
        else:
            runs = ", ".join(f"x {a}-{b} ({a / w:.3f}-{b / w:.3f}) {d:+.1f}" for a, b, d in r["runs"][:6])
            more = f" (+{len(r['runs']) - 6} more)" if len(r["runs"]) > 6 else ""
            print(f"{r['frame']}: score {r['score']:.4f}, {r['columns']} columns; {runs or 'no runs'}{more}",
                  flush=True)
    for (w, h), results in step_results.items():
        if len(results) > 1:
            a = steps_over_frames(results)
            if as_json:
                print(json.dumps({"over_frames": f"{w}x{h}", **a}), flush=True)
            else:
                top = ", ".join(f"x {b} {m}" for b, m in a["over_2"][:8])
                print(f"over {a['frames']} frames ({w}x{h}): each boundary's median step, their median "
                      f"{a['boundary_median']}; {len(a['over_2'])} boundaries over 2 levels"
                      f"{': ' + top if top else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

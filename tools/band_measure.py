#!/usr/bin/env python3
"""Vertical bands in a frame: a triage score for the owner's night bands (C6b's device item, 2026-11-09).

    py tools/band_measure.py FRAME_OR_DIR... [--json] [--rows 0.45-0.72] [--threshold 6] [--window 33]
    py tools/band_measure.py --self-test

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
    print(f"PNG reader (RGB and RGBA, the five filters): {'right' if png_ok else 'WRONG'}")
    print(f"smooth: {r0['columns']} columns over; banded: score {r1['score']}, runs {found}")
    print(f"numpy {'and plain Python agree' if same else 'and plain Python DIFFER'}"
          if saved is not None else "plain Python only (no numpy)")
    print("self-test", "passed" if ok and same else "FAILED")
    return 0 if ok and same else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    rows, threshold, window, as_json, paths = (0.45, 0.72), 6.0, 33, False, []
    it = iter(argv)
    for a in it:
        if a == "--json":
            as_json = True
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
    for p in frames_in(paths):
        w, h, rgb = read_frame(p)
        r = measure(w, h, rgb, rows, threshold, window)
        r = {"frame": os.path.basename(p), **r}
        if as_json:
            print(json.dumps(r), flush=True)
        else:
            runs = ", ".join(f"x {a}-{b} ({a / w:.3f}-{b / w:.3f}) {d:+.1f}" for a, b, d in r["runs"][:6])
            more = f" (+{len(r['runs']) - 6} more)" if len(r["runs"]) > 6 else ""
            print(f"{r['frame']}: score {r['score']:.4f}, {r['columns']} columns; {runs or 'no runs'}{more}",
                  flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

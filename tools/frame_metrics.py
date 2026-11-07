#!/usr/bin/env python3
"""Night artefact metrics over a run of captured frames (docs/modern/PLAN.md ticket N1).

    py tools/frame_metrics.py FRAMES_DIR [--notes NOTES.json] [--json OUT.json] [step|bands|tint|all]
    py tools/frame_metrics.py --selftest

FRAMES_DIR holds the frames as PPM (P6, 8-bit), in frame order by the last number in their names
(`gt4_host --capture PATH --capture-every 1` writes PATH_000001.ppm ...; C1's --capture-frames every:1 later).
They are game frames: they stay in the clone's build/ folder and are never committed.

Three metrics, one per Route 5 night artefact (docs/modern/ARCHITECTURE.md section 8):

- step: per frame, the mean natural log of Rec.709 luminance Y (sRGB decoded to linear) over the scene
  rectangle with the HUD masked. A step is a frame-to-frame change of exp(|d|) - 1 above `step_threshold`
  (default 4 %) that is not a camera cut. A cut is a frame whose luminance structure (the brightness-normalised
  64 x 36 log-luminance image) correlates with the previous frame's below `cut_correlation` (default 0.5), or a
  frame the notes list in `cuts`. Reported: every step, the largest change, and the change at each crossing
  frame the notes list (`crossings`, +-`crossing_window` frames).
- bands: for each entry of the notes' `bands` (a band rectangle and a rectangle of the road beside it, from
  R11), per frame the ratio of the band's mean Y to the road's mean Y. Reported: per band, the largest ratio and
  the frame of it.
- tint: per frame, the mean Oklab a and b of the scene rectangle (HUD masked) outside a disc of `lamp_radius`
  (default 8 %) of the frame height around each warning lamp (the notes' `lamps`). The blink signal is the
  lamps' mean Y inside their discs, split into on and off at the midpoint of its range (or the notes' `blink`
  0/1 list). The slow trend of each series is taken away (a centred mean over `tint_periods` whole blink
  periods, default 2, which keeps the blink's own part whole), and the tint is the mean of the on frames minus
  the mean of the off frames: peak to peak, not half of it. Reported: |tint a|, |tint b|, the blink count and
  period.

Notes file (JSON, beside each capture; all rectangles and positions are fractions of the frame, x y w h):

    {"scene": [0, 0, 1, 1],                 # the scene rectangle (default the whole frame)
     "hud": [[x, y, w, h], ...],            # HUD rectangles left out of step and tint
     "cuts": [frame, ...],                  # camera cuts, frame numbers as in the file names
     "crossings": [frame, ...],             # section boundary crossings (N0 notes)
     "bands": [{"band": [x, y, w, h], "beside": [x, y, w, h], "frames": [first, last]}, ...],
     "lamps": [[x, y], ...]                 # fixed warning-lamp positions, or
     "lamp_track": {"frame": [[x, y], ...], ...},   # keyed positions, held from each key to the next
     "blink": [0, 1, ...]}                  # optional, one per frame from the first: 1 when the lamps are on

Needs numpy; without it the self-test exits 77 (skipped).
"""
import argparse
import json
import math
import os
import re
import sys

try:
    import numpy as np
except ImportError:   # the self-test skips; the tool says why
    np = None

DEFAULTS = {
    "step_threshold": 0.04,
    "cut_correlation": 0.5,
    "crossing_window": 3,
    "lamp_radius": 0.08,
    "tint_periods": 2,
}


# --- pixels ---------------------------------------------------------------------------------------------------

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # header: P6, width, height, maxval, separated by whitespace (comments allowed)
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
    pos += 1
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"{path}: only 8-bit P6 PPM is read")
    w, h = int(fields[1]), int(fields[2])
    pixels = np.frombuffer(data, np.uint8, w * h * 3, pos)
    return pixels.reshape(h, w, 3)


_LUT = None


def linear(rgb8):
    """sRGB 8-bit to linear float32, by table."""
    global _LUT
    if _LUT is None:
        c = np.arange(256, dtype=np.float64) / 255.0
        _LUT = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4).astype(np.float32)
    return _LUT[rgb8]


def luminance(lin):
    return lin[..., 0] * 0.2126 + lin[..., 1] * 0.7152 + lin[..., 2] * 0.0722


def oklab_ab(lin):
    """Oklab a and b of linear sRGB (Ottosson 2020)."""
    r, g, b = lin[..., 0], lin[..., 1], lin[..., 2]
    l = np.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
    m = np.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
    s = np.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
    return (1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def box(rect, w, h):
    x, y, rw, rh = rect
    x0, y0 = int(round(x * w)), int(round(y * h))
    x1, y1 = int(round((x + rw) * w)), int(round((y + rh) * h))
    return max(0, x0), max(0, y0), min(w, x1), min(h, y1)


# --- frames ---------------------------------------------------------------------------------------------------

def frame_files(directory):
    found = []
    for name in os.listdir(directory):
        if name.lower().endswith(".ppm"):
            numbers = re.findall(r"\d+", name)
            found.append((int(numbers[-1]) if numbers else 0, name))
    found.sort()
    return [(n, os.path.join(directory, name)) for n, name in found]


class Masks:
    """Scene rectangle minus HUD, at one frame size."""

    def __init__(self, notes, w, h):
        self.w, self.h = w, h
        scene = np.zeros((h, w), bool)
        x0, y0, x1, y1 = box(notes.get("scene", [0, 0, 1, 1]), w, h)
        scene[y0:y1, x0:x1] = True
        for rect in notes.get("hud", []):
            x0, y0, x1, y1 = box(rect, w, h)
            scene[y0:y1, x0:x1] = False
        self.scene = scene
        yy, xx = np.mgrid[0:h, 0:w]
        self.yy, self.xx = yy.astype(np.float32), xx.astype(np.float32)

    def discs(self, lamps, radius):
        out = np.zeros((self.h, self.w), bool)
        r = radius * self.h
        for x, y in lamps:
            out |= (self.xx - x * self.w) ** 2 + (self.yy - y * self.h) ** 2 <= r * r
        return out


def lamps_at(notes, frame):
    track = notes.get("lamp_track")
    if track:
        keys = sorted((int(k), v) for k, v in track.items())
        current = keys[0][1]
        for k, v in keys:
            if k <= frame:
                current = v
        return current
    return notes.get("lamps", [])


def structure(logy):
    """The brightness-normalised 64 x 36 log-luminance image (a cut changes it, a uniform gain does not)."""
    h, w = logy.shape
    gh, gw = 36, 64
    small = logy[: h - h % gh, : w - w % gw].reshape(gh, h // gh, gw, w // gw).mean(axis=(1, 3))
    small = small - small.mean()
    norm = math.sqrt(float((small * small).sum()))
    # a flat frame (black, a fade's end) has no structure to compare: None, and never a cut
    return small / norm if norm > 1e-3 else None


def per_frame(files, notes, cfg, want_tint):
    rows, masks, prev_struct = [], None, None
    for n, path in files:
        rgb8 = read_ppm(path)
        h, w, _ = rgb8.shape
        if masks is None or (masks.w, masks.h) != (w, h):
            masks = Masks(notes, w, h)
        lin = linear(rgb8)
        y = luminance(lin)
        logy = np.log(np.maximum(y, 1e-4))
        st = structure(np.where(masks.scene, logy, float(logy[masks.scene].mean())))
        corr = float((st * prev_struct).sum()) if st is not None and prev_struct is not None else 1.0
        prev_struct = st
        row = {"frame": n, "logY": float(logy[masks.scene].mean()), "corr": corr}
        bands = []
        for b in notes.get("bands", []):
            first, last = b.get("frames", [-1, 1 << 30])
            if first <= n <= last:
                bx0, by0, bx1, by1 = box(b["band"], w, h)
                rx0, ry0, rx1, ry1 = box(b["beside"], w, h)
                road = float(y[ry0:ry1, rx0:rx1].mean())
                bands.append(float(y[by0:by1, bx0:bx1].mean()) / max(road, 1e-6))
            else:
                bands.append(None)
        row["bands"] = bands
        if want_tint:
            lamps = lamps_at(notes, n)
            disc = masks.discs(lamps, cfg["lamp_radius"])
            rest = masks.scene & ~disc
            a, b = oklab_ab(lin)
            row["a"], row["b"] = float(a[rest].mean()), float(b[rest].mean())
            row["lampY"] = float(y[disc].mean()) if disc.any() else 0.0
        rows.append(row)
    return rows


# --- metrics --------------------------------------------------------------------------------------------------

def step_metric(rows, notes, cfg):
    cuts = set(notes.get("cuts", []))
    steps, changes = [], []
    for prev, row in zip(rows, rows[1:]):
        change = math.exp(abs(row["logY"] - prev["logY"])) - 1.0
        cut = row["frame"] in cuts or row["corr"] < cfg["cut_correlation"]
        changes.append({"frame": row["frame"], "change": change, "cut": cut})
        if change > cfg["step_threshold"] and not cut:
            steps.append({"frame": row["frame"], "change": round(change, 5)})
    clean = [c for c in changes if not c["cut"]]
    crossings = []
    for f in notes.get("crossings", []):
        near = [c for c in clean if abs(c["frame"] - f) <= cfg["crossing_window"]]
        if near:
            top = max(near, key=lambda c: c["change"])
            crossings.append({"crossing": f, "frame": top["frame"], "change": round(top["change"], 5)})
    top = max(clean, key=lambda c: c["change"]) if clean else None
    return {"frames": len(rows), "cuts": [c["frame"] for c in changes if c["cut"]],
            "steps": steps, "max": ({"frame": top["frame"], "change": round(top["change"], 5)} if top else None),
            "crossings": crossings}


def bands_metric(rows, notes):
    out = []
    for i, b in enumerate(notes.get("bands", [])):
        values = [(r["frame"], r["bands"][i]) for r in rows if r["bands"][i] is not None]
        if values:
            f, v = max(values, key=lambda t: t[1])
            out.append({"band": b["band"], "frames": len(values), "max_ratio": round(v, 4), "frame": f,
                        "mean_ratio": round(sum(v for _, v in values) / len(values), 4)})
    return out


def blink_period(on):
    """Frames per blink cycle (on + off), from the lengths of the runs between edges."""
    edges = np.flatnonzero(on[1:] != on[:-1]) + 1
    runs = np.diff(edges)
    return 2.0 * float(runs.mean()) if len(runs) else float(len(on))


def in_step(x, on, period, periods):
    """The part of x in step with the blink: the mean of the on frames minus the mean of the off frames, after
    removing the slow trend. The trend is a centred moving average over a whole number of blink periods, which
    leaves the blink's own square wave out of it (a box of length k * period has zeros at the blink's frequency
    and its harmonics), so nothing of the in-step part is taken away with the trend. A series too short for that
    window is fitted instead: x on the blink plus a linear trend."""
    n = len(x)
    window = int(round(periods * period))
    if window >= 3 and n - window + 1 >= 2 * period:
        # only where the window fits: no padding at the ends to bias them
        k = window // 2
        trend = np.convolve(x, np.ones(window) / window, mode="valid")
        h = x[k : k + len(trend)] - trend
        o = on[k : k + len(trend)]
        if o.any() and not o.all():
            return float(h[o].mean() - h[~o].mean())
    t = np.linspace(-1.0, 1.0, n)
    design = np.stack([on.astype(float), np.ones(n), t], axis=1)
    coef, *_ = np.linalg.lstsq(design, x, rcond=None)
    return float(coef[0])


def tint_metric(rows, notes, cfg):
    a = np.array([r["a"] for r in rows])
    b = np.array([r["b"] for r in rows])
    if "blink" in notes:
        if len(notes["blink"]) < len(rows):
            raise SystemExit(f"the notes' blink list has {len(notes['blink'])} entries for {len(rows)} frames")
        on = np.array(notes["blink"][: len(rows)], dtype=bool)
    else:
        lamp = np.array([r["lampY"] for r in rows])
        on = lamp > (lamp.min() + lamp.max()) / 2 if lamp.max() > lamp.min() else np.zeros(len(rows), bool)
    edges = int(np.count_nonzero(on[1:] != on[:-1]))
    if on.all() or not on.any():
        return {"frames": len(rows), "blinks": 0, "tint_a": None, "tint_b": None,
                "note": "no blink in the lamp signal: check the notes' lamps"}
    period = blink_period(on)
    ta = in_step(a, on, period, cfg["tint_periods"])
    tb = in_step(b, on, period, cfg["tint_periods"])
    return {"frames": len(rows), "blinks": edges // 2, "on_frames": int(on.sum()),
            "blink_period_frames": round(period, 2),
            "tint_a": round(abs(ta), 6), "tint_b": round(abs(tb), 6),
            "tint_is": "lamps on minus lamps off (peak to peak), Oklab, outside the lamp discs",
            "signed": {"a": round(ta, 6), "b": round(tb, 6)},
            "mean": {"a": round(float(a.mean()), 6), "b": round(float(b.mean()), 6)}}


def measure(directory, notes, which, cfg):
    files = frame_files(directory)
    if not files:
        raise SystemExit(f"no .ppm frames in {directory}")
    want_tint = which in ("tint", "all") and (notes.get("lamps") or notes.get("lamp_track"))
    rows = per_frame(files, notes, cfg, want_tint)
    out = {"directory": os.path.abspath(directory), "first": files[0][0], "last": files[-1][0]}
    if which in ("step", "all"):
        out["step"] = step_metric(rows, notes, cfg)
    if which in ("bands", "all") and notes.get("bands"):
        out["bands"] = bands_metric(rows, notes)
    if want_tint:
        out["tint"] = tint_metric(rows, notes, cfg)
    out["series"] = [{k: (round(v, 6) if isinstance(v, float) else v) for k, v in r.items()} for r in rows]
    return out


# --- self-test ------------------------------------------------------------------------------------------------

def write_ppm(path, rgb8):
    h, w, _ = rgb8.shape
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(np.ascontiguousarray(rgb8, np.uint8).tobytes())


def to8(lin):
    c = np.clip(lin, 0, 1)
    s = np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)
    return np.round(s * 255).astype(np.uint8)


def selftest():
    if np is None:
        print("frame_metrics_selftest: numpy not installed, skipped")
        return 77
    import tempfile
    failures = []

    def expect(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            failures.append(what)

    rng = np.random.default_rng(4)
    w, h = 320, 180
    base = rng.uniform(0.02, 0.2, (h // 4, w // 4, 1)).repeat(4, 0).repeat(4, 1) * np.ones((1, 1, 3))
    other = rng.uniform(0.02, 0.2, (h // 4, w // 4, 1)).repeat(4, 0).repeat(4, 1) * np.ones((1, 1, 3))
    hud = [0.75, 0.75, 0.25, 0.25]
    cfg = dict(DEFAULTS)
    with tempfile.TemporaryDirectory(prefix="gt4_frame_metrics_") as tmp:
        # step: 40 frames, a slow drift of 0.5 % a frame, a 6 % step at frame 20, a cut at frame 30, a blinking
        # HUD element that must not count, and a 3 % change at frame 35 (below the bar)
        d = os.path.join(tmp, "step")
        os.makedirs(d)
        gain = 1.0
        for i in range(40):
            gain *= 1.005
            if i == 20:
                gain *= 1.06
            if i == 35:
                gain *= 1.03
            img = (other if i >= 30 else base) * gain
            if i % 2:
                x0, y0, x1, y1 = box(hud, w, h)
                img = img.copy()
                img[y0:y1, x0:x1] = 1.0
            write_ppm(os.path.join(d, f"f_{i:06d}.ppm"), to8(img))
        r = measure(d, {"hud": [hud], "crossings": [20]}, "step", cfg)["step"]
        expect([s["frame"] for s in r["steps"]] == [20], f"one step, at frame 20 (got {r['steps']})")
        expect(abs(r["steps"][0]["change"] - (1.06 * 1.005 - 1)) < 0.01 if r["steps"] else False,
               "the step's size within 1 % of 6.5 %")
        expect(r["cuts"] == [30], f"the cut at frame 30 found and not counted (got {r['cuts']})")
        expect(r["crossings"] and r["crossings"][0]["frame"] == 20, "the crossing's change reported at frame 20")
        r2 = measure(d, {"crossings": [20]}, "step", cfg)["step"]
        expect(len(r2["steps"]) > 5, "without the HUD mask the blinking HUD counts as steps")

        # bands: a band at 2.5x the road beside it, on frames 5..9 only
        d = os.path.join(tmp, "bands")
        os.makedirs(d)
        road = np.full((h, w, 3), 0.05)
        band, beside = [0.4, 0.5, 0.1, 0.5], [0.55, 0.5, 0.1, 0.5]
        for i in range(12):
            img = road.copy()
            if 5 <= i <= 9:
                x0, y0, x1, y1 = box(band, w, h)
                img[y0:y1, x0:x1] *= 2.5
            write_ppm(os.path.join(d, f"f_{i:06d}.ppm"), to8(img))
        r = measure(d, {"bands": [{"band": band, "beside": beside}]}, "bands", cfg)["bands"][0]
        expect(abs(r["max_ratio"] - 2.5) < 0.05, f"band ratio 2.5 (got {r['max_ratio']})")
        expect(5 <= r["frame"] <= 9, "the band's frame")

        # tint. A yellow lamp blinks over a warm road that drifts slowly; with `tinted`, the whole frame outside
        # the lamp gets yellower while the lamp is on. The truth, frame by frame, is the tinted run's b minus the
        # same run's b without the tint (the drift and the lamp cancel), averaged over the lamp-on frames. Short
        # blinks (8 on, 8 off) and a 1 Hz blink (30 on, 30 off), which a trend window shorter than the period
        # would halve.
        lamp = [0.3, 0.4]
        lamps2 = [[0.3, 0.4], [0.7, 0.6]]
        warm = base * np.array([1.0, 0.85, 0.6])   # a sodium-lit road: a and b away from 0
        def run(name, half, frames, tinted, drift, disc_radius=0.03, lamp_radius=None, lamps=None):
            lamps = lamps or [lamp]
            d = os.path.join(tmp, name)
            os.makedirs(d)
            disc = Masks({}, w, h).discs(lamps, disc_radius)
            for i in range(frames):
                on = (i // half) % 2 == 0
                img = warm * (1 + drift * i)
                if on:
                    img = img.copy()
                    img[disc] = [1.0, 0.85, 0.0]
                    if tinted:
                        img = img * np.array([1.06, 1.02, 0.85])
                write_ppm(os.path.join(d, f"f_{i:06d}.ppm"), to8(img))
            c = dict(cfg)
            if lamp_radius is not None:
                c["lamp_radius"] = lamp_radius
            return measure(d, {"lamps": lamps}, "tint", c)
        def truth(clean_result, tinted_result):
            on = np.array([(r["frame"] // half) % 2 == 0 for r in clean_result["series"]])
            diff = np.array([t["b"] - c["b"] for c, t in zip(clean_result["series"], tinted_result["series"])])
            return float(diff[on].mean())
        for half, frames in ((8, 96), (30, 300)):
            clean_all = run(f"clean{half}", half, frames, False, 0.004)
            tinted_all = run(f"tint{half}", half, frames, True, 0.004)
            clean, tinted = clean_all["tint"], tinted_all["tint"]
            exact = truth(clean_all, tinted_all)
            expect(clean["blinks"] >= 4, f"{half}/{half} blink: blinks found from the lamp ({clean['blinks']})")
            expect(clean["tint_b"] < 0.002 and clean["tint_a"] < 0.002,
                   f"{half}/{half} blink: no tint when only the lamp blinks (a {clean['tint_a']}, b {clean['tint_b']})")
            expect(exact > 0.005 and abs(tinted["signed"]["b"] - exact) < 0.1 * exact,
                   f"{half}/{half} blink: tint b {tinted['signed']['b']} within 10 % of the true {exact:.5f}")
        # the lamps' own discs must be left out: two big bright lamps with no frame tint read as a tint without
        # the mask (the 8 % discs cover them)
        masked = run("bigmask", 8, 96, False, 0.0, disc_radius=0.075, lamps=lamps2)["tint"]
        unmasked = run("bignomask", 8, 96, False, 0.0, disc_radius=0.075, lamp_radius=0.001, lamps=lamps2)["tint"]
        expect(masked["tint_b"] < 0.002 and unmasked["tint_b"] > 0.002,
               f"the lamp disc is masked (b {masked['tint_b']} with the mask, {unmasked['tint_b']} without)")

        # Rec.709 luminance of decoded sRGB (Rec.601 weights or no decoding would fail)
        g = luminance(linear(np.array([[[0, 255, 0], [128, 128, 128]]], np.uint8)))
        expect(abs(g[0, 0] - 0.7152) < 1e-4 and abs(g[0, 1] - 0.2158605) < 1e-4,
               f"Rec.709 Y of sRGB green and mid grey ({g[0, 0]:.5f}, {g[0, 1]:.5f})")

        # a step that is not a uniform gain: half the frame doubles. The mean of log Y changes by sqrt(2) (41.4 %);
        # the mean of Y would change by about 9 %. Then a fade to black and back: black frames are not cuts.
        d = os.path.join(tmp, "halves")
        os.makedirs(d)
        tex = base / base.mean()
        for i in range(30):
            img = tex.copy()
            img[:, : w // 2] *= 0.02 if i >= 10 else 0.01
            img[:, w // 2 :] *= 0.1
            if 20 <= i < 24:
                img = img * 0.0
            write_ppm(os.path.join(d, f"f_{i:06d}.ppm"), to8(img))
        r = measure(d, {}, "step", cfg)["step"]
        at10 = [s for s in r["steps"] if s["frame"] == 10]
        expect(bool(at10) and abs(at10[0]["change"] - 0.4142) < 0.03,
               f"half the frame doubling is a 41 % step in log luminance (got {at10})")
        expect(not any(20 <= c <= 24 for c in r["cuts"]), f"black frames are not cuts (cuts {r['cuts']})")
    print("frame_metrics_selftest: " + ("all checks passed" if not failures else f"{len(failures)} failed"))
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("frames", nargs="?", help="folder of PPM frames")
    ap.add_argument("metric", nargs="?", default="all", choices=["step", "bands", "tint", "all"])
    ap.add_argument("--notes", help="the capture's notes (JSON)")
    ap.add_argument("--json", help="write the full result, per-frame series included, here")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="override a default: " + ", ".join(f"{k}={v}" for k, v in DEFAULTS.items()))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if np is None:
        raise SystemExit("frame_metrics.py needs numpy (py -m pip install numpy)")
    if not a.frames:
        ap.error("a frames folder is required")
    cfg = dict(DEFAULTS)
    for kv in a.set:
        k, _, v = kv.partition("=")
        if k not in cfg:
            ap.error(f"unknown setting {k}")
        cfg[k] = type(cfg[k])(v)
    notes = {}
    if a.notes:
        with open(a.notes, encoding="utf-8") as f:
            notes = json.load(f)
    out = measure(a.frames, notes, a.metric, cfg)
    if a.json:
        with open(a.json, "w", newline="\n") as f:
            json.dump(out, f, indent=1)
    summary = {k: v for k, v in out.items() if k != "series"}
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())

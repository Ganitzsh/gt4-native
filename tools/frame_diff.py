#!/usr/bin/env python3
"""Compare frames: two PPM files, two folders of them, or two hosts replaying captures (docs/modern/PLAN.md C1).

    py tools/frame_diff.py A B [--heatmap DIR] [--json FILE] [--min-identical P] [--min-within4 P] [--tolerance N]
    py tools/frame_diff.py --baseline OLD_HOST NEW_HOST --captures C1.cap[,C2.cap...] [--presets original,low,...]
                           [--frames 1-100] [--size WxH] [--set KEY=VALUE ...] [--work DIR] [same checks]

Per frame: the share of identical pixels, of pixels within --tolerance (4) on every channel, the largest channel
error, PSNR (inf when identical), and the share of lit pixels in A (a frame or capture that is all black proves
nothing). Folders are compared by matching file names. --heatmap writes, per differing frame, a PNG of the error
(black: equal; red: the largest error of the frame). The exit code is 1 when a frame is under --min-identical or
--min-within4 (percent; default 100: identical), or when a capture shows no picture, else 0.

--baseline replays each capture with both hosts at each preset (gt4_host --headless --replay ... --capture-frames
... --capture-dir), with display.frame_interpolation=off and texture.hd_pack=off unless --set says otherwise (both
make a replay differ from run to run: they follow the wall clock and the texture pack's loading threads), and
compares the frames. Frames are written under --work (default build/frame_diff) and removed after a capture's
comparison unless --keep.

PPM files only (P6, 8 bits): what gt4_host --capture and --capture-frames write. numpy is used when it is there
(fast); without it the comparison runs in plain Python (slow on full frames, enough for the self-test).
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import zlib

try:
    import numpy as np
except ImportError:  # (plain Python below)
    np = None

NO_WINDOW = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}


def read_ppm(path):
    """(width, height, RGB bytes) of a binary 8-bit PPM."""
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:   # magic, width, height, max, separated by whitespace (comments are not written)
        while data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"{path}: not an 8-bit binary PPM")
    w, h = int(fields[1]), int(fields[2])
    pixels = data[pos + 1:pos + 1 + w * h * 3]
    if len(pixels) != w * h * 3:
        raise ValueError(f"{path}: truncated")
    return w, h, pixels


def write_png(path, w, h, rgb):
    def chunk(kind, payload):
        return struct.pack("!I", len(payload)) + kind + payload + struct.pack("!I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def compare(a_path, b_path, tolerance=4, heatmap=None):
    """The metrics of one pair of frames (and its heat map, when asked and when they differ)."""
    wa, ha, a = read_ppm(a_path)
    wb, hb, b = read_ppm(b_path)
    if (wa, ha) != (wb, hb):
        return {"size": f"{wa}x{ha} vs {wb}x{hb}", "identical": 0.0, "within": 0.0, "max_error": 255,
                "psnr": 0.0, "lit": 0.0}
    n = wa * ha
    if np is not None:
        x = np.frombuffer(a, np.uint8).reshape(ha, wa, 3).astype(np.int16)
        y = np.frombuffer(b, np.uint8).reshape(ha, wa, 3).astype(np.int16)
        err = np.abs(x - y).max(axis=2)
        identical = float((err == 0).sum()) / n
        within = float((err <= tolerance).sum()) / n
        worst = int(err.max())
        mse = float(((x - y).astype(np.float64) ** 2).mean())
        lit = float((x.max(axis=2) > 0).sum()) / n
        heat = err
    else:
        same = close = lit_count = 0
        worst, squares = 0, 0
        heat = []
        for i in range(n):
            e = max(abs(a[3 * i + k] - b[3 * i + k]) for k in range(3))
            squares += sum((a[3 * i + k] - b[3 * i + k]) ** 2 for k in range(3))
            same += e == 0
            close += e <= tolerance
            lit_count += max(a[3 * i:3 * i + 3]) > 0
            worst = max(worst, e)
            heat.append(e)
        identical, within, lit = same / n, close / n, lit_count / n
        mse = squares / (3 * n)
    psnr = math.inf if mse == 0 else 10 * math.log10(255 * 255 / mse)
    if heatmap and worst:
        os.makedirs(heatmap, exist_ok=True)
        scale = 255.0 / worst
        if np is not None:
            red = (heat * scale).astype(np.uint8)
            img = np.zeros((ha, wa, 3), np.uint8)
            img[..., 0] = red
            data = img.tobytes()
        else:
            data = b"".join(bytes((int(e * scale), 0, 0)) for e in heat)
        write_png(os.path.join(heatmap, os.path.splitext(os.path.basename(a_path))[0] + "_diff.png"), wa, ha, data)
    return {"identical": identical, "within": within, "max_error": worst, "psnr": psnr, "lit": lit}


def compare_folders(a, b, args, label=""):
    """Results of every frame in folder a that b has too; missing frames are failures."""
    names = sorted(n for n in os.listdir(a) if n.lower().endswith(".ppm"))
    results = {}
    for n in names:
        if not os.path.exists(os.path.join(b, n)):
            results[n] = {"missing": True}
            continue
        results[n] = compare(os.path.join(a, n), os.path.join(b, n), args.tolerance, args.heatmap and os.path.join(
            args.heatmap, label) if label else args.heatmap)
    return results


def check(results, args, label):
    """Prints a summary of results; returns whether they pass."""
    ok = True
    frames = [r for r in results.values() if "missing" not in r]
    missing = [n for n, r in results.items() if "missing" in r]
    failing = [(n, r) for n, r in results.items() if "missing" not in r and
               (100 * r["identical"] < args.min_identical or 100 * r["within"] < args.min_within4)]
    lit = sum(1 for r in frames if r.get("lit", 0) > 0.05)
    identical = sum(1 for r in frames if r["identical"] == 1.0)
    worst = max((r["max_error"] for r in frames), default=0)
    print(f"{label or 'frames'}: {len(frames)} compared, {identical} identical, {lit} with a picture (over 5 % lit); "
          f"largest error {worst}; under the bar: {len(failing)}; missing: {len(missing)}")
    for n, r in sorted(failing, key=lambda t: t[1]["identical"])[:10]:
        print(f"  {n}: {100 * r['identical']:.3f} % identical, {100 * r['within']:.3f} % within {args.tolerance}, "
              f"max {r['max_error']}, PSNR {r['psnr']:.2f}")
    if failing or missing:
        ok = False
    if frames and not lit:
        print(f"  {label or 'frames'}: no frame shows a picture: the comparison proves nothing")
        ok = False
    return ok


def baseline(args):
    """Replays every capture with both hosts at every preset and compares the frames."""
    sets = [("display.frame_interpolation", "off"), ("texture.hd_pack", "off")]
    for kv in args.set or []:
        key, _, value = kv.partition("=")
        sets = [(k, v) for k, v in sets if k != key] + [(key, value)]
    last = max(int(p.split("/")[0].split("-")[-1]) for p in args.frames.split(",") if not p.startswith("every:"))
    ok, report = True, {}
    for capture in args.captures.split(","):
        cname = os.path.splitext(os.path.basename(capture))[0]
        for preset in args.presets.split(","):
            dirs = []
            for side, host in (("old", args.baseline[0]), ("new", args.baseline[1])):
                d = os.path.join(args.work, cname, preset, side)
                shutil.rmtree(d, ignore_errors=True)
                os.makedirs(d)
                cmd = [host, "--headless", "--replay", capture, "--frames", str(last), "--preset", preset,
                       "--capture-frames", args.frames, "--capture-dir", d]
                if args.size:
                    cmd += ["--size", args.size]
                for k, v in sets:
                    cmd += ["--set", f"{k}={v}"]
                r = subprocess.run(cmd, capture_output=True, text=True, **NO_WINDOW)
                with open(d + ".log", "w", encoding="utf-8") as log:
                    log.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
                if r.returncode != 0:
                    print(f"{cname} {preset} {side}: gt4_host exited {r.returncode} (see {d}.log)")
                    ok = False
                dirs.append(d)
            label = f"{cname} {preset}"
            results = compare_folders(dirs[0], dirs[1], args, label.replace(" ", "_"))
            report[label] = results
            ok = check(results, args, label) and ok
            if not args.keep:
                for d in dirs:
                    shutil.rmtree(d, ignore_errors=True)
    return ok, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", nargs="?", help="a PPM file or a folder of them")
    ap.add_argument("b", nargs="?", help="the PPM file or folder to compare it with")
    ap.add_argument("--baseline", nargs=2, metavar=("OLD_HOST", "NEW_HOST"), help="replay captures with two hosts")
    ap.add_argument("--captures", help="--baseline: comma-separated capture files")
    ap.add_argument("--presets", default="original,low,medium,high,ultra", help="--baseline: comma-separated presets")
    ap.add_argument("--frames", default="1-100", help="--baseline: the frames to compare (gt4_host --capture-frames)")
    ap.add_argument("--size", help="--baseline: headless output size WxH")
    ap.add_argument("--set", action="append", metavar="KEY=VALUE", help="--baseline: a setting for both hosts")
    ap.add_argument("--work", default=os.path.join("build", "frame_diff"), help="--baseline: where frames are written")
    ap.add_argument("--keep", action="store_true", help="--baseline: keep the frames")
    ap.add_argument("--tolerance", type=int, default=4, help="per-channel difference counted as close (default 4)")
    ap.add_argument("--min-identical", type=float, default=100.0, help="percent of identical pixels a frame needs")
    ap.add_argument("--min-within4", type=float, default=100.0, help="percent of pixels within --tolerance it needs")
    ap.add_argument("--heatmap", help="folder for error heat maps of differing frames (PNG)")
    ap.add_argument("--json", help="write every frame's metrics to this file")
    args = ap.parse_args()
    if args.baseline:
        if not args.captures:
            ap.error("--baseline needs --captures")
        ok, report = baseline(args)
    elif args.a and args.b:
        if os.path.isdir(args.a):
            report = {"frames": compare_folders(args.a, args.b, args)}
        else:
            report = {"frames": {os.path.basename(args.a): compare(args.a, args.b, args.tolerance, args.heatmap)}}
        ok = check(report["frames"], args, "")
    else:
        ap.error("give two frames or folders, or --baseline OLD_HOST NEW_HOST --captures ...")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1, default=lambda v: None if v == math.inf else v)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

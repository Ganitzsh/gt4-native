#!/usr/bin/env python3
"""PLAN.md section 2's NATIVE, step 1: the native GS renderer above 1:1, scored for 32-GS-pixel strips on the REF
captures and lap_ssr5f_boot, against the per-frame baselines in docs/modern/native_baselines/.

    py -I tools/native_strips.py --host PATH/gt4_host.exe --caps DIR (--write | --check) [--captures a,b]
                                 [--baselines DIR] [--work DIR]

Each capture in FRAME_SETS is replayed headless at NATIVE's settings (NATIVE_FLAGS: 1280 x 896, exactly 2x on both
axes, preset original with render.gs = native and render.quality = native, aspect.ratio = window, frame interpolation
and the HD pack off), its frame set captured into --work, scored by device_run.py's strip_steps (as `device_run.py
--strip-steps`), and the frames deleted. Captures missing from --caps are skipped and said so.

--write puts each capture's counts in --baselines/<capture>.txt: a header (the frame set, the settings, --label), then
a 'frame on32 on40 strips' line per frame. Counts only: they can neither rebuild nor identify the content. A frame the
rule flags (strips_rule: 6 or more steps on the 32 grid and half again as many as on the 40) is then looked at, and
listed in the file's header as judged, with the counts it was judged at and what it is ('# judged: frame N on32 X
on40 Y, what it is'; on the night captures the judgement must find it isn't the light draw whose strips 73cfad8
fixed). --write keeps a judged line whose frame's counts are unchanged, and names those it drops.
--check compares with those files and exits 1 when a frame fails, the same rule on every capture (PLAN's NATIVE): a
frame the rule flags that has no judged line, and any frame whose on32 is above its baseline by more than
max(ON32_MARGIN, ON32_SHARE of it), judged ones included (the count moves by a few between commits that touch nothing
near a strip draw: lane A, race7 at 15 and 14; a strip draw coming back on a judged frame fails on it). A commit that
moves a count past that re-baselines with its reason, as ORIG does.

Runs through a heavy slot (lap_ssr5f_boot and menus_boot: heavy-both, CAPTURES.md), started from tools/slot.py.
"""
import argparse
import datetime
import math
import os
import re
import shutil
import subprocess
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from device_run import FIXED, read_frame, strip_steps, strips_rule  # noqa: E402

NATIVE_FLAGS = ["--size", "1280x896", "--preset", "original", "--set", "render.gs=native", "--set",
                "render.quality=native", "--set", "aspect.ratio=window"] + FIXED
# capture: (frames replayed, the frames scored as --capture-frames takes them). ORIG's steps where it has them
# (race7 every frame, race_grid43 every 10th), every 50th of race_pe2_prog, every 100th elsewhere (every 10th of the
# 300-frame race_pe2_r), and lap_ssr5f_boot from its race start (about frame 7,970) to its end: the whole night lap.
FRAME_SETS = {
    "race7.cap": (100, "1-100"),
    "race_grid43.cap": (700, "10-700/10"),
    "race_pe2_prog.cap": (4900, "50-4900/50"),
    "race_boot.cap": (4900, "100-4900/100"),
    "race_grid43_r6.cap": (4400, "100-4400/100"),
    "race_pe2_r.cap": (300, "10-300/10"),
    "garage_r6.cap": (3900, "100-3900/100"),
    "menus_boot.cap": (18900, "100-18900/100"),
    "lap_ssr5f_boot.cap": (20200, "7900-20200/100"),
    "lap_ssr5r_prog.cap": (4200, "100-4200/100"),
    "lap_ssr5r_boot.cap": (20200, "9000-20200/100"),
}
ON32_MARGIN, ON32_SHARE = 3, 0.25


def allowed(base):
    return base + max(ON32_MARGIN, math.ceil(base * ON32_SHARE))


def score(host, cap_path, frames, spec, work, log):
    """{frame: (on32, on40, strips)} for one capture, the frames deleted after."""
    d = os.path.join(work, "frames")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    cmd = [host, "--headless", "--replay", cap_path, "--frames", str(frames), "--capture-frames", spec,
           "--capture-dir", d] + NATIVE_FLAGS
    with open(log, "w", encoding="utf-8", errors="replace") as out:
        code = subprocess.run(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=os.path.dirname(host)).returncode
    result = {}
    try:
        for name in os.listdir(d):
            digits = "".join(ch if ch.isdigit() else " " for ch in name).split()
            if not name.lower().endswith((".ppm", ".png")) or not digits:
                continue
            w, h, rgb = read_frame(os.path.join(d, name))
            s = strip_steps(w, h, rgb)
            result[int(digits[-1])] = (s["on32"], s["on40"], strips_rule(s))
    finally:
        shutil.rmtree(d, ignore_errors=True)   # (game frames: never kept)
    return code, result


JUDGED = re.compile(r"^# judged: frame (\d+) on32 (\d+) on40 (\d+), ")


def read_baseline(path):
    """({frame: (on32, on40, strips)}, {frame: (on32, on40, its judged line)})"""
    counts, judged = {}, {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = JUDGED.match(line)
            if m:
                judged[int(m.group(1))] = (int(m.group(2)), int(m.group(3)), line.rstrip("\n"))
            if line.startswith("#") or not line.strip():
                continue
            frame, on32, on40, strips = line.split()
            counts[int(frame)] = (int(on32), int(on40), strips == "1")
    return counts, judged


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", required=True, help="gt4_host(.exe) of the build to score")
    ap.add_argument("--caps", required=True, help="the folder holding the captures")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="write the baselines")
    mode.add_argument("--check", action="store_true", help="check against the baselines")
    ap.add_argument("--captures", default="", help="comma-separated capture names (default: all of FRAME_SETS)")
    ap.add_argument("--baselines", default=os.path.join(ROOT, "docs", "modern", "native_baselines"))
    ap.add_argument("--work", default=os.path.join(ROOT, "build", "native_strips"))
    ap.add_argument("--label", default="", help="--write: what built the host (its commit), for the header")
    a = ap.parse_args()
    names = [n if n.endswith(".cap") else n + ".cap" for n in a.captures.split(",") if n] or list(FRAME_SETS)
    unknown = [n for n in names if n not in FRAME_SETS]
    if unknown:
        ap.error(f"no frame set for {unknown}; known: {', '.join(FRAME_SETS)}")
    os.makedirs(a.work, exist_ok=True)
    if a.write:
        os.makedirs(a.baselines, exist_ok=True)
    failed = scored = 0
    for name in names:
        cap_path = os.path.join(a.caps, name)
        if not os.path.isfile(cap_path):
            print(f"{name}: not in {a.caps}, skipped", flush=True)
            continue
        frames, spec = FRAME_SETS[name]
        log = os.path.join(a.work, name[:-4] + ".log")
        code, result = score(os.path.abspath(a.host), cap_path, frames, spec, a.work, log)
        with_strips = sorted(f for f, v in result.items() if v[2])
        print(f"{name}: replay exit {code}, {len(result)} frames scored ({spec}), {len(with_strips)} with strips"
              f"{': ' + ', '.join(map(str, with_strips)) if with_strips else ''}; log {log}", flush=True)
        if code or not result:
            print(f"{name}: FAILED: the replay gave {'no frames' if not result else 'exit ' + str(code)}", flush=True)
            failed += 1
            continue
        scored += 1
        baseline = os.path.join(a.baselines, name[:-4] + ".txt")
        if a.write:
            # (a judged line stays while its frame's counts are the same: the judgement was of those)
            _, old_judged = read_baseline(baseline) if os.path.isfile(baseline) else ({}, {})
            kept = [j[2] for f, j in sorted(old_judged.items()) if f in result and result[f][:2] == j[:2]]
            dropped = [f for f, j in sorted(old_judged.items()) if not (f in result and result[f][:2] == j[:2])]
            if dropped:
                print(f"{name}: judged lines dropped (counts changed, look again): {', '.join(map(str, dropped))}",
                      flush=True)
            with open(baseline, "w", encoding="utf-8", newline="\n") as f:
                settings = textwrap.wrap(" ".join(NATIVE_FLAGS), 100, break_on_hyphens=False)
                f.write(f"# NATIVE step 1 baseline (PLAN.md section 2; tools/native_strips.py): {name}, frames {spec}\n"
                        + "".join(f"# {'settings: ' if i == 0 else '  '}{s}\n" for i, s in enumerate(settings)) +
                        f"# host: {a.label or '(not given)'}; written {datetime.date.today().isoformat()}\n"
                        + "".join(j + "\n" for j in kept) +
                        "# frame on32 on40 strips (strips: 1 when strips_rule holds)\n")
                for frame in sorted(result):
                    on32, on40, strips = result[frame]
                    f.write(f"{frame} {on32} {on40} {int(strips)}\n")
            print(f"{name}: wrote {baseline}", flush=True)
            continue
        if not os.path.isfile(baseline):
            print(f"{name}: FAILED: no baseline {baseline}", flush=True)
            failed += 1
            continue
        base, judged = read_baseline(baseline)
        problems = []
        for frame in sorted(set(base) | set(result)):
            if frame not in result or frame not in base:
                problems.append(f"frame {frame}: {'not scored' if frame not in result else 'not in the baseline'}")
                continue
            on32, on40, strips = result[frame]
            b32 = judged[frame][0] if frame in judged else base[frame][0]
            if strips and frame not in judged:
                problems.append(f"frame {frame}: flagged (on32 {on32}, on40 {on40}) and not judged: look at it, and "
                                f"list it in {baseline} if it isn't strips")
            if on32 > allowed(b32):
                problems.append(f"frame {frame}: on32 {on32} over {allowed(b32)} (baseline {b32})")
        if judged:
            print(f"{name}: judged frames: {', '.join(map(str, sorted(judged)))}", flush=True)
        for p in problems:
            print(f"{name}: {p}", flush=True)
        print(f"{name}: {'FAILED' if problems else 'ok'}", flush=True)
        failed += bool(problems)
    print(f"native_strips: {scored} captures scored, {failed} failed", flush=True)
    return 1 if failed or not scored else 0


if __name__ == "__main__":
    sys.exit(main())

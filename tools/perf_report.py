#!/usr/bin/env python3
"""Summarise gt4_host --profile-json files, and compare one with a baseline (docs/modern/PLAN.md C1).

    py tools/perf_report.py RUN.jsonl [--baseline BASE.jsonl] [--skip N] [--period 16.667] [--tolerance 1.0]
                            [--max-change PCT] [--json OUT]

Per run: frames, and for present_ms (the time between two presents: the pacing), draw_ms and gpu_ms the mean,
median, 95th and 99th percentile and maximum; frames skipped; and the presents off the display period by more
than --tolerance ms (PLAN.md E4: "presented frame times within 1 ms of 16.7 ms"). --skip leaves out the first N
frames (start-up: shaders, the first textures). With --baseline, each statistic's change in percent; the exit code
is 1 when a mean or a 95th percentile grew by more than --max-change percent (PERF(x) in PLAN.md section 2).
"""
import argparse
import json
import sys

FIELDS = ("present_ms", "draw_ms", "gpu_ms")


def load(path, skip):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows[skip:]


def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def summary(rows, period, tolerance):
    out = {"frames": len(rows), "skipped": sum(r.get("skipped", 0) for r in rows)}
    for field in FIELDS:
        v = [r[field] for r in rows if field in r and not (field == "present_ms" and r[field] == 0.0)]
        out[field] = {"mean": sum(v) / len(v) if v else 0.0, "p50": percentile(v, 50), "p95": percentile(v, 95),
                      "p99": percentile(v, 99), "max": max(v) if v else 0.0}
    presents = [r["present_ms"] for r in rows if r.get("present_ms", 0.0) > 0.0]
    off = [r["frame"] for r in rows if r.get("present_ms", 0.0) > 0.0 and abs(r["present_ms"] - period) > tolerance]
    out["off_period"] = {"count": len(off), "share": len(off) / len(presents) if presents else 0.0, "frames": off[:50]}
    return out


def show(name, s):
    print(f"{name}: {s['frames']} frames, {s['skipped']} skipped, {s['off_period']['count']} presents off the period "
          f"({100 * s['off_period']['share']:.1f} %)")
    for field in FIELDS:
        d = s[field]
        print(f"  {field:11s} mean {d['mean']:8.3f}  p50 {d['p50']:8.3f}  p95 {d['p95']:8.3f}  p99 {d['p99']:8.3f}  "
              f"max {d['max']:8.3f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run")
    ap.add_argument("--baseline")
    ap.add_argument("--skip", type=int, default=60, help="frames left out at the start (default 60)")
    ap.add_argument("--period", type=float, default=1000.0 / 60.0, help="the display period in ms (default 16.667)")
    ap.add_argument("--tolerance", type=float, default=1.0, help="ms off the period that counts (default 1)")
    ap.add_argument("--max-change", type=float, help="percent a mean or 95th percentile may grow over the baseline")
    ap.add_argument("--json", help="write the summaries to this file")
    a = ap.parse_args()
    run = summary(load(a.run, a.skip), a.period, a.tolerance)
    show(a.run, run)
    report, ok = {"run": run}, True
    if a.baseline:
        base = summary(load(a.baseline, a.skip), a.period, a.tolerance)
        show(a.baseline, base)
        report["baseline"] = base
        print("change against the baseline:")
        for field in FIELDS:
            parts = []
            for stat in ("mean", "p95", "p99", "max"):
                b, r = base[field][stat], run[field][stat]
                change = 100.0 * (r - b) / b if b else 0.0
                parts.append(f"{stat} {change:+.1f} %")
                if a.max_change is not None and stat in ("mean", "p95") and change > a.max_change:
                    ok = False
            print(f"  {field:11s} " + "  ".join(parts))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""The input-to-present chain of a run with GT4_TRACE_LATENCY=1 (PLAN.md D11), from the host's log.

    py tools/latency_report.py HOST_LOG [--json]

The host and the game log a "latency:" line a step, in QPC microseconds (one clock for both processes):
  input   an input event at its SDL time (host, platform/sdl/main.cpp)
  sent    pad 1's bytes when they change, as they go to the game (host)
  read    the game's first read of new bytes on pad 1, with its VBlank (game, runtime/link.cpp link_pad)
  present a present, with the newest game frame (VBlank) it shows (host)

Each change of the bytes sent is one sample: the input event before it (the newest since the change before; none for a
change the host makes itself, such as a key's ramp), the game's first read of those bytes, then the first present of a
game frame from the read's VBlank on. Stages, in milliseconds:
  input -> sent     the host's wait to sample the pad (today: once per game frame, after the frame arrives)
  sent -> read      until the game's next pad read (its VBlank)
  read -> present   the game's frame and the host's draw (the game's own reaction to the input, 0 or 1 ticks for GT4's
                    steering (GT4_VEHICLE_LOG), is not in it: the first frame that can show the input)
  input -> present  the sum
An input's SDL time on Windows comes from the message time, which moves in timer ticks (15.6 ms): it reads up to a
tick early, so input -> sent from it is that much too long. --inputs takes the times where the inputs were made
instead (a run's injected keys, on the same clock).
The present is the present call; the time until the picture is shown (the display's queue) is not measured here.
The game's log and the host's output share one file: a game line can land inside a host line, cutting its number.
A step is taken only from a line whose time ends it (a cut line is dropped, not misread).
"""
import argparse
import json
import re
import statistics
import sys

INPUT = re.compile(r"latency: input (\w+) (-?\d+) (\S+) at (\d+)\s*$")
SENT = re.compile(r"latency: sent ([0-9a-f]{36}) at (\d+)\s*$")
READ = re.compile(r"latency: read ([0-9a-f]{36}) VBlank (\d+) at (\d+)\s*$")
PRESENT = re.compile(r"latency: present VBlank (\d+) at (\d+)\s*$")


def percentile(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, max(0, round(p / 100 * (len(s) - 1))))]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--json", action="store_true", help="one JSON object with each stage's numbers")
    ap.add_argument("--inputs", help="the inputs' times from where they were made instead of their SDL times: "
                    "\"latency: input inject CODE down|up at US\" lines (a run's injected keys)")
    args = ap.parse_args()
    inputs, sents, reads, presents = [], [], [], []
    with open(args.log, errors="replace") as f:
        for line in f:
            if "latency:" not in line:
                continue
            if m := INPUT.search(line):
                inputs.append(float(m[4]))
            elif m := SENT.search(line):
                sents.append((float(m[2]), m[1]))
            elif m := READ.search(line):
                reads.append((float(m[3]), m[1], int(m[2])))
            elif m := PRESENT.search(line):
                presents.append((float(m[2]), int(m[1])))
    if args.inputs:
        with open(args.inputs, errors="replace") as f:
            inputs = [float(m[4]) for line in f if (m := INPUT.search(line))]
    if not sents or not reads or not presents:
        sys.exit(f"{args.log}: {len(sents)} sent, {len(reads)} read, {len(presents)} present lines: "
                 "was GT4_TRACE_LATENCY set?")
    inputs.sort()
    reads.sort()
    presents.sort()
    stages = {"input -> sent": [], "sent -> read": [], "read -> present": [], "input -> present": []}
    unread = unpresented = 0
    previous_sent = 0.0
    ii = ri = pi = 0
    for t_sent, data in sents:
        # the newest input event since the change before (an input after the previous change caused this one)
        while ii < len(inputs) and inputs[ii] <= t_sent:
            ii += 1
        t_input = inputs[ii - 1] if ii and inputs[ii - 1] > previous_sent else None
        previous_sent = t_sent
        while ri < len(reads) and reads[ri][0] < t_sent:
            ri += 1
        read = next((r for r in reads[ri:ri + 64] if r[1] == data), None)
        if read is None:
            unread += 1   # (overtaken by the next change before the game read it)
            continue
        t_read, _, vblank = read
        while pi < len(presents) and presents[pi][0] < t_read:
            pi += 1
        present = next((p for p in presents[pi:pi + 64] if p[1] >= vblank), None)
        if present is None:
            unpresented += 1
            continue
        t_present = present[0]
        stages["sent -> read"].append((t_read - t_sent) / 1000)
        stages["read -> present"].append((t_present - t_read) / 1000)
        if t_input is not None:
            stages["input -> sent"].append((t_sent - t_input) / 1000)
            stages["input -> present"].append((t_present - t_input) / 1000)
    report = {"log": args.log, "changes sent": len(sents), "not read (overtaken)": unread,
              "not presented": unpresented, "stages_ms": {}}
    for name, values in stages.items():
        if values:
            report["stages_ms"][name] = {"n": len(values), "mean": round(statistics.fmean(values), 2),
                                         "p50": round(percentile(values, 50), 2),
                                         "p95": round(percentile(values, 95), 2), "max": round(max(values), 2)}
    if args.json:
        print(json.dumps(report))
        return
    print(f"{args.log}: {len(sents)} changes sent, {unread} overtaken before a read, {unpresented} without a present")
    print(f"  {'stage':18} {'n':>5} {'mean':>8} {'p50':>8} {'p95':>8} {'max':>8}   (ms)")
    for name, s in report["stages_ms"].items():
        print(f"  {name:18} {s['n']:5d} {s['mean']:8.2f} {s['p50']:8.2f} {s['p95']:8.2f} {s['max']:8.2f}")


if __name__ == "__main__":
    main()

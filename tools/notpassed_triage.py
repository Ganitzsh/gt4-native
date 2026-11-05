#!/usr/bin/env python3
"""Triage of abi_lint's "arg N not passed, read by the callee" rows against the original code.

    py tools/notpassed_triage.py --decomp C:\\path\\to\\gt4-decomp [ABI_LINT_OUTPUT]

abi_lint compares source with source: a callee whose native definition names a parameter the caller does not
pass reads a stack slot natively. Whether that matters depends on the original code, which this checks for
each row (the EE register of the missing parameter: integers a0-t3 and floats f12-f19 in their own orders):
  - artefact: the callee's original code never reads that register before writing it (passthrough_lint's
    argument_reads, through its direct callees): a draft or m2c parameter the function does not use;
  - leftover: the callee reads it, but at the call the caller's register holds what an earlier call left
    there (or what the caller set for another call): undefined on the EE as well;
  - REAL (caller sets it): the caller writes the register for this call; the source dropped an argument;
  - REAL (caller forwards it): the caller never writes it before the call, so the callee gets the caller's
    own incoming argument; natively the caller must pass it on;
  - stack: the parameter lies past t3 / f19 (on the caller's stack): not decided here.
The call sites are the caller's `jal callee` (every one is checked; the worst verdict counts).
"""
import argparse
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import passthrough_lint as pt  # noqa: E402

NAMES = {4: "a0", 5: "a1", 6: "a2", 7: "a3", 8: "t0", 9: "t1", 10: "t2", 11: "t3"}


def register_of(kinds, n):
    """The EE register of parameter n (1-based) given the definition's kinds: ('g', 4..11) or ('f', 12..19)."""
    gi, fi = 4, 12
    for i, k in enumerate(kinds):
        if i == n - 1:
            return ("f", fi) if k == "f" else ("g", gi)
        if k == "f":
            fi += 1
        elif k == "q" or k == "w" or k in "i?":
            gi += 1
    return None


def site_state(segs, start, site, reg, is_f):
    """What the caller's register holds at the call at `site`: 'set' (written by the caller for this call),
    'forward' (never written since entry, no call before), 'leftover' (last written by a call, or set before an
    earlier call)."""
    last_write, last_call = None, None
    for a in range(start, site + 8, 4):   # the delay slot counts as before the call
        w = pt.word_at(segs, a)
        if w is None:
            break
        if a == site + 4:
            g, f = pt.writes(w)
            if reg in (f if is_f else g):
                last_write = a
            break
        if a == site:
            continue
        op, funct = w >> 26, w & 63
        if op == 3 or (op == 0 and funct == 0x09):
            last_call = a
            continue
        g, f = pt.writes(w)
        if reg in (f if is_f else g):
            last_write = a
    if last_write is None:
        return "forward" if last_call is None else "leftover"
    if last_call is not None and last_call > last_write:
        return "leftover"
    # written after the last call, or there was no call: did an earlier call come between? (write then call
    # then this call -> the write was for that earlier call, clobbered since)
    return "set"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("lint", nargs="?", default=os.path.join(ROOT, "build", "abi_lint.txt"))
    a = ap.parse_args()
    segs = pt.load_text(os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))
    with open(os.path.join(a.decomp, "build", "functions.csv"), newline="") as f:
        sizes = {int(r["address"], 16): int(r.get("max_size") or r.get("size") or 0) for r in csv.DictReader(f)}
    by_name = {}
    for line in open(os.path.join(a.decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", line)
        if m and "type:func" in line:
            by_name.setdefault(m.group(1), int(m.group(2), 16))
    memo = {}
    counts = {}
    out = []
    for line in open(a.lint, encoding="utf-8"):
        if "not passed" not in line:
            continue
        parts = line.split()
        callee = int(parts[0], 16)
        kinds = re.match(r"def\((.*)\)", parts[1]).group(1)
        path = parts[2].rsplit(":", 1)[0]
        base = os.path.splitext(os.path.basename(path))[0]
        m = re.search(r"func_([0-9A-Fa-f]{8})|^([0-9A-Fa-f]{8})_[0-9a-f]+$", base)   # a draft: ADDR_hash
        caller = int(m.group(1) or m.group(2), 16) if m else by_name.get(base)
        verdicts = []
        for n in map(int, re.findall(r"arg (\d+) \(\w\) not passed", line)):
            reg = register_of(kinds, n)
            if reg is None or (reg[0] == "g" and reg[1] > 11) or (reg[0] == "f" and reg[1] > 19):
                verdicts.append(f"arg {n}: stack")
                continue
            is_f, r = reg[0] == "f", reg[1]
            rname = f"f{r}" if is_f else NAMES[r]
            gr, fr = pt.argument_reads(segs, callee, sizes.get(callee, 0), sizes, memo)
            if r not in (fr if is_f else gr):
                verdicts.append(f"arg {n} ({rname}): artefact")
                continue
            if caller is None or caller not in sizes:
                verdicts.append(f"arg {n} ({rname}): callee reads it; caller unknown")
                continue
            target_bits = (callee >> 2) & 0x03FFFFFF
            sites = [s for s in range(caller, caller + sizes[caller], 4)
                     if (pt.word_at(segs, s) or 0) == (0x0C000000 | target_bits)]
            if not sites:
                verdicts.append(f"arg {n} ({rname}): callee reads it; no direct call in the caller")
                continue
            states = {site_state(segs, caller, s, r, is_f) for s in sites}
            if "set" in states:
                verdicts.append(f"arg {n} ({rname}): REAL (caller sets it)")
            elif "forward" in states:
                verdicts.append(f"arg {n} ({rname}): REAL (caller forwards its own)")
            else:
                verdicts.append(f"arg {n} ({rname}): leftover")
        worst = ("REAL" if any("REAL" in v for v in verdicts) else "unknown" if any("unknown" in v or "no direct" in v
                 or "stack" in v for v in verdicts) else "leftover" if any("leftover" in v for v in verdicts) else "artefact")
        counts[worst] = counts.get(worst, 0) + 1
        out.append((worst, "%08X" % callee, "%08X" % caller if caller else "?", path, "; ".join(verdicts)))
    order = {"REAL": 0, "unknown": 1, "leftover": 2, "artefact": 3}
    for worst, callee, caller, path, why in sorted(out, key=lambda x: (order[x[0]], x[1])):
        print(f"{worst:8} {callee} <- {caller}  {path}  :  {why}")
    print(" ".join(f"{k} {v}" for k, v in sorted(counts.items(), key=lambda kv: order[kv[0]])), file=sys.stderr)


if __name__ == "__main__":
    main()

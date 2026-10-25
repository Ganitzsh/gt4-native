#!/usr/bin/env python3
"""Which functions without native code can the game reach, and how soon.

    py tools/missing_reach.py --decomp C:\\path\\to\\gt4-decomp [--game build/game] [--out FILE]

Walks the original executable's code from the game's entry points (main 00107F08, the static
constructor list at 00616F24, and the threads and handlers the boot starts) along direct calls
(jal/j) and function addresses built in registers (lui + addiu/ori: callbacks, thread entries),
and lists the reachable functions that have no native implementation in the last link
(build/game/gen/native_functions.c), nearest first (breadth-first depth). Calls through vtables
and data tables are not followed, so the list is a lower bound.
Output: one line per function, `ADDR depth size caller`, to stdout or --out (the format
`swarm.py assign --only` reads: the address first).
"""
import argparse
import collections
import csv
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENTRIES = [0x00107F08, 0x001016E8, 0x005BC4C8]


def load_text(elf_path):
    d = open(elf_path, "rb").read()
    phoff, = struct.unpack_from("<I", d, 0x1C)
    phnum, = struct.unpack_from("<H", d, 0x2C)
    segs = []
    for i in range(phnum):
        t, off, va, _, fs, ms, _, _ = struct.unpack_from("<8I", d, phoff + i * 32)
        if t == 1:
            segs.append((va, d[off:off + fs]))
    return segs


def word_at(segs, a):
    for va, data in segs:
        if va <= a < va + len(data) - 3:
            return struct.unpack_from("<I", data, a - va)[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--game", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--out", default="")
    a = ap.parse_args()
    segs = load_text(os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))
    fns = {}
    with open(os.path.join(a.decomp, "build", "functions.csv"), newline="") as f:
        for row in csv.DictReader(f):
            fns[int(row["address"], 16)] = int(row["max_size"])
    natives = {int(m, 16) for m in re.findall(r"\{0x([0-9A-F]{8}),&gt4_n", open(
        os.path.join(a.game, "gen", "native_functions.c")).read())}
    starts = sorted(fns)
    # the constructor list: count word then pointers, NULL-terminated
    p = 0x00616F24 + 4
    while True:
        w = word_at(segs, p)
        if not w:
            break
        if w in fns:
            ENTRIES.append(w)
        p += 4
    depth, parent = {}, {}
    q = collections.deque()
    for e in ENTRIES:
        depth[e] = 0
        q.append(e)
    while q:
        fn = q.popleft()
        size = fns.get(fn, 0)
        hi = {}
        for off in range(0, size, 4):
            pc = fn + off
            w = word_at(segs, pc)
            if w is None:
                break
            op = w >> 26
            targets = []
            if op in (2, 3):  # j / jal
                targets.append(((w & 0x3FFFFFF) << 2) | (pc & 0xF0000000))
            elif op == 15:  # lui rt, imm
                hi[(w >> 16) & 31] = (w & 0xFFFF) << 16
            elif op in (9, 13):  # addiu / ori rt, rs, imm
                rs, imm = (w >> 21) & 31, w & 0xFFFF
                if rs in hi:
                    v = hi[rs] + (imm - 0x10000 if op == 9 and imm & 0x8000 else imm)
                    targets.append(v & 0xFFFFFFFF)
            for t in targets:
                if t in fns and t not in depth:
                    depth[t] = depth[fn] + 1
                    parent[t] = fn
                    q.append(t)
    def runtime_handled(t):
        # syscall stubs (the kernel points them at its implementations) and the constructor list
        w0, w1 = word_at(segs, t), word_at(segs, t + 4)
        return t == 0x00616F24 or (w1 == 0x0000000C and w0 is not None and (w0 >> 16) == 0x2403)

    missing = sorted((d, t) for t, d in depth.items() if t not in natives and not runtime_handled(t))
    lines = [f"{t:08x} {d} {fns[t]} {parent.get(t, 0):08x}" for d, t in missing]
    text = "\n".join(lines) + "\n"
    if a.out:
        open(a.out, "w").write(text)
    else:
        print(text, end="")
    print(f"# reachable: {len(depth)} functions; without native code: {len(missing)} "
          f"({sum(fns[t] for _, t in missing)} bytes)", file=__import__('sys').stderr)


if __name__ == "__main__":
    main()

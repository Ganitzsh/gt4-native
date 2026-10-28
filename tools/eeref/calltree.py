#!/usr/bin/env python3
"""The static call tree of a game function, for GT4_RECORD lists (docs/DIFFERENTIAL_TESTING.md).

    calltree.py --elf CORE.GT4.elf --functions gt4-decomp/build/functions.csv ADDR [--depth N] [--max M] [--min-size B]

Prints the PS2 addresses of the functions ADDR reaches through `jal` and tail calls (not through pointers), breadth first
to depth N (default 3), comma-separated without ADDR itself: GT4_RECORD_ROOT=ADDR GT4_RECORD=<this>. Functions
smaller than B bytes (default 0) are walked but not listed: accessors are rarely worth a recording.
"""
import argparse
import bisect
import csv
import struct


def load_text(path):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', d, 0x20)
    shentsize, shnum = struct.unpack_from('<HH', d, 0x2E)
    for k in range(shnum):
        _, kind, flags, addr, off, size = struct.unpack_from('<IIIIII', d, shoff + k * shentsize)
        if kind == 1 and flags & 4:
            return addr, d[off:off + size]
    raise SystemExit('no code section in ' + path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--functions', required=True, help="gt4-decomp's build/functions.csv")
    ap.add_argument('addr')
    ap.add_argument('--depth', type=int, default=3)
    ap.add_argument('--max', type=int, default=250)
    ap.add_argument('--min-size', type=lambda v: int(v, 0), default=0)
    a = ap.parse_args()
    base, text = load_text(a.elf)
    starts, sizes = [], {}
    with open(a.functions, newline='') as f:
        for row in csv.DictReader(f):
            s = int(row['address'], 16)
            starts.append(s)
            sizes[s] = int(row['max_size'])
    starts.sort()

    def callees(fn):
        out = []
        for pc in range(fn, fn + sizes.get(fn, 0), 4):
            word, = struct.unpack_from('<I', text, pc - base)
            if word >> 26 in (2, 3):  # jal, or j to a function's start (a tail call)
                target = (pc & 0xF0000000) | (word & 0x03FFFFFF) << 2
                i = bisect.bisect_right(starts, target) - 1
                if i >= 0 and starts[i] == target and target not in out:
                    out.append(target)
        return out

    root = int(a.addr, 16)
    seen, level, order = {root}, [root], []
    for _ in range(a.depth):
        nxt = []
        for fn in level:
            for c in callees(fn):
                if c not in seen:
                    seen.add(c)
                    nxt.append(c)
                    order.append(c)
        level = nxt
    order = [c for c in order if sizes.get(c, 0) >= a.min_size]
    print(','.join('%08X' % c for c in order[:a.max]))


if __name__ == '__main__':
    main()

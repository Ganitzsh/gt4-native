#!/usr/bin/env python3
"""Virtual functions whose definition leaves out the object, which the EE tolerates and the native build does not.

    py tools/vtable_this_lint.py --decomp C:\\path\\to\\gt4-decomp [--game build/game] [--all]

A virtual call passes the object first: on the EE in $a0, natively as the first stack argument. A source matched as
`void f(float x, float y)` reads x from $f12 on the EE, whatever $a0 holds; natively it reads the object's address as
x and every later argument one place off (the motion target's setters, 0042C870..0042C928: the skinned people's
bones). So does a definition whose first parameter is 8 or 16 bytes (a 64-bit or 128-bit integer, a double).

The vtables are found in the ELF's data as gcc 2 lays them out: entries of 8 bytes, a 16-bit delta and a 16-bit
index, then a function start (the decomp's build/functions.csv); runs of at least `--min-run` entries. Each slot
function's definition is the one the native link uses (game_src/, else the matched source, else the draft named in
build/game/draft_natives.txt), parsed by abi_lint. Reported: a first parameter that is a float, 64-bit or 128-bit,
or a structure by value (`?`, unknown: listed with --all). A function also called directly (a plain function that is
in a vtable too) needs a look at its direct callers before a fix.
"""
import argparse
import collections
import csv
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import abi_lint  # noqa: E402


# Tables that look like vtables and are not: the soft-float library's and libm's function tables (called with plain
# arguments, no object).
KNOWN = {
    "0057F490": "soft-float table 00655D98",
    "0057F8B8": "soft-float table 0068A504 (dpmul)",
    "0057FB50": "soft-float table 00655D98 (fptodp)",
    "0059BEE8": "libm table 0068AB3C (atan)",
    "005BC350": "libgcc table 0068A560 (double -> unsigned)",
    "005BC3B8": "libgcc table 0068A560 (float -> unsigned)",
}


def segments(elf):
    phoff = struct.unpack_from("<I", elf, 0x1C)[0]
    count = struct.unpack_from("<H", elf, 0x2C)[0]
    out = []
    for i in range(count):
        kind, off, va, _pa, size, _msize, flags = struct.unpack_from("<7I", elf, phoff + 32 * i)
        if kind == 1:
            out.append((va, off, size, flags))
    return out


def find_vtables(elf, starts, min_run):
    """{vtable address: [(slot, function)]} from runs of gcc 2 vtable entries (GT4 has one loadable segment)."""
    found = {}
    for va, off, size, flags in segments(elf):
        data = elf[off:off + size]
        i = 0
        while i + 8 <= len(data):
            run = []
            j = i
            while j + 8 <= len(data):
                w0, fn = struct.unpack_from("<II", data, j)
                delta, index = w0 & 0xFFFF, w0 >> 16
                if fn not in starts or not (delta < 0x1000 or delta > 0xF000) or index > 0x1000:
                    break
                run.append(fn)
                j += 8
            if len(run) >= min_run:
                found[va + i] = list(enumerate(run))
                i = j
            else:
                i += 4
    return found


def definitions(decomp, game, game_src=True):
    """{ADDR: (path, role)}: the definition the native link compiles for each address."""
    out = {}
    drafts = os.path.join(game, "draft_natives.txt")
    if os.path.exists(drafts):
        for line in open(drafts, encoding="utf-8", errors="replace"):
            parts = line.split()
            if len(parts) >= 2 and not line.startswith("#"):
                out[parts[0].upper()] = (os.path.join(decomp, parts[1]), "draft")
    files = subprocess.run(["git", "-C", decomp, "ls-files", "src"], capture_output=True, text=True).stdout.split()
    for f in files:
        base = os.path.basename(f)
        name = os.path.splitext(base)[0]
        out[name] = (os.path.join(decomp, f), "src")   # by name too (named functions)
        if base.startswith("func_"):
            out[name[5:13].upper()] = (os.path.join(decomp, f), "src")
    for f in os.listdir(os.path.join(ROOT, "game_src")) if game_src else []:
        if f.startswith("func_") and f.endswith((".c", ".cpp")):
            out[f[5:13].upper()] = (os.path.join(ROOT, "game_src", f), "game_src")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--game", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--elf", default="")
    ap.add_argument("--min-run", type=int, default=3)
    ap.add_argument("--without-game-src", action="store_true", help="judge the matched sources and drafts alone (what game_src/ corrects shows too)")
    ap.add_argument("--all", action="store_true", help="also list first parameters of unknown type (structures by value)")
    a = ap.parse_args()

    elf_path = a.elf or os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf")
    elf = open(elf_path, "rb").read()
    starts = set()
    with open(os.path.join(a.decomp, "build", "functions.csv")) as f:
        for row in list(csv.reader(f))[1:]:
            starts.add(int(row[0], 16))
    names = {}
    for line in open(os.path.join(a.decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace"):
        parts = line.split("=")
        if len(parts) >= 2 and "type:func" in line:
            try:
                addr = "%08X" % int(parts[1].split(";")[0].strip(), 16)
            except ValueError:
                continue
            abi_lint.FUNC_NAMES[parts[0].strip()] = addr
            names.setdefault(addr, parts[0].strip())

    vtables = find_vtables(elf, starts, a.min_run)
    slots = collections.defaultdict(list)   # function -> [(vtable, slot)]
    for vt, entries in vtables.items():
        for slot, fn in entries:
            slots["%08X" % fn].append((vt, slot))

    defs = definitions(a.decomp, a.game, not a.without_game_src)
    rows = []
    for addr in sorted(slots):
        d = defs.get(addr) or defs.get(names.get(addr, ""))
        if not d or addr in KNOWN:
            continue
        path, role = d
        try:
            text = abi_lint.strip_comments(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            continue
        kinds = None
        for found in abi_lint.scan(path, text):
            if found[0] == addr and found[2] and found[1] is not None:
                kinds = found[1]
                break
        if not kinds:
            continue
        first = kinds[0]
        if first in ("f", "w", "q") or (a.all and first == "?"):
            rows.append((addr, role, os.path.relpath(path, a.decomp if role != "game_src" else ROOT), "".join(kinds),
                         slots[addr]))
    for addr, role, path, kinds, where in rows:
        vts = ", ".join("%08X[%d]" % (vt, s) for vt, s in where[:4]) + (" ..." if len(where) > 4 else "")
        print("%s  %-8s %-6s %-60s in %s" % (addr, role, kinds, path.replace("\\", "/"), vts))
    print("%d vtables, %d slot functions, %d reported" % (len(vtables), len(slots), len(rows)), file=sys.stderr)


if __name__ == "__main__":
    main()

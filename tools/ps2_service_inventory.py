#!/usr/bin/env python3
"""Inventory the PS2 services the game code calls: the shim surface the native host must provide.

Reads a gt4-decomp checkout (its config/sdk_functions.tsv names the Sony SDK functions linked into the
executable, build/functions.csv bounds every function) and the user's own executable (orig/SCUS-97328/CORE.GT4.elf),
finds every `jal` from game code into an SDK function, and groups the targets by library member and by
host service. Calls between SDK functions are internal to the libraries and are not counted.

    python3 tools/ps2_service_inventory.py --decomp ../gt4-decomp [--out docs/PS2_SERVICES.md]

The report holds SDK function names and counts only: no game bytes or addresses of game data.
"""
import argparse
import bisect
import collections
import csv
import os
import struct

# Library member -> host service (docs/PLATFORM.md). Members not listed fall under "other".
SERVICES = [
    ("C library (host libc, no shim)", ("memset", "memcpy", "memmove", "memchr", "strcpy", "strncpy", "strcat",
                                        "strncat", "strchr", "setjmp", "stdm_mlc", "converterr")),
    ("Input (pad)", ("libpad", "libpad2", "libvib")),
    ("Storage: disc", ("cdvd", "libcdvd", "filestub", "eeloadfile")),
    ("Storage: memory card", ("libmc", "libmc2")),
    ("Audio (IOP sound)", ("libsd", "sdr", "libsdr", "libspu", "libsnd", "liberx")),
    ("Video (GS, DMA, VU)", ("libgraph", "graph", "libdma", "libvu0", "libvif", "libgif", "libpkt", "libdev")),
    ("IOP services (SIF RPC, modules)", ("sifrpc", "sifcmd", "msifrpc", "loadcore", "modload", "iopheap",
                                         "iopreset", "iopnotify", "loadelf", "sysmem", "libscf", "devman")),
    ("Clock, threads, kernel", ("thread", "timer", "timeralarm", "tlbfunc", "initsys", "exit", "glue",
                                "libkernl", "kernl", "alarm", "intr", "sema", "deci2", "kprintf", "tty", "diei",
                                "cache", "r_perf")),
    ("Network (later)", ("libinet", "inet", "ntgui", "libnet", "netcnf", "udnetcnf", "dnas", "libdbc", "libntp",
                         "res_comp", "md5c", "convauth")),
]


def service_of(member):
    stem = member.rsplit(".", 1)[0].lower()
    for name, prefixes in SERVICES:
        if any(stem == p or stem.startswith(p) for p in prefixes):
            return name
    return "Other"


def text_segment(path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    for i in range(phnum):
        ptype, off, vaddr, _, filesz, _, flags, _ = struct.unpack_from("<8I", data, phoff + i * phentsize)
        if ptype == 1 and flags & 1:  # PT_LOAD, executable
            return vaddr, data[off:off + filesz]
    raise SystemExit("no executable PT_LOAD segment")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--elf", help="default: <decomp>/orig/SCUS-97328/CORE.GT4.elf")
    ap.add_argument("--out", default="docs/PS2_SERVICES.md")
    a = ap.parse_args()
    sdk = {}
    for row in open(os.path.join(a.decomp, "config", "sdk_functions.tsv")):
        if row.startswith("#") or not row.strip():
            continue
        addr, name, member = row.rstrip("\n").split("\t")[:3]
        if name != ".text":
            sdk[int(addr, 16)] = (name, member)
    starts = sorted(int(r["address"], 16) for r in csv.DictReader(open(os.path.join(a.decomp, "build", "functions.csv"))))
    base, text = text_segment(a.elf or os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))

    calls = collections.Counter()      # sdk addr -> game call sites
    callers = collections.defaultdict(set)
    for off in range(0, len(text) - 3, 4):
        word, = struct.unpack_from("<I", text, off)
        if word >> 26 != 3:  # jal
            continue
        pc = base + off
        target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        if target not in sdk:
            continue
        i = bisect.bisect_right(starts, pc) - 1
        caller = starts[i] if i >= 0 else pc
        if caller in sdk:
            continue  # inside the SDK libraries
        calls[target] += 1
        callers[target].add(caller)

    # Hardware registers written directly by game code: a lui of an EE register block's upper half.
    # The game drives DMA, VIF and the GS this way (not through libgraph), so R1 starts from these.
    blocks = {0x1000: "EE timers, IPU, GIF, VIF0/1, DMA channels (0x1000xxxx)", 0x1001: "DMA control, interrupts (0x1001xxxx)",
              0x1100: "VU0/VU1 micro and data memory (0x1100xxxx)", 0x1200: "GS privileged registers (0x1200xxxx)",
              0x7000: "scratchpad (0x7000xxxx)"}
    hw = collections.defaultdict(set)
    for off in range(0, len(text) - 3, 4):
        word, = struct.unpack_from("<I", text, off)
        if word >> 26 == 0x0F and (word & 0xFFFF) in blocks:  # lui
            pc = base + off
            i = bisect.bisect_right(starts, pc) - 1
            caller = starts[i] if i >= 0 else pc
            if caller not in sdk:
                hw[word & 0xFFFF].add(caller)

    by_service = collections.defaultdict(list)
    for addr, (name, member) in sdk.items():
        if calls[addr]:
            by_service[service_of(member)].append((name, member, calls[addr], len(callers[addr])))
    order = [s for s, _ in SERVICES] + ["Other"]
    lines = ["# PS2 services the game calls", "",
             "Generated by `tools/ps2_service_inventory.py` from a gt4-decomp checkout and the user's own executable:",
             "every `jal` from game code into a Sony SDK function linked in the executable, grouped by the host service",
             "that will replace it (docs/PLATFORM.md). Calls between SDK functions are internal and not counted. Kernel",
             "syscalls made directly (not through an SDK wrapper) and IOP-side modules are not in this table.", "",
             "| Host service | SDK functions called | Call sites | Calling functions |", "|---|---|---|---|"]
    for s in order:
        rows = by_service.get(s, [])
        if rows:
            lines.append(f"| {s} | {len(rows)} | {sum(r[2] for r in rows)} | {len(set().union(*(callers[k] for k, v in sdk.items() if v[0] in {r[0] for r in rows})))} |")
    for s in order:
        rows = sorted(by_service.get(s, []), key=lambda r: (-r[2], r[0]))
        if not rows:
            continue
        lines += ["", f"## {s}", "", "| SDK function | Library member | Call sites | Calling functions |", "|---|---|---|---|"]
        lines += [f"| `{n}` | {m} | {c} | {k} |" for n, m, c, k in rows]
    lines += ["", "## Hardware reached directly", "",
              "Game functions (outside the SDK) that load the upper half of an EE hardware block's address. The game",
              "programs DMA, VIF and the GS itself; these are the entry points of the R1 renderer boundary.", "",
              "| Block | Game functions |", "|---|---|"]
    lines += [f"| {blocks[b]} | {len(hw[b])} |" for b in sorted(blocks) if hw[b]]
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    open(a.out, "w", newline="\n").write("\n".join(lines) + "\n")
    print(f"{sum(len(v) for v in by_service.values())} SDK functions called from game code; wrote {a.out}")


if __name__ == "__main__":
    main()

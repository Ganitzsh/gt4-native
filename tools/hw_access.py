#!/usr/bin/env python3
"""Which EE hardware registers the game code reaches, and from which functions: the R1/P1 boundary.

For every game function (outside the linked Sony SDK), a linear pass follows `lui` into a register,
`addiu`/`ori` on it, and loads/stores through it, and records each address in an EE hardware block
(DMA channels and control, VIF0/VIF1/GIF registers and FIFOs, timers, interrupt control, the GS
privileged registers). Registers are named after the public PS2 hardware documentation; an address with
no name is shown as its block and offset. Branches are not followed, so the result is a lower bound.

    python3 tools/hw_access.py --decomp ../gt4-decomp [--out docs/HW_ACCESS.md]

The report holds register names, function addresses and the decomp's source paths: no game bytes.
"""
import argparse
import bisect
import collections
import csv
import os
import re
import struct
import subprocess

DMA_CHANNELS = {0x10008000: "VIF0", 0x10009000: "VIF1", 0x1000A000: "GIF", 0x1000B000: "fromIPU",
                0x1000B400: "toIPU", 0x1000C000: "SIF0", 0x1000C400: "SIF1", 0x1000C800: "SIF2",
                0x1000D000: "fromSPR", 0x1000D400: "toSPR"}
DMA_REGS = {0x00: "CHCR", 0x10: "MADR", 0x20: "QWC", 0x30: "TADR", 0x40: "ASR0", 0x50: "ASR1", 0x80: "SADR"}
NAMED = {
    0x10000000: "T0_COUNT", 0x10000010: "T0_MODE", 0x10000020: "T0_COMP", 0x10000030: "T0_HOLD",
    0x10000800: "T1_COUNT", 0x10000810: "T1_MODE", 0x10000820: "T1_COMP", 0x10000830: "T1_HOLD",
    0x10001000: "T2_COUNT", 0x10001010: "T2_MODE", 0x10001020: "T2_COMP",
    0x10001800: "T3_COUNT", 0x10001810: "T3_MODE", 0x10001820: "T3_COMP",
    0x10002000: "IPU_CMD", 0x10002010: "IPU_CTRL", 0x10002020: "IPU_BP", 0x10002030: "IPU_TOP",
    0x10003000: "GIF_CTRL", 0x10003010: "GIF_MODE", 0x10003020: "GIF_STAT",
    0x10003800: "VIF0_STAT", 0x10003810: "VIF0_FBRST", 0x10003820: "VIF0_ERR", 0x10003830: "VIF0_MARK",
    0x10003C00: "VIF1_STAT", 0x10003C10: "VIF1_FBRST", 0x10003C20: "VIF1_ERR", 0x10003C30: "VIF1_MARK",
    0x10004000: "VIF0_FIFO", 0x10005000: "VIF1_FIFO", 0x10006000: "GIF_FIFO", 0x10007000: "IPU_out_FIFO",
    0x10007010: "IPU_in_FIFO",
    0x1000E000: "D_CTRL", 0x1000E010: "D_STAT", 0x1000E020: "D_PCR", 0x1000E030: "D_SQWC", 0x1000E040: "D_RBSR",
    0x1000E050: "D_RBOR", 0x1000E060: "D_STADR", 0x1000F000: "I_STAT", 0x1000F010: "I_MASK",
    0x1000F520: "D_ENABLER", 0x1000F590: "D_ENABLEW",
    0x12000000: "GS PMODE", 0x12000010: "GS SMODE1", 0x12000020: "GS SMODE2", 0x12000030: "GS SRFSH",
    0x12000040: "GS SYNCH1", 0x12000050: "GS SYNCH2", 0x12000060: "GS SYNCV", 0x12000070: "GS DISPFB1",
    0x12000080: "GS DISPLAY1", 0x12000090: "GS DISPFB2", 0x120000A0: "GS DISPLAY2", 0x120000B0: "GS EXTBUF",
    0x120000C0: "GS EXTDATA", 0x120000D0: "GS EXTWRITE", 0x120000E0: "GS BGCOLOR", 0x12001000: "GS CSR",
    0x12001010: "GS IMR", 0x12001040: "GS BUSDIR", 0x12001080: "GS SIGLBLID",
}
GROUPS = [("DMA channels and control", lambda a: 0x10008000 <= a < 0x1000F000 or 0x1000F500 <= a < 0x1000F600),
          ("VIF / GIF / IPU registers and FIFOs", lambda a: 0x10002000 <= a < 0x10008000),
          ("GS privileged registers", lambda a: 0x12000000 <= a < 0x12002000),
          ("Timers and interrupts", lambda a: 0x10000000 <= a < 0x10002000 or 0x1000F000 <= a < 0x1000F100),
          ("VU0 / VU1 memory", lambda a: 0x11000000 <= a < 0x11010000)]


def reg_name(addr):
    if addr in NAMED:
        return NAMED[addr]
    for base, ch in DMA_CHANNELS.items():
        if base <= addr < base + 0x400 and (addr - base) in DMA_REGS:
            return f"D_{ch} {DMA_REGS[addr - base]}"
    if 0x11000000 <= addr < 0x11010000:
        part = {0x11000000: "VU0 micro", 0x11004000: "VU0 data", 0x11008000: "VU1 micro", 0x1100C000: "VU1 data"}
        b = max(k for k in part if k <= addr)
        return f"{part[b]} memory"
    return f"0x{addr:08X}"


def text_segment(path):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    for i in range(phnum):
        ptype, off, vaddr, _, filesz, _, flags, _ = struct.unpack_from("<8I", data, phoff + i * phentsize)
        if ptype == 1 and flags & 1:
            return vaddr, data[off:off + filesz]
    raise SystemExit("no executable PT_LOAD segment")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--elf", help="default: <decomp>/orig/SCUS-97328/CORE.GT4.elf")
    ap.add_argument("--out", default="docs/HW_ACCESS.md")
    a = ap.parse_args()
    sdk = set()
    for row in open(os.path.join(a.decomp, "config", "sdk_functions.tsv")):
        if not row.startswith("#") and row.strip():
            sdk.add(int(row.split("\t")[0], 16))
    starts = sorted(int(r["address"], 16) for r in csv.DictReader(open(os.path.join(a.decomp, "build", "functions.csv"))))
    base, text = text_segment(a.elf or os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))
    files = subprocess.run(["git", "-C", a.decomp, "ls-files", "src"], capture_output=True, text=True).stdout.split()
    src = {}
    for f in files:
        m = re.match(r"func_([0-9A-Fa-f]{8})", os.path.basename(f))
        if m:
            src[int(m.group(1), 16)] = f

    sext = lambda v: v - 0x10000 if v & 0x8000 else v
    access = collections.defaultdict(lambda: collections.defaultdict(set))  # reg -> func -> {"r","w"}
    for i, start in enumerate(starts):
        if start in sdk:
            continue
        end = starts[i + 1] if i + 1 < len(starts) else base + len(text)
        regs = {}
        for pc in range(start, min(end, base + len(text)), 4):
            w, = struct.unpack_from("<I", text, pc - base)
            op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            if op == 0x0F:  # lui
                regs[rt] = imm << 16
            elif op in (0x09, 0x19) and rs in regs:  # addiu / daddiu
                regs[rt] = (regs[rs] + sext(imm)) & 0xFFFFFFFF
            elif op == 0x0D and rs in regs:  # ori
                regs[rt] = regs[rs] | imm
            elif op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x1E, 0x28, 0x29, 0x2B, 0x3F, 0x1F) and rs in regs:
                addr = (regs[rs] + sext(imm)) & 0xFFFFFFFF
                if any(g(addr) for _, g in GROUPS):
                    kind = "w" if op in (0x28, 0x29, 0x2B, 0x3F, 0x1F) else "r"
                    access[addr][start].add(kind)
                if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37, 0x1E):
                    regs.pop(rt, None)
            elif op == 0 and (w & 0x3F) in (0x21, 0x2D, 0x25):  # addu/daddu/or: a plain copy keeps the value
                rd = (w >> 11) & 31
                if rt == 0 and rs in regs:
                    regs[rd] = regs[rs]
                elif rs == 0 and rt in regs:
                    regs[rd] = regs[rt]
                else:
                    regs.pop(rd, None)
            elif op not in (0x2B, 0x3F, 0x28, 0x29, 0x1F, 0x02, 0x03, 0x04, 0x05, 0x14, 0x15) and op != 0:
                regs.pop(rt, None)

    lines = ["# EE hardware reached by game code", "",
             "Generated by `tools/hw_access.py` from a gt4-decomp checkout and the user's own executable: every load",
             "(r) or store (w) of an EE hardware register in a game function outside the linked Sony SDK, found by a",
             "linear pass (a lower bound: branches are not followed). These are the points where the native host",
             "replaces the hardware: DMA/VIF/GIF and the GS for the renderer (R1), timers and interrupts for P1.", ""]
    for title, g in GROUPS:
        regs = sorted(r for r in access if g(r))
        if not regs:
            continue
        funcs = set().union(*(access[r].keys() for r in regs))
        lines += [f"## {title}", "", f"{len(regs)} registers, {len(funcs)} functions.", "",
                  "| Register | Address | Functions (r/w) |", "|---|---|---|"]
        for r in regs:
            fs = ", ".join(f"`{f:08X}`{''.join(sorted(k))}" for f, k in sorted(access[r].items()))
            lines.append(f"| {reg_name(r)} | `{r:08X}` | {fs} |")
        lines.append("")
    all_funcs = sorted(set().union(*(set(v) for v in access.values())))
    lines += ["## Functions", "", "| Function | Source | Registers |", "|---|---|---|"]
    for f in all_funcs:
        rs = sorted({reg_name(r) for r in access if f in access[r]})
        lines.append(f"| `{f:08X}` | {src.get(f, 'no source yet')} | {', '.join(rs)} |")
    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    open(a.out, "w", newline="\n").write("\n".join(lines) + "\n")
    print(f"{len(access)} registers, {len(all_funcs)} functions; wrote {a.out}")


if __name__ == "__main__":
    main()

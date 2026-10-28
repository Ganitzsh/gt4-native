#!/usr/bin/env python3
"""Arguments the EE forwards to an indirect call without the source passing them.

    py tools/passthrough_lint.py --decomp C:\\path\\to\\gt4-decomp [--all]

On the EE an argument register a function never writes still holds the value its caller put there, so a
function that calls through a pointer (jalr, or a jr tail call) forwards its own incoming arguments to the
callee whether its source names them or not. CompatibleModel::virtual_04(this, flag) called
this->callback(this->context) and the callback read flag from $a1: natively it read a stale stack slot (the
dealers' car drawn mirrored in three of four passes). tools/abi_lint.py checks direct calls against their
callee's definition; an indirect call's callee is not known statically, so this lists the indirect calls that
receive an argument register (a1-a3, t0-t3, f12-f19) their function never wrote, where the function's native
definition does not even take that argument: the value cannot reach the callee natively. With --all, also
the ones whose function takes the argument (the call itself must pass it on).

Registers are tracked along the instructions in address order from the entry to the call (a write on any
earlier path counts as written), so the list errs towards silence.
"""
import argparse
import csv
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import abi_lint  # noqa: E402

GPR_ARGS = list(range(4, 12))     # a0-a3, t0-t3
FPR_ARGS = list(range(12, 20))    # f12-f19
NAMES = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3"]


def load_text(elf):
    data = open(elf, "rb").read()
    phoff = struct.unpack_from("<I", data, 0x1C)[0]
    phnum = struct.unpack_from("<H", data, 0x2C)[0]
    segs = []
    for i in range(phnum):
        p_type, off, vaddr, _pa, filesz, _ms, flags, _al = struct.unpack_from("<8I", data, phoff + i * 32)
        if p_type == 1 and flags & 1:
            segs.append((vaddr, data[off:off + filesz]))
    return segs


def word_at(segs, addr):
    for vaddr, blob in segs:
        if vaddr <= addr < vaddr + len(blob) - 3:
            return struct.unpack_from("<I", blob, addr - vaddr)[0]
    return None


def writes(w):
    """(GPRs written, FPRs written) by one instruction (EE subset that matters for argument registers)."""
    op = w >> 26
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    g, f = set(), set()
    if op == 0:   # SPECIAL
        funct = w & 63
        if funct in (0x08, 0x0C, 0x0D, 0x0F, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F):
            pass   # jr, syscall, break, sync, mthi, mtlo, mult(u)/div(u) (EE mult also writes rd)
        else:
            g.add(rd)
        if funct in (0x18, 0x19):
            g.add(rd)
    elif op == 1:   # REGIMM: bltzal/bgezal write ra
        if rt in (0x10, 0x11, 0x12, 0x13):
            g.add(31)
    elif op == 3:   # jal
        g.add(31)
    elif op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x18, 0x19,      # addi..lui, daddi(u)
                0x1A, 0x1B, 0x1E,                                               # ldl, ldr, lq
                0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37):          # loads, ld
        g.add(rt)
    elif op == 0x1C:   # MMI: rd
        g.add(rd)
    elif op == 0x11:   # COP1
        if rs in (0x00, 0x02):      # mfc1, cfc1
            g.add(rt)
        elif rs == 0x04:            # mtc1
            f.add(rd)
        elif rs in (0x10, 0x14):    # .S and .W arithmetic: fd (compares and ACC ops write none)
            funct = w & 63
            if not (funct >= 0x30 or funct in (0x18, 0x19, 0x1A, 0x1E, 0x1F)):
                f.add((w >> 6) & 31)
    elif op == 0x12:   # COP2: qmfc2/cfc2 write rt
        if rs in (0x01, 0x02):
            g.add(rt)
    elif op == 0x31:   # lwc1
        f.add(rt)
    return g, f


def indirect_calls(segs, start, size):
    """[(address, GPR pass-throughs, FPR pass-throughs)] for each jalr / jr-to-non-ra of the function."""
    out = []
    g_written, f_written = set(), set()
    for a in range(start, start + size, 4):
        w = word_at(segs, a)
        if w is None:
            break
        op, funct, rs = w >> 26, w & 63, (w >> 21) & 31
        indirect = op == 0 and (funct == 0x09 or (funct == 0x08 and rs != 31))
        if indirect:
            slot = word_at(segs, a + 4) or 0
            g2, f2 = writes(slot)
            gw, fw = g_written | g2, f_written | f2
            out.append((a, [r for r in GPR_ARGS if r not in gw], [r for r in FPR_ARGS if r not in fw]))
        g, f = writes(w)
        g_written |= g
        f_written |= f
    return out


def definitions(decomp):
    """{ADDR: (int params, float params, path)} for the sources the native link compiles."""
    files = abi_lint.native_sources(decomp, os.path.join(ROOT, "build", "game"))
    named = {}
    for line in open(os.path.join(decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", line)
        if m:
            named.setdefault(m.group(1), "%08X" % int(m.group(2), 16))
    rank = {"game_src": 0, "src": 1, "draft": 2}
    out = {}
    for path, role in files.items():
        base = os.path.splitext(os.path.basename(path))[0]
        m = re.search(r"func_([0-9A-Fa-f]{8})|^([0-9a-f]{8})_", base)
        addr = (m.group(1) or m.group(2)).upper() if m else named.get(base)
        if not addr:
            continue
        fname = "func_" + addr if m and m.group(1) else (base if not m else None)
        try:
            text = abi_lint.strip_comments(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            continue
        pat = r"\b(?:func_%s|%s)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:throw\s*\(\s*\)\s*)?\{" % (addr, re.escape(fname or "func_" + addr))
        d = re.search(pat, text, re.I)
        if not d:
            continue
        params = [p for p in abi_lint.split_params(d.group(1)) if p.strip() and p.strip() != "void"]
        kinds = [abi_lint.kind(p) for p in params]
        nint = sum(1 for k in kinds if k != "f")
        nflt = sum(1 for k in kinds if k == "f")
        cur = out.get(addr)
        if cur is None or rank[role] < cur[3]:
            out[addr] = (nint, nflt, os.path.relpath(path, decomp if path.startswith(decomp) else ROOT), rank[role])
    return out


def expected_arguments(decomp, defs):
    """{ADDR: (int arguments, float arguments, why)} the function is called with: the most its callers'
    declarations pass, and for a virtual method the most any method in the same slot of a related class
    takes (classes sharing a root base, config/classes.json)."""
    out = {}

    def raise_to(addr, nint, nflt, why):
        cur = out.get(addr, (0, 0, ""))
        if nint > cur[0] or nflt > cur[1]:
            out[addr] = (max(nint, cur[0]), max(nflt, cur[1]), why)

    for path, _role in abi_lint.native_sources(decomp, os.path.join(ROOT, "build", "game")).items():
        try:
            text = abi_lint.strip_comments(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            continue
        for addr, kinds, is_def, _line, _used, _ret in abi_lint.scan(path, text):
            if not is_def and kinds is not None:
                raise_to(addr, sum(1 for k in kinds if k != "f"), sum(1 for k in kinds if k == "f"),
                         "declared so in " + os.path.basename(path))
    classes = __import__("json").load(open(os.path.join(decomp, "config", "classes.json")))

    def primary(name):
        vts = (classes.get(name) or {}).get("vtables") or []
        return vts[0].get("methods") or [] if vts else []

    def introducer(name, slot):
        """The topmost class of name's primary base chain whose vtable already has that slot: the slot's
        method is the same virtual function in every class below it."""
        top, seen = name, set()
        while True:
            seen.add(top)
            bases = (classes.get(top) or {}).get("bases") or []
            if not bases or bases[0] not in classes or bases[0] in seen or len(primary(bases[0])) <= slot:
                return top
            top = bases[0]

    slots = {}
    for name in classes:
        for s, fn in enumerate(primary(name)):
            if fn:
                slots.setdefault((introducer(name, s), s), set()).add("%08X" % fn)
    for (r, s), fns in slots.items():
        known = [(defs[f][0], defs[f][1], f) for f in fns if f in defs]
        if not known:
            continue
        nint = max(k[0] for k in known)
        nflt = max(k[1] for k in known)
        best = max(known)[2]
        for f in fns:
            raise_to(f, nint, nflt, "slot %d of %s and its subclasses, as %s" % (s, r, best))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args()
    segs = load_text(os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))
    defs = definitions(a.decomp)
    expected = expected_arguments(a.decomp, defs)
    found = 0
    with open(os.path.join(a.decomp, "build", "functions.csv"), newline="") as f:
        rows = [(int(r["address"], 16), int(r.get("max_size") or r.get("size") or 0)) for r in csv.DictReader(f)]
    for start, size in sorted(rows):
        if size <= 0:
            continue
        addr = "%08X" % start
        d = defs.get(addr)
        if not d:
            continue
        nint, nflt, path, _ = d
        eint, eflt, why = expected.get(addr, (0, 0, ""))
        for site, gpr, fpr in indirect_calls(segs, start, size):
            # Registers the function receives (its callers pass that many) but does not take: on the EE they
            # reach the callee unchanged, natively they are lost.
            lost = [NAMES[r] for r in gpr if nint <= r - 4 < eint] + ["f%d" % r for r in fpr if nflt <= r - 12 < eflt]
            if lost:
                found += 1
                print("%s call at %08X: %s forwarded on the EE, not a parameter here (%d int, %d float; called with "
                      "%d, %d: %s) | %s" % (addr, site, " ".join(lost), nint, nflt, eint, eflt, why, path))
            elif a.all:
                kept = [NAMES[r] for r in gpr if r - 4 < nint and r != 4] + ["f%d" % r for r in fpr if r - 12 < nflt]
                if kept:
                    print("%s call at %08X: parameters %s forwarded: check the call passes them | %s"
                          % (addr, site, " ".join(kept), path))
    print("%d indirect calls with arguments the native build cannot forward" % found, file=sys.stderr)


if __name__ == "__main__":
    main()

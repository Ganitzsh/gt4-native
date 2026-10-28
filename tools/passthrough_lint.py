#!/usr/bin/env python3
"""Arguments the EE forwards to an indirect call without the source passing them.

    py tools/passthrough_lint.py --decomp C:\\path\\to\\gt4-decomp [--all [--readers]]

On the EE an argument register a function never writes still holds the value its caller put there, so a
function that calls through a pointer (jalr, or a jr tail call) forwards its own incoming arguments to the
callee whether its source names them or not. CompatibleModel::virtual_04(this, flag) called
this->callback(this->context) and the callback read flag from $a1: natively it read a stale stack slot (the
dealers' car drawn mirrored in three of four passes). tools/abi_lint.py checks direct calls against their
callee's definition; an indirect call's callee is not known statically, so this lists the indirect calls that
receive an argument register (a1-a3, t0-t3, f12-f19) their function never wrote, where the function's native
definition does not even take that argument: the value cannot reach the callee natively. With --all, also
the ones whose function takes the argument (the call itself must pass it on): ResultArcade::setCarName took
the race entry but called the slot-9 virtual without it, and a licence test's result display crashed copying
the car name; the strobe trampoline 004811F0 dropped its callback's arguments 2-4 (a crash in every test). --readers narrows the vtable calls
to those where some method in that slot (config/classes.json) reads a forwarded register.

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
        if indirect and funct == 0x08 and is_switch(segs, a, start):
            indirect = False   # a jump table, not a tail call
        if indirect:
            slot = word_at(segs, a + 4) or 0
            g2, f2 = writes(slot)
            gw, fw = g_written | g2, f_written | f2
            out.append((a, [r for r in GPR_ARGS if r not in gw], [r for r in FPR_ARGS if r not in fw]))
        g, f = writes(w)
        g_written |= g
        f_written |= f
        if op == 3 or (op == 0 and funct == 0x09):   # after a call the argument registers hold the callee's values
            g_written |= set(GPR_ARGS)
            f_written |= set(FPR_ARGS)
    return out


def reads(w):
    """(GPRs read, FPRs read) by one instruction (the EE subset that matters for argument registers)."""
    op = w >> 26
    rs, rt, rd = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31
    g, f = set(), set()
    if op == 0:   # SPECIAL: shifts by a constant read rt only, the rest rs and rt
        funct = w & 63
        if funct in (0x00, 0x02, 0x03, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F):
            g.add(rt)
        elif funct in (0x08, 0x09, 0x11, 0x13):   # jr, jalr, mthi, mtlo
            g.add(rs)
        elif funct not in (0x0C, 0x0D, 0x0F, 0x10, 0x12):   # syscall, break, sync, mfhi, mflo
            g |= {rs, rt}
    elif op == 1:   # REGIMM branches
        g.add(rs)
    elif op in (0x04, 0x05, 0x14, 0x15):   # beq, bne(l)
        g |= {rs, rt}
    elif op in (0x06, 0x07, 0x16, 0x17):   # blez, bgtz(l)
        g.add(rs)
    elif op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x18, 0x19, 0x1A, 0x1B, 0x1E,
                0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37, 0x31, 0x33):   # immediates, loads
        g.add(rs)
    elif op in (0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x3F, 0x1F):   # stores (sb..swr, sd, sq)
        g |= {rs, rt}
    elif op == 0x39:   # swc1
        g.add(rs)
        f.add(rt)
    elif op == 0x1C:   # MMI
        g |= {rs, rt}
    elif op == 0x11:   # COP1
        if rs in (0x04, 0x06):        # mtc1, ctc1
            g.add(rt)
        elif rs in (0x10, 0x14):      # .S / .W: fs, and ft for two-operand operations
            funct = w & 63
            f.add(rd)                 # fs
            if funct in (0x00, 0x01, 0x02, 0x03, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x28, 0x29) or funct >= 0x30:
                f.add(rt)             # ft
        elif rs == 0x00 or rs == 0x02:   # mfc1, cfc1 read fs
            f.add(rd)
    return g, f


def argument_reads(segs, start, size, rows, memo, depth=0):
    """(GPR, FPR) argument registers the function at `start` reads before writing them (address order): read
    by an instruction, or by a direct callee called before the function wrote them. An indirect call counts as
    reading every argument register still unwritten (it may forward them)."""
    key = start
    if key in memo:
        return memo[key]
    memo[key] = (set(), set())   # recursion guard
    gr, fr, gw, fw = set(), set(), {0}, set()
    for a in range(start, start + size, 4):
        w = word_at(segs, a)
        if w is None:
            break
        op, funct = w >> 26, w & 63
        g, f = reads(w)
        gr |= {r for r in g if r in GPR_ARGS and r not in gw}
        fr |= {r for r in f if r in FPR_ARGS and r not in fw}
        call = op == 3 or (op == 0 and funct == 0x09) or (op == 0 and funct == 0x08 and ((w >> 21) & 31) != 31)
        if call:
            slot = word_at(segs, a + 4) or 0
            g2, f2 = reads(slot)
            gr |= {r for r in g2 if r in GPR_ARGS and r not in gw}
            fr |= {r for r in f2 if r in FPR_ARGS and r not in fw}
            gw2, fw2 = writes(slot)
            gwc, fwc = gw | gw2, fw | fw2
            if op == 3 and depth < 6:
                target = (a & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                if target in rows:
                    cg, cf = argument_reads(segs, target, rows[target], rows, memo, depth + 1)
                    gr |= {r for r in cg if r not in gwc}
                    fr |= {r for r in cf if r not in fwc}
            elif op == 0:
                gr |= {r for r in GPR_ARGS if r not in gwc}
                fr |= {r for r in FPR_ARGS if r not in fwc}
            if op == 0 and funct == 0x08:
                break   # a tail call ends the path
            gw |= set(GPR_ARGS)
            fw |= set(FPR_ARGS)
            continue
        if op == 0 and funct == 0x08:   # jr ra: this path returns (later code is another branch: keep going)
            pass
        g, f = writes(w)
        gw |= g
        fw |= f
    memo[key] = (gr, fr)
    return memo[key]


def vtable_slot(segs, start, site):
    """The vtable slot an indirect call at `site` calls through: the `lw REG, OFF(base)` that loaded the jump
    register (gcc 2.x vtables: 8-byte entries {delta, index, function}, entry 0 the header; slot s's function
    at 8 * (s + 1) + 4). None when the register came from elsewhere."""
    reg = ((word_at(segs, site) or 0) >> 21) & 31
    for a in range(site - 4, max(start, site - 64) - 4, -4):
        w = word_at(segs, a) or 0
        g, _ = writes(w)
        if reg in g:
            if (w >> 26) == 0x23 and ((w >> 16) & 31) == reg:
                off = w & 0xFFFF
                if off >= 12 and (off - 4) % 8 == 0:
                    return (off - 4) // 8 - 1
                if off == 4:   # the entry's address computed first (its delta is loaded from it too)
                    base = (w >> 21) & 31
                    for b in range(a - 4, max(start, a - 64) - 4, -4):
                        wb = word_at(segs, b) or 0
                        if base in writes(wb)[0]:
                            imm = wb & 0xFFFF
                            if (wb >> 26) == 0x09 and imm >= 8 and imm % 8 == 0:   # addiu base, vtbl, 8 * (s + 1)
                                return imm // 8 - 1
                            return None
            return None
    return None


def is_switch(segs, at, start):
    """A jr through a jump table: an sltiu bounds check and a load of the target shortly before."""
    seen_load = seen_check = False
    for a in range(max(start, at - 48), at, 4):
        w = word_at(segs, a) or 0
        seen_check |= (w >> 26) == 0x0B          # sltiu
        seen_load |= (w >> 26) == 0x23           # lw
    return seen_check and seen_load


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
        rets = [r for (x, _k, is_def, _l, _u, r) in abi_lint.scan(path, text) if is_def and x == addr]
        if rets and rets[0] == "s":
            kinds = ["i"] + kinds   # the returned struct's address comes first
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
    ap.add_argument("--readers", action="store_true",
                    help="with --all: of the vtable calls, only those where a method in that slot of some class "
                         "reads a forwarded register (or forwards it on), with those methods; other indirect "
                         "calls (callbacks, function-pointer fields) are all kept, as 'callee unknown'")
    a = ap.parse_args()
    segs = load_text(os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf"))
    defs = definitions(a.decomp)
    expected = expected_arguments(a.decomp, defs)
    found = 0
    with open(os.path.join(a.decomp, "build", "functions.csv"), newline="") as f:
        rows = [(int(r["address"], 16), int(r.get("max_size") or r.get("size") or 0)) for r in csv.DictReader(f)]
    sizes = dict(rows)
    by_slot, memo = {}, {}
    if a.readers:
        for c in __import__("json").load(open(os.path.join(a.decomp, "config", "classes.json"))).values():
            for vt in c.get("vtables") or []:
                for s, fn in enumerate(vt.get("methods") or []):
                    if fn:
                        by_slot.setdefault(s, set()).add(fn)
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
                if kept and a.readers:
                    slot = vtable_slot(segs, start, site)
                    if slot is None:   # a callback or function-pointer field: the callee is not known
                        print("%s call at %08X: callee unknown, parameters %s forwarded | %s"
                              % (addr, site, " ".join(kept), path))
                        continue
                    users = []
                    for fn in sorted(by_slot.get(slot, ())):
                        if fn not in sizes:
                            continue
                        rg, rf = argument_reads(segs, fn, sizes[fn], sizes, memo)
                        used = [n for n in kept if (n.startswith("f") and int(n[1:]) in rf) or
                                (not n.startswith("f") and NAMES.index(n) in rg)]
                        if used:
                            users.append("%08X(%s)" % (fn, " ".join(used)))
                    if users:
                        print("%s call at %08X: slot %d, parameters %s forwarded, read by %s | %s"
                              % (addr, site, slot, " ".join(kept), ", ".join(users[:6]) +
                                 (" +%d" % (len(users) - 6) if len(users) > 6 else ""), path))
                    continue
                if kept:
                    print("%s call at %08X: parameters %s forwarded: check the call passes them | %s"
                          % (addr, site, " ".join(kept), path))
    print("%d indirect calls with arguments the native build cannot forward" % found, file=sys.stderr)


if __name__ == "__main__":
    main()

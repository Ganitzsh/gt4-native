#!/usr/bin/env python3
"""Differential test: a decomp function compiled for the host against the original MIPS code.

For one function of a gt4-decomp checkout:
  * the original side runs the function's bytes from the game image in a MIPS emulator (Unicorn);
  * the native side compiles the decomp source for a 32-bit host (the EE `long` spelled long long,
    128-bit registers as a struct; see compile_census.py --guest32) and runs it with guest memory
    mapped at the same addresses;
  * both get the same trials (4 integer arguments, some pointing into a shared random memory
    block) and must return the same value and leave the same memory.

    diff_test.py --decomp /mnt/e/PROJECTS/GT4 ADDR [ADDR ...] [--trials 300] [--seed 1]
                 [--args p,i,i,i]

Argument kinds: p pointer into the memory block, i small int, w random 32-bit, c byte. Default:
taken from the parameters of the source's function (`*` is p, else i). Only functions that call
nothing and touch no globals are covered so far: an undefined symbol skips the function.
Needs the decomp's venv (rabbitizer, via tools/match.py) with unicorn installed.
"""
import argparse
import os
import random
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(os.path.dirname(HERE))
ERRORS = {}
BASE, SIZE = 0x10000000, 0x4000          # the shared memory block
STACK, STACK_SIZE = 0x7F000000, 0x10000
CODE_PAGE = 0x1000
SENTINEL = 0x7E000000

sys.path.insert(0, os.path.join(NATIVE, "tools"))
from compile_census import guest_long  # noqa: E402


def decomp_source(decomp, addr):
    for ext in ("c", "cpp"):
        for root, _, names in os.walk(os.path.join(decomp, "src")):
            for n in names:
                if n.lower() == f"func_{addr:08x}.{ext}":
                    return os.path.join(root, n)
    return None


def arg_kinds(path, addr):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(rf"\bfunc_{addr:08X}\s*\(([^)]*)\)", text)
    if not m:
        return ["i"] * 4
    params = [p for p in m.group(1).split(",") if p.strip() and p.strip() != "void"]
    kinds = ["p" if "*" in p else ("f" if re.search(r"\b(f32|float)\b", p) else
                                   "c" if re.search(r"\b(char|s8|u8)\b", p) else "i") for p in params]
    return (kinds + ["i"] * 4)[:4]


def returns_value(path, addr):
    """False when the function is declared void: the value left in v0 / eax means nothing there."""
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(rf"([\w\s\*]+?)\bfunc_{addr:08X}\s*\(", text)
    head = (m.group(1) if m else "").replace('extern "C"', "").strip().split("\n")[-1]
    return not re.search(r"\bvoid\b(?!\s*\*)", head)


def return_kind(path, addr):
    """('void'|'f32'|'int', width in bits, C type for the native driver) from the source's head."""
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(rf"([\w\s\*]+?)\bfunc_{addr:08X}\s*\(", text)
    head = (m.group(1) if m else "").replace('extern "C"', "").strip().split("\n")[-1]
    if re.search(r"\bvoid\b(?!\s*\*)", head):
        return "void", 32, "int"
    if re.search(r"\b(f32|float)\b(?!\s*\*)", head):
        return "f32", 32, "float"
    if "*" not in head and re.search(r"\b(s16|u16|short)\b", head):
        return "int", 16, "short"
    if "*" not in head and re.search(r"\b(s8|u8|char)\b", head):
        return "int", 8, "char"
    if "*" not in head and re.search(r"\b(f64|double|s64|u64|long long)\b", head):
        return "unsupported", 64, "int"
    return "int", 32, "int"


def make_memory(rng, floaty=False):
    """Random words: some small ints, some pointers into the block, some wild."""
    words = []
    for _ in range(SIZE // 4):
        r = rng.random()
        if r < 0.45:
            # small integers read as floats are denormals, which the PS2 FPU flushes and
            # Unicorn's generic MIPS FPU does not: float functions get normal floats instead
            words.append(rng.randrange(0, 64) if not floaty else
                         struct.unpack("<I", struct.pack("<f", rng.uniform(-1000, 1000)))[0])
        elif r < 0.85:
            words.append(BASE + rng.randrange(0, SIZE // 4) * 4)
        else:
            words.append(rng.randrange(0, 1 << 32))
    return struct.pack(f"<{SIZE // 4}I", *words)


def make_args(kinds, rng):
    out = []
    for k in kinds:
        if k == "p":
            out.append(BASE + rng.randrange(0, SIZE // 64) * 16)
        elif k == "c":
            out.append(rng.randrange(0, 256))
        elif k == "f":
            out.append(struct.unpack("<I", struct.pack("<f", rng.choice([0.0, 1.0, -1.0, 0.5, 2.5, 100.0, 3.14159, -7.25, rng.uniform(-1000, 1000)])))[0])
        elif k == "w":
            out.append(rng.randrange(0, 1 << 32))
        else:
            out.append(rng.choice([0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 100, 255, 256, 0x7FFF, 0xFFFF, rng.randrange(0, 1 << 20)]))
    return out


def run_original(decomp, addr, kinds, ret, trials):
    sys.path.insert(0, os.path.join(decomp, "tools"))
    cwd = os.getcwd()
    os.chdir(decomp)
    import match
    text_addr, text = match.load_text()
    words = match.trim_padding(match.words_at(text_addr, text, addr, match.function_span(addr)))
    os.chdir(cwd)
    code = struct.pack(f"<{len(words)}I", *words)
    from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS64, UC_MODE_LITTLE_ENDIAN, UcError
    from unicorn.mips_const import UC_CPU_MIPS64_5KF
    from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_V0, UC_MIPS_REG_RA, UC_MIPS_REG_SP, UC_MIPS_REG_F0, UC_MIPS_REG_F12
    results = []
    for args, mem in trials:
        mu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_LITTLE_ENDIAN)
        mu.ctl_set_cpu_model(UC_CPU_MIPS64_5KF)
        pages = (len(code) + 0xFFF) // 0x1000 * 0x1000
        mu.mem_map(addr & ~0xFFF, pages + 0x1000)
        mu.mem_write(addr, code)
        mu.mem_map(BASE, SIZE)
        mu.mem_write(BASE, mem)
        mu.mem_map(STACK, STACK_SIZE)
        mu.mem_map(SENTINEL, 0x1000)
        ints = floats = 0
        for k, a in zip(kinds, args):
            if k == "f":
                mu.reg_write(UC_MIPS_REG_F12 + floats, a)
                floats += 1
            else:
                mu.reg_write(UC_MIPS_REG_A0 + ints, a if a < 0x80000000 else a - (1 << 32) + (1 << 64))
                ints += 1
        mu.reg_write(UC_MIPS_REG_SP, STACK + STACK_SIZE - 0x100)
        mu.reg_write(UC_MIPS_REG_RA, SENTINEL)
        try:
            mu.emu_start(addr, SENTINEL, timeout=2_000_000, count=200000)
            v0 = (mu.reg_read(UC_MIPS_REG_F0) if ret[0] == "f32" else mu.reg_read(UC_MIPS_REG_V0)) & 0xFFFFFFFF
            ok = True
        except UcError as e:
            ok, v0 = False, 0
            ERRORS.setdefault(addr, str(e))
        results.append((ok, v0, bytes(mu.mem_read(BASE, SIZE)) if ok else b""))
    return results


def build_native(decomp, addr, src, ctype="int"):
    work = tempfile.mkdtemp(prefix=f"difft_{addr:08x}_")
    ext = os.path.splitext(src)[1]
    text = guest_long(open(src, encoding="utf-8", errors="replace").read())
    text = re.sub(r"^\s*/\*\s*compiler:[^*]*\*/", "", text, count=1)
    srcf = os.path.join(work, "under_test" + ext)
    open(srcf, "w", encoding="utf-8").write(text)
    inc = ["-I", os.path.join(NATIVE, "compat", "host32"), "-I", os.path.join(decomp, "include"),
           "-I", os.path.join(decomp, "include", "shim"), "-I", os.path.join(decomp, "include", "stl"),
           "-I", os.path.dirname(src)]
    cc = "g++" if ext == ".cpp" else "gcc"
    obj = os.path.join(work, "under_test.o")
    r = subprocess.run([cc, "-m32", "-malign-double", "-msse2", "-mfpmath=sse", "-O1", "-w", "-fpermissive", "-fno-strict-aliasing", "-c", srcf, "-o", obj] + inc,
                       capture_output=True, text=True)
    if r.returncode:
        return None, "compile: " + next((l for l in r.stderr.splitlines() if "error" in l), r.stderr[:200])
    undef = subprocess.run(["nm", "-u", obj], capture_output=True, text=True).stdout.split()
    undef = [u for u in undef if u not in ("U",) and not u.startswith(("_GLOBAL_OFFSET", "__stack_chk", "__x86"))]
    if undef:
        return None, "undefined: " + ", ".join(sorted(set(undef))[:4])
    drv = os.path.join(work, "driver.o")
    subprocess.run(["gcc", "-m32", "-malign-double", "-msse2", "-O1", "-c", os.path.join(HERE, "driver.c"), "-o", drv,
                    f"-Dfunc_UNDER_TEST=func_{addr:08X}", f"-DRET_T={ctype}"], check=True)
    exe = os.path.join(work, "run")
    r = subprocess.run(["gcc", "-m32", "-no-pie", obj, drv, "-o", exe], capture_output=True, text=True)
    if r.returncode:
        return None, "link: " + r.stderr[:200]
    return exe, work


def run_native(exe, work, trials):
    inp, out = os.path.join(work, "in.bin"), os.path.join(work, "out.bin")
    with open(inp, "wb") as f:
        f.write(struct.pack("<3I", BASE, SIZE, len(trials)))
        for args, mem in trials:
            f.write(struct.pack("<4I", *args) + mem)
    r = subprocess.run([exe, inp, out], capture_output=True, text=True, timeout=600)
    if r.returncode:
        raise RuntimeError("native runner: " + r.stderr[:200])
    data = open(out, "rb").read()
    rec = 12 + SIZE
    res = []
    for i in range(len(trials)):
        st, lo, hi = struct.unpack_from("<3I", data, i * rec)
        res.append((bool(st), lo, data[i * rec + 12:(i + 1) * rec]))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("addrs", nargs="+")
    ap.add_argument("--trials", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--args")
    ap.add_argument("--source", help="test this file instead of the decomp's own source (a negative control)")
    ap.add_argument("--sample", type=int, default=0, help="with `all`: a random sample of that many sources")
    a = ap.parse_args()
    addrs = a.addrs
    if addrs == ["all"]:
        found = []
        for root, _, names in os.walk(os.path.join(a.decomp, "src")):
            for n in names:
                m = re.fullmatch(r"func_([0-9A-Fa-f]{8})\.(c|cpp)", n)
                if m:
                    found.append(m.group(1).lower())
        found.sort()
        if a.sample:
            random.Random(7).shuffle(found)
            found = found[:a.sample]
        addrs = found
    totals = {}
    for s in addrs:
        addr = int(s, 16)
        src = a.source or decomp_source(a.decomp, addr)
        if not src:
            print(f"{addr:08X}: no source"); continue
        ret = return_kind(src, addr)
        if ret[0] == "unsupported":
            print(f"{addr:08X}: skipped (64-bit or double return)", flush=True)
            totals["skipped: wide return"] = totals.get("skipped: wide return", 0) + 1
            continue
        exe, work = build_native(a.decomp, addr, src, ret[2])
        if not exe:
            print(f"{addr:08X}: skipped ({work})", flush=True)
            totals["skipped: " + work.split(":")[0]] = totals.get("skipped: " + work.split(":")[0], 0) + 1
            continue
        kinds = a.args.split(",") if a.args else arg_kinds(src, addr)
        rng = random.Random(a.seed)
        src_text = open(src, encoding="utf-8", errors="replace").read()
        floaty = ret[0] == "f32" or "f" in kinds or bool(re.search(r"\b(f32|float)\b", src_text))
        trials = [(make_args(kinds, rng), make_memory(rng, floaty)) for _ in range(a.trials)]
        nat = run_native(exe, work, trials)
        org = run_original(a.decomp, addr, kinds, ret, trials)
        check_ret = ret[0] != "void"
        mask = (1 << ret[1]) - 1
        same = diff = both_fault = 0
        first = None
        def canon(v):
            return 0x7FC00000 if ret[0] == "f32" and (v & 0x7F800000) == 0x7F800000 and (v & 0x7FFFFF) else v

        for i, ((nok, nret, nmem), (ook, oret, omem)) in enumerate(zip(nat, org)):
            nret, oret = canon(nret), canon(oret)
            if not nok and not ook:
                both_fault += 1
            elif nok and ook and ((nret & mask) == (oret & mask) or not check_ret) and nmem == omem:
                same += 1
            else:
                diff += 1
                if first is None:
                    why = "native faulted" if not nok else "original faulted" if not ook else \
                        "return value" if ((nret & mask) != (oret & mask) and check_ret) else "memory"
                    first = (i, why, trials[i][0], nret, oret)
        oracle_stuck = "EXCEPTION" in ERRORS.get(addr, "") and all(not o[0] for o in org)
        verdict = ("ORACLE-UNSUPPORTED" if oracle_stuck else
                   "AGREE" if diff == 0 and same else "DIFFER" if diff else "no usable trial")
        line = f"{addr:08X} {kinds}: {verdict}  same={same} differ={diff} both-fault={both_fault}"
        if first:
            line += (f"  [original: {ERRORS[addr]}]" if addr in ERRORS else "") + f"  first: trial {first[0]} {first[1]} args={[hex(x) for x in first[2]]} native={first[3]:#x} orig={first[4]:#x}"
        print(line, flush=True)
        totals[verdict] = totals.get(verdict, 0) + 1
    print("SUMMARY", totals)


if __name__ == "__main__":
    main()

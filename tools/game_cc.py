#!/usr/bin/env python3
"""Compile the decomp's matched game sources for the native (32-bit x86 Windows) program.

The compiler is gcc 2.9-ee-990721 (Sony's ee-gcc source) retargeted to i386-pc-mingw32 with the
EE's data model (64-bit long, 32-bit bool, 64-bit long double, int wchar_t, 8-aligned 64-bit
types); see docs/GAME_TOOLCHAIN.md and tools/game_toolchain/. It runs in WSL. This driver runs
on Windows:

  1. reads each source's compiler profile (its first line, `/* compiler: NAME */`, else the
     project's compiler) from the decomp's project.toml and maps the EE flags to i386 ones;
  2. ships the sources in batches to WSL (one `wsl` call per batch, a tar on stdin: the C: drive
     is slow per file from Linux), with on-the-fly transforms (transform(): mode(TI) typedefs,
     hexadecimal float constants, one-instruction MIPS asm with a C meaning); the native compat
     headers (compat/game) come first on the include path and compat/game/gt4_prelude.h is
     included first; compiles there in parallel (--jobs, each compile killed after --timeout s)
     and gets the .s files back as a tar on stdout;
  3. assembles each .s on Windows with MSYS2's i686 `as --32` into a COFF .o;
  4. writes a census (JSONL: file, profile, ok, stage, first error, category).

  py tools/game_cc.py --decomp C:/Users/julie/gt4-decomp --out build/game [--all | FILE... |
                      --list FILE] [--jobs 8] [--timeout 60] [--batch 1500] [--keep-s]
                      [--export own|all]

Each object exports only its own function (--export own, the decomp build's policy): sources
repeat their neighbours' definitions so that each compiles alone, and every such copy becomes a
local symbol.

Outputs (under --out): obj/<src path>.o, s/<src path>.s (only failures unless --keep-s),
census.jsonl, census_summary.txt. Never writes in the decomp tree.
"""
import argparse
import collections
import concurrent.futures
import fractions
import io
import json
import math
import os
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "game_toolchain"))
import asm_translate  # noqa: E402

try:
    import tomllib
except ImportError:  # Python < 3.11
    tomllib = None

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COMPAT = os.path.join(ROOT, "compat", "game")
AS_EXE = os.environ.get("GT4_AS", r"C:\msys64\mingw32\bin\as.exe")
# The compiler in WSL ($HOME expanded there); tools/game_toolchain/build_gcc29.sh installs it.
GCC = os.environ.get("GT4_GAME_GCC", "$HOME/.local/share/gt4/i386-gcc29/bin/i386-gt4-gcc")

MARKER = re.compile(rb"^/\* compiler: ([\w.+-]+) \*/\s*$")
# EE flags with no meaning here (MIPS, small data, the matching assemblers, debug info) or that
# gcc 2.9 does not know (sibling-call optimisation arrived in 2.96).
DROP_PREFIX = ("-G", "-m", "-B", "-g")
DROP = {"-c", "-fno-optimize-sibling-calls", "-foptimize-sibling-calls"}
# Always on: the decompiled code reads one object through several types all the time.
# Struct returns follow the EE (tools/game_toolchain/build_gcc29.sh: in registers when the EE uses
# $v0), so the decomp's two declarations of one function (struct by value or scalar) agree.
# Float and double results come back in eax (eax:edx), not on the x87 stack: the EE returns them
# in $f0, so a caller that declares a float function void (or ignores its result) is harmless
# there, but natively every such call would leave a value on the x87 stack until it overflows
# and float loads become NaN. Runtime code that returns a float to game code returns its bits.
ALWAYS = ["-fno-strict-aliasing", "-mno-fp-ret-in-387"] + os.environ.get("GT4_CC_EXTRA", "").split()  # GT4_CC_EXTRA: debugging flags

# mode(TI) typedefs: `typedef [unsigned|signed] int NAME __attribute__((mode(TI)));`
TI_TYPEDEF = re.compile(
    rb"typedef\s+((?:unsigned|signed)\s+int|unsigned|signed|int)\s+(\w+)\s*"
    rb"__attribute__\s*\(\(\s*(?:__)?mode(?:__)?\s*\(\s*(?:__)?TI(?:__)?\s*\)\s*\)\)\s*;")


# C99 hexadecimal floating constants (0x1.8p3f), which ee-gcc 2.96 reads and gcc 2.9 rejects
# (C++: "floating constant may not be in radix 16"; C: an internal error in real_hex_to_f).
HEX_FLOAT = re.compile(rb"(?<![\w.])0[xX]([0-9a-fA-F]*)(?:\.([0-9a-fA-F]*))?[pP]([+-]?\d+)([fFlL]?)(?![\w.])")


def hex_float_decimal(m):
    """The same value as a decimal constant: the shortest digits that convert back to the same
    float (f suffix) or double; both are well inside the precision of gcc's real.c conversion."""
    whole, frac, exp, suffix = m.group(1) or b"0", m.group(2) or b"", m.group(3), m.group(4)
    value = float.fromhex("0x%s.%sp%s" % (whole.decode(), frac.decode(), exp.decode()))
    if suffix in (b"f", b"F"):
        for digits in range(6, 10):
            text = "%.*g" % (digits, value)
            if struct.unpack("<f", struct.pack("<f", float(text)))[0] == value:
                break
    else:
        text = repr(value)
    if "e" not in text and "." not in text and "inf" not in text:
        text += ".0"
    return text.encode() + suffix


# Decimal floating constants. ee-gcc 2.96 converts them toward zero (0.1f is 0x3DCCCCCC, 1.02 is
# 0x3FF051EB851EB851), gcc 2.9 for x86 to nearest (0x3DCCCCCD, ...852): every inexact constant would be one
# ulp off natively. Each one becomes the decimal that rounds, to nearest, to the EE's value.
DEC_FLOAT = re.compile(rb"(?<![\w.])((?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?|\d+[eE][+-]?\d+)([fFlL]?)(?![\w.])")


def _toward_zero(exact, single):
    """The float (single) or double nearest `exact` (a positive Fraction) that is not above it."""
    if single:
        try:
            value = struct.unpack("<f", struct.pack("<f", float(exact)))[0]
        except OverflowError:   # past FLT_MAX: the largest float, toward zero
            value = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]
        if fractions.Fraction(value) > exact:
            bits = struct.unpack("<I", struct.pack("<f", value))[0] - 1
            value = struct.unpack("<f", struct.pack("<I", bits))[0]
    else:
        value = float(exact)
        if fractions.Fraction(value) > exact:
            value = math.nextafter(value, 0.0)
    return value


def ee_float_literal(m):
    text, suffix = m.group(1).decode(), m.group(2)
    if suffix in (b"l", b"L"):
        return m.group(0)
    single = suffix in (b"f", b"F")
    exact = fractions.Fraction(text)
    if exact == 0:
        return m.group(0)
    value = _toward_zero(exact, single)
    if fractions.Fraction(value) == exact:
        return m.group(0)
    if single:
        for digits in range(6, 12):
            out = "%.*g" % (digits, value)
            if struct.unpack("<f", struct.pack("<f", float(out)))[0] == value:
                break
    else:
        out = repr(value)
    if "e" not in out and "." not in out:
        out += ".0"
    return out.encode() + suffix


def ee_float_constants(data):
    """DEC_FLOAT applied outside comments, string and character literals and preprocessor lines'
    #include."""
    out, i, n = [], 0, len(data)
    code_start = 0
    while i < n:
        c = data[i:i + 1]
        two = data[i:i + 2]
        if two == b"//" or two == b"/*" or c in (b'"', b"'"):
            out.append(DEC_FLOAT.sub(ee_float_literal, data[code_start:i]))
            if two == b"//":
                j = data.find(b"\n", i)
                j = n if j < 0 else j
            elif two == b"/*":
                j = data.find(b"*/", i + 2)
                j = n if j < 0 else j + 2
            else:
                j = i + 1
                while j < n and data[j:j + 1] != c:
                    j += 2 if data[j:j + 1] == b"\\" else 1
                j = min(j + 1, n)
            out.append(data[i:j])
            i = code_start = j
            continue
        i += 1
    out.append(DEC_FLOAT.sub(ee_float_literal, data[code_start:]))
    return b"".join(out)


ASM_START = re.compile(rb"\b(?:__asm__|asm)\s*(?:(?:volatile|__volatile__)\s*)?\(")


def _split_top(text, sep):
    """text split at sep where no bracket or string is open."""
    parts, depth, cur, i, quote = [], 0, b"", 0, None
    while i < len(text):
        c = text[i:i + 1]
        if quote:
            cur += c
            if c == b"\\":
                cur += text[i + 1:i + 2]
                i += 1
            elif c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
            cur += c
        elif c in b"([{":
            depth += 1
            cur += c
        elif c in b")]}":
            depth -= 1
            cur += c
        elif c == sep and depth == 0:
            parts.append(cur)
            cur = b""
        else:
            cur += c
        i += 1
    parts.append(cur)
    return parts


def _close_paren(data, i):
    """Index just past the ')' matching the '(' at data[i - 1]; None if unbalanced."""
    depth, quote = 1, None
    while i < len(data):
        c = data[i:i + 1]
        if quote:
            if c == b"\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
        elif c == b"(":
            depth += 1
        elif c == b")":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == b"\n" and data[i - 1:i] == b"\\":
            return None  # inside a macro definition: leave it
        i += 1
    return None


_OPERAND = re.compile(rb'^\s*"[^"]*"\s*\((.*)\)\s*$', re.S)
_BARRIER = b'__asm__ __volatile__ ("" : : : "memory")'
# Normalised instruction text -> C ({o0} outputs, {i1}... inputs, numbered as in the asm).
ASM_C = [
    (re.compile(rb"^min\.s %0,%1,%2$"), b"{o0} = __gt4_minf ({i1}, {i2})"),
    (re.compile(rb"^max\.s %0,%1,%2$"), b"{o0} = __gt4_maxf ({i1}, {i2})"),
    (re.compile(rb"^sqrt\.s %0,%1$"), b"{o0} = __gt4_sqrtf ({i1})"),
    (re.compile(rb"^abs\.s %0,%1$"), b"{o0} = __gt4_absf ({i1})"),
    (re.compile(rb"^neg\.s %0,%1$"), b"{o0} = -({i1})"),
    (re.compile(rb"^rsqrt\.s %0,%1,%2$"), b"{o0} = ({i1}) / __gt4_sqrtf ({i2})"),
    (re.compile(rb"^mtc1 \$0,%0$"), b"{o0} = 0.0f"),
    (re.compile(rb"^mtc1 %1,%0$"), b"{o0} = __gt4_bits_float (__GT4_LO32 ({i1}))"),
    (re.compile(rb"^mfc1 %0,%1$"), b"{o0} = __gt4_float_bits ({i1})"),
    (re.compile(rb"^plzcw %0,%1$"), b"{o0} = __gt4_plzcw ((int) __GT4_LO32 ({i1}))"),
    (re.compile(rb"^(?:sync|sync\.l|sync\.p|vnop|nop|pref [^%]*%0\)|cache [^%]*%0\))$"), _BARRIER),
    (re.compile(rb"^ei$"), b"__gt4_ee_ei ()"),
    (re.compile(rb"^sync\.l;ei$"), b"__gt4_ee_ei ()"),
    (re.compile(rb"^di$"), b"__gt4_ee_di ()"),
    (re.compile(rb"^mfc0 %0,\$(\d+)$"), rb"{o0} = __gt4_ee_mfc0 (\1)"),
    (re.compile(rb"^li %0,(-?(?:0x[0-9a-fA-F]+|\d+))$"), rb"{o0} = (__typeof__ ({o0})) \1"),
    (re.compile(rb"^(?:daddu %0,\$(?:28|gp),\$0|move %0,\$(?:28|gp))$"), b"{o0} = (__typeof__ ({o0})) __gt4_ee_get_gp ()"),
    (re.compile(rb"^(?:daddu \$(?:28|gp),%0,\$0|move \$(?:28|gp),%0)$"), b"__gt4_ee_set_gp ((void *) __GT4_LO32 ({i0}))"),
    (re.compile(rb"^sw \$(?:28|gp),%0$"), b"*(void **) &({o0}) = __gt4_ee_get_gp ()"),
    (re.compile(rb"^break(?: (\d+))?$"), rb"__gt4_ee_break (0\1)"),
]


def _close_paren_macro(data, i):
    """As _close_paren, through backslash-newlines (an asm statement in a #define)."""
    depth, quote = 1, None
    while i < len(data):
        c = data[i:i + 1]
        if quote:
            if c == b"\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in (b'"', b"'"):
            quote = c
        elif c == b"(":
            depth += 1
        elif c == b")":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == b"\n" and data[i - 1:i] != b"\\":
            return None  # the end of a #define line: unbalanced
        i += 1
    return None


def _asm_c(data, m, end):
    """The ASM_C translation of the asm statement data[m.start():end], or None."""
    parts = _split_top(data[m.end():end - 1], b":")
    strings = re.findall(rb'"((?:[^"\\]|\\.)*)"', parts[0])
    if not strings or re.sub(rb'"(?:[^"\\]|\\.)*"|\s', b"", parts[0]):
        return None
    insn = b"".join(strings).replace(b"\\n", b";").replace(b"\\t", b" ")
    insn = re.sub(rb"\s*([,;()])\s*", rb"\1", insn.strip()).strip(b";")
    insn = re.sub(rb"\s+", b" ", insn)
    ops = []
    for section in parts[1:3]:
        for o in _split_top(section, b","):
            if o.strip():
                om = _OPERAND.match(o)
                if not om:
                    return None
                ops.append(om.group(1).strip())
    n_out = len([o for o in _split_top(parts[1], b",") if o.strip()]) if len(parts) > 1 else 0
    for rx, tmpl in ASM_C:
        im = rx.match(insn)
        if not im:
            continue
        c = im.expand(tmpl)
        try:
            for k, e in enumerate(ops):
                c = c.replace(b"{o%d}" % k if k < n_out else b"{i%d}" % k, b"(" + e + b")")
        except Exception:
            return None
        if b"{" in c.replace(b"__GT4", b"") and re.search(rb"\{[oi]\d\}", c):
            return None  # an operand the template needs is missing
        return b"(" + c + b")" if not c.startswith(b"__asm__") else c
    return None


def translate_asm(data, name=None):
    """MIPS asm statements become C. One-instruction statements with a C meaning (the decomp's
    policy, tools/asm_policy.py) go through ASM_C: FPU min/max/sqrt/abs/rsqrt, mtc1/mfc1, plzcw,
    the barriers (sync, pref, cache: an empty asm with a memory clobber) and the EE kernel's
    privileged instructions (calls into the native runtime, see gt4_prelude.h). Every other
    statement (several instructions, MMI, explicit registers, asm in a #define) goes to
    tools/game_toolchain/asm_translate.py, which turns it into C over 128-bit register temporaries
    (compat/game/gt4_asm.h) when it knows all its instructions (never VU0's: the game has no VU0
    model; such functions are rewritten in game_src/); the statics it needs are declared at the top
    of the file."""
    stmts = []
    for m in ASM_START.finditer(data):
        end = _close_paren(data, m.end())
        macro = False
        if end is None:
            end, macro = _close_paren_macro(data, m.end()), True
            if end is None:
                continue
        stmts.append((m, end, macro))
    if not stmts:
        return data
    bodies = [data[m.end():end - 1].decode("latin-1") for m, end, _ in stmts]
    ctx = asm_translate.Ctx(asm_translate.scan_shadow_names(bodies))
    out, pos = [], 0
    for (m, end, macro), body in zip(stmts, bodies):
        if m.start() < pos:
            continue
        c = None if macro else _asm_c(data, m, end)
        if c is None:
            g = asm_translate.translate(body, ctx)
            if g is None:
                continue
            nl = data[m.start():end].count(b"\n")
            c = g.encode("latin-1") + (b"\\\n" if macro else b"\n") * nl
        out.append(data[pos:m.start()])
        out.append(c)
        pos = end
    out.append(data[pos:])
    data = b"".join(out)
    pro = ctx.prologue()
    if pro:
        line = b'#line 1 "%s"\n' % name.encode() if name else b""
        data = pro.encode("latin-1") + line + data
    return data


def transform(data, name=None):
    """Source rewrites done on the fly (the decomp stays untouched): every 128-bit mode(TI)
    typedef names one of the 16-byte structures of compat/game/gt4_prelude.h instead, C99
    hexadecimal floating constants become decimal ones of the same value, and the MIPS asm
    statements become C (translate_asm). name: the file's path, for #line after added lines.
    Also: C99 variadic macros (`#define F(a, ...) ... __VA_ARGS__`) take gcc 2.9's named form
    (`__VA_ARGS__...`); variables bound to a MIPS register (`register T v __asm__("$25")`) lose the
    binding, and a file-scope one bound to $28 reads the module's $gp from the runtime; an explicit
    instantiation of an operator of a class named by a typedef (`template Vec &Vec::operator=(...)`)
    is preceded by a sizeof of that class, which gcc 2.9 needs complete there."""
    if b"TI" in data:
        ti = {m.group(2): b"u" if m.group(1).startswith(b"unsigned") else b"s" for m in TI_TYPEDEF.finditer(data)}
        data = TI_TYPEDEF.sub(lambda m: b"typedef struct __gt4_%s128 %s;" % (
            b"u" if m.group(1).startswith(b"unsigned") else b"s", m.group(2)), data)
        if ti:
            # `(u128) 0`: a 128-bit zero (the structures have no constructor from int)
            names = b"|".join(re.escape(n) for n in ti)
            data = re.sub(rb"\(\s*(" + names + rb")\s*\)\s*0\b(?![.xX])",
                          lambda m: b"__gt4_zero_%s128 ()" % ti[m.group(1)], data)
    if b"." in data or b"e" in data or b"E" in data:
        data = ee_float_constants(data)
    if b"0x" in data or b"0X" in data:
        data = HEX_FLOAT.sub(hex_float_decimal, data)
    if b"__VA_ARGS__" in data:
        data = VA_DEFINE.sub(rb"\1__VA_ARGS__...)", data)
    if b"operator" in data and b"template" in data:
        data = INST_OPERATOR.sub(lambda m: b"typedef char __gt4_complete_%d[sizeof (%s)]; " % (m.start(), m.group(1))
                                 + m.group(0), data)
    if b"asm" in data:
        data = GP_GLOBAL.sub(lambda m: b"#define %s ((%s) __gt4_ee_get_gp ())" % (m.group(2), m.group(1).strip()), data)
        data = REG_VAR.sub(rb"\1", data)
        data = translate_asm(data, name)
    if b"[" in data:
        data = array_runs(data)
    return data


LOCAL_ARRAY = re.compile(rb"^([ \t]*)((?:unsigned\s+|signed\s+)?[A-Za-z_]\w*)\s+(\w+)\s*\[\s*(?:0x[0-9A-Fa-f]+|\d+)\s*\]\s*;", re.M)
NOT_CODE_BLOCK = re.compile(rb"(\b(?:struct|union|class|enum|namespace)\b[^;{}()]*|=\s*|extern\s*\"C\"\s*)$")


def _block(data, pos):
    """(start, end) of the innermost {} block around pos, or None when pos is outside every block
    or the block is a type's body or an initializer."""
    depth, i = 0, pos - 1
    while i >= 0:
        c = data[i:i + 1]
        if c == b"}":
            depth += 1
        elif c == b"{":
            if depth == 0:
                break
            depth -= 1
        i -= 1
    if i < 0 or NOT_CODE_BLOCK.search(data[max(0, i - 200):i].rstrip()):
        return None
    depth, j = 1, pos
    while j < len(data) and depth:
        c = data[j:j + 1]
        depth += {b"{": 1, b"}": -1}.get(c, 0)
        j += 1
    return i, j


def array_runs(data):
    """Adjacent local arrays of one type (`s32 buf0[4]; s32 buf1[4]; ...`, how m2c names a stack
    area it could not type) become the members of one local structure. The EE lays locals out
    upwards in declaration order, so a callee writing more than the first array (a 0x34-byte
    request into `buf0[4]`) fills the next ones, which the code may then read (buf1, buf3) or
    which are only padding; natively gcc puts the first array just below the saved registers and
    the same write overruns the frame. The structure restores the EE order; the names in the rest
    of the block become member accesses. Lines are kept, for the diagnostics."""
    decls = list(LOCAL_ARRAY.finditer(data))
    runs, run = [], []
    for m in decls:
        if run and data[run[-1].end():m.start()].strip() == b"" and m.group(2) == run[0].group(2):
            run.append(m)
            continue
        if len(run) >= 2:
            runs.append(run)
        run = [m]
    if len(run) >= 2:
        runs.append(run)
    for n, run in reversed(list(enumerate(runs))):
        block = _block(data, run[0].start())
        if block is None:
            continue
        tag = b"__gt4_run%d" % n
        names = [m.group(3) for m in run]
        end = run[-1].end()
        rest = re.sub(rb"(?<![\w.>:])(" + b"|".join(re.escape(x) for x in names) + rb")\b",
                      tag + rb".\1", data[end:block[1]])
        first, last = run[0], run[-1]
        data = (data[:first.start(2)] + b"struct { " + data[first.start(2):last.end()] + b" } " + tag + b";"
                + rest + data[block[1]:])
    return data


VA_DEFINE = re.compile(rb"^([ \t]*#[ \t]*define[ \t]+\w+\([^)\n]*?)\.\.\.\)", re.M)
GP_GLOBAL = re.compile(rb'^register\s+([^;=(]*?)\b(\w+)\s*(?:__asm__|asm)\s*\(\s*"\$(?:28|gp)"\s*\)\s*;', re.M)
INST_OPERATOR = re.compile(rb"^template\s+[^<;{}]*?\b(\w+)\s*::\s*operator\b", re.M)
REG_VAR = re.compile(rb'(\bregister\s+[^;=(){}]*?\w)\s*(?:__asm__|asm)\s*\(\s*"\$\w+"\s*\)')


def load_profiles(decomp):
    """{profile name or None: [i386 flags]} from the decomp's project.toml."""
    path = os.path.join(decomp, "project.toml")
    if tomllib is None:
        raise SystemExit("Python 3.11+ (tomllib) is needed to read project.toml")
    cfg = tomllib.load(open(path, "rb"))
    out = {None: map_flags(cfg["compiler"]["command"])}
    for name, c in cfg.get("compilers", {}).items():
        out[name] = map_flags(c["command"])
    return out


def map_flags(command):
    """i386 flags for an EE compile command (the gcc part of a cc_as.sh wrapper included)."""
    words = shlex.split(command)
    if words and words[0] == "bash":  # bash cc_as.sh "<as command>" "<gcc command>"
        words = shlex.split(words[-1])
    flags, skip = [], False
    for w in words[1:]:
        if skip:
            skip = False
            continue
        if w in ("-B", "-o"):
            skip = True
            continue
        if w in DROP or w.startswith(DROP_PREFIX) or not w.startswith("-"):
            continue
        flags.append(w)
    for f in ALWAYS:
        if f not in flags:
            flags.append(f)
    if not any(f.startswith("-O") for f in flags):
        flags.insert(0, "-O2")
    return flags


def list_sources(decomp, args):
    if args.all:
        out = subprocess.run(["git", "-C", decomp, "ls-files", "-co", "--exclude-standard", "src"],
                             capture_output=True, text=True, check=True).stdout.split("\n")
        files = [f for f in out if f.endswith((".c", ".cpp"))]
    else:
        files = list(args.files)
        if args.list:
            files += [l.strip() for l in open(args.list) if l.strip()]
        files = [os.path.relpath(os.path.abspath(os.path.join(decomp, f)) if not os.path.isabs(f) else f,
                                 decomp).replace(os.sep, "/") for f in files]
    return sorted(set(files))


RECOVERED = "recovered/"


def recovered_sources(decomp):
    """{"recovered/<src path>": (address, source bytes)}: the functions config/asm_functions.txt
    keeps as assembly although they were matched in C once (`... (was NAME.cpp)`: moved out by the
    decomp's inline-asm policy, tools/asm_policy.py). Their last matched version is read from the
    decomp's history (git show, read only); the asm translation makes them native code."""
    path = os.path.join(decomp, "config", "asm_functions.txt")
    if not os.path.exists(path):
        return {}
    wanted = {}
    for line in open(path, encoding="utf-8"):
        m = re.match(r"^([0-9a-fA-F]{8})\b.*\(was ([\w.]+\.(?:c|cpp))\)", line)
        if m:
            wanted[m.group(2)] = int(m.group(1), 16)
    if not wanted:
        return {}
    log = subprocess.run(["git", "-C", decomp, "log", "--all", "--diff-filter=D", "--format=commit %H",
                          "--name-only", "--", "src"], capture_output=True, text=True, check=True).stdout
    found, commit = {}, None
    for line in log.splitlines():
        if line.startswith("commit "):
            commit = line.split()[1]
        elif line.strip():
            name = line.strip().rsplit("/", 1)[-1]
            if name in wanted and name not in found:  # newest deletion first
                found[name] = (commit, line.strip())
    out = {}
    for name, (commit, src) in sorted(found.items()):
        data = subprocess.run(["git", "-C", decomp, "show", f"{commit}^:{src}"], capture_output=True, check=True).stdout
        out[RECOVERED + src] = (wanted[name], data)
    missing = sorted(set(wanted) - set(found))
    if missing:
        print(f"warning: {len(missing)} moved-out sources not found in the decomp's history: {missing[:5]}",
              file=sys.stderr)
    return out


def profile_of(head):
    m = MARKER.match(head.split(b"\n", 1)[0].rstrip(b"\r") + b"\n") if head else None
    return m.group(1).decode() if m else None


def add_bytes(tar, name, data):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mtime = 0
    tar.addfile(info, io.BytesIO(data))


def static_tar_members(decomp):
    """include/ and compat/game/ (as compat/), transformed: in every batch."""
    out = []
    for base, arc in ((os.path.join(decomp, "include"), "include"), (COMPAT, "compat")):
        for dirpath, dirs, files in os.walk(base):
            dirs.sort()
            for f in sorted(files):
                p = os.path.join(dirpath, f)
                rel = arc + "/" + os.path.relpath(p, base).replace(os.sep, "/")
                out.append((rel, transform(open(p, "rb").read(), rel)))
    return out


# One batch in WSL: unpack, compile every job line in parallel, tar the results to stdout.
WSL_SCRIPT = r"""
set -u
W=$(mktemp -d /tmp/gt4-game-cc.XXXXXX)
trap 'rm -rf "$W"' EXIT
cd "$W" && tar xf - -C "$W" || exit 3
mkdir -p out
GCC="{gcc}"
O="$W/out"
export GCC O
compile() {{
  id=$1; src=$2; shift 2
  start=$(date +%s.%N)
  timeout -k 5 {timeout} $GCC -S "$@" -o "$O/$id.s" "$src" 2> "$O/$id.err"
  rc=$?
  end=$(date +%s.%N)
  echo "$id $rc $start $end" > "$O/$id.rc"
}}
export -f compile
cd tree
xargs -a ../jobs.txt -P {jobs} -L 1 bash -c 'compile "$@"' _
cd ..
tar cf - out
"""


def run_batch(decomp, batch, profiles, static, args):
    """Compile one batch in WSL; return {id: (rc, seconds, stderr, asm bytes or None)}."""
    buf = io.BytesIO()
    jobs = []
    with tarfile.open(fileobj=buf, mode="w") as tar:
        for rel, data in static:
            add_bytes(tar, "tree/" + rel, data)
        for i, (rel, prof, data) in enumerate(batch):
            add_bytes(tar, "tree/" + rel, transform(data, rel))
            flags = ["-Icompat"] + profiles[prof] + ["-include", "compat/gt4_prelude.h"]
            if not rel.endswith(".c"):
                # `extern "C" int f ();` takes no arguments, as in gcc 2.96 (2.9 read it as `(...)`)
                flags.append("-fstrict-prototype")
            jobs.append(f"{i} {rel} {' '.join(flags)}")
        add_bytes(tar, "jobs.txt", ("\n".join(jobs) + "\n").encode())
    script = WSL_SCRIPT.format(gcc=GCC, timeout=args.timeout, jobs=args.jobs)
    p = subprocess.run(["wsl", "-e", "bash", "-c", script], input=buf.getvalue(), capture_output=True)
    if p.returncode != 0:
        raise SystemExit(f"wsl batch failed ({p.returncode}): {p.stderr.decode(errors='replace')[-2000:]}")
    results = {}
    with tarfile.open(fileobj=io.BytesIO(p.stdout), mode="r") as tar:
        for m in tar.getmembers():
            if not m.isfile():
                continue
            name = os.path.basename(m.name)
            i, ext = name.split(".", 1)
            r = results.setdefault(int(i), {})
            r[ext] = tar.extractfile(m).read()
    out = {}
    for i in range(len(batch)):
        r = results.get(i, {})
        rc_line = r.get("rc", b"").split()
        rc = int(rc_line[1]) if len(rc_line) > 1 else -1
        secs = float(rc_line[3]) - float(rc_line[2]) if len(rc_line) > 3 else None
        out[i] = (rc, secs, r.get("err", b"").decode("utf-8", "replace"), r.get("s") if rc == 0 else None)
    return out


ERR_LINE = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?:\d+:)?\s*(?P<msg>.*)$")


def first_error(text):
    """The first error line of gcc's output (warnings and 'In function' context skipped)."""
    lines = text.splitlines()
    for i, l in enumerate(lines):
        if "warning:" in l or l.startswith("In file included") or l.strip().startswith("from ") \
                or ": In function" in l or ": In method" in l or ": At top level" in l \
                or ": In instantiation" in l or "instantiated from" in l:
            continue
        if l.strip():
            # gcc wraps long messages: join a following indented/unprefixed line
            nxt = lines[i + 1] if i + 1 < len(lines) else ""
            if nxt and not ERR_LINE.match(nxt) and not nxt.startswith("In file") \
                    and ": In " not in nxt and ": At top level" not in nxt:
                l = l + " " + nxt.strip()
            return l.strip()
    return text.strip().splitlines()[0] if text.strip() else ""


COMPILE_CATEGORIES = [
    ("missing header", re.compile(r"No such file or directory|No include path in which to find")),
    ("MIPS inline asm", re.compile(r"unknown register name|impossible constraint|inconsistent operand constraints"
                                   r"|invalid .asm.|asm operand|output number \d+ not directly addressable"
                                   r"|can't find a register in class|invalid `asm'|Output constraint|register constraint"
                                   r"|invalid register name")),
    ("128-bit/MMI types", re.compile(r"__gt4_[us]128|\b[us]128\b|U?TItype|mode `TI'|vu_u128")),
    ("compiler crash", re.compile(r"Internal (compiler )?error|internal error|Segmentation|confused by earlier errors"
                                  r"|Abort|abort\(\)|killed")),
    ("frontend: parse error", re.compile(r"parse error|syntax error")),
    ("frontend: unknown builtin or attribute", re.compile(r"__builtin_\w+' undeclared|attribute directive ignored"
                                                         r"|`__builtin_\w+' was not declared")),
    ("frontend: templates", re.compile(r"template|specializ|instantiat")),
    ("frontend: undeclared or redeclared", re.compile(r"undeclared|not declared|redeclar|redefinition|conflicting"
                                                     r"|previous declaration|has no member|not a member")),
    ("frontend: types and conversions", re.compile(r"conver|cast|incompatible|invalid operands|lvalue|pointer"
                                                  r"|initializ|ambiguous|no match")),
]


def classify_compile(rc, msg):
    if rc in (124, 137):
        return "timeout"
    for name, rx in COMPILE_CATEGORIES:
        if rx.search(msg):
            return name
    return "frontend: other"


def app_ranges(asm_text):
    """Line ranges (1-based, inclusive) of the asm statements' text (#APP ... #NO_APP)."""
    ranges, start = [], None
    for n, line in enumerate(asm_text.splitlines(), 1):
        s = line.strip()
        if s == "/APP" or s == "#APP":
            start = n
        elif (s == "/NO_APP" or s == "#NO_APP") and start is not None:
            ranges.append((start, n))
            start = None
    return ranges


def assemble(s_path, o_path):
    p = subprocess.run([AS_EXE, "--32", "-o", o_path, s_path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if p.returncode == 0 and os.path.exists(o_path):
        return True, "", None
    msgs = [l for l in p.stderr.splitlines() if "Error" in l or "error" in l] or p.stderr.splitlines()
    first = msgs[0] if msgs else f"as exited {p.returncode}"
    cat = "assembler: compiler output"
    m = re.search(r":(\d+): Error", first)
    if m:
        line = int(m.group(1))
        text = open(s_path, encoding="utf-8", errors="replace").read()
        if any(a <= line <= b for a, b in app_ranges(text)):
            cat = "MIPS inline asm"
    try:
        os.remove(o_path)
    except OSError:
        pass
    return False, first, cat


def source_addresses(decomp):
    """{src path: address} of every function source, from the decomp's own tools/project.py
    (read only: no bytecode written), for the names a source's object exports."""
    code = ("import sys, os, json; sys.path.insert(0, os.path.join(sys.argv[1], 'tools')); import project; "
            "r = os.path.join(sys.argv[1], ''); "
            "print(json.dumps({os.path.relpath(p, r).replace(os.sep, '/'): a for a, p in project.sources().items()}))")
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    p = subprocess.run([sys.executable, "-B", "-c", code, decomp], capture_output=True, text=True, env=env, cwd=ROOT)
    if p.returncode != 0:
        print(f"warning: no address map from the decomp's tools ({p.stderr.strip()[-300:]}); "
              "objects export their file-name symbol only", file=sys.stderr)
        return {}
    return json.loads(p.stdout)


def stem_address(rel):
    """The address in a file name func_XXXXXXXX.c(pp): files outside the decomp's source map
    (tools/draft_natives.py stages kept drafts that way)."""
    m = re.match(r"func_([0-9A-Fa-f]{8})\.", os.path.basename(rel))
    return int(m.group(1), 16) if m else None


GLOBL = re.compile(rb"^\.globl[ \t]+(\S+)[ \t]*\r?\n", re.M)
DEF_EXT = re.compile(rb"(\.def[ \t]+([^;\s]+);[ \t]*\.scl[ \t]+)2(?=;)")
LINKONCE = re.compile(rb"^[ \t]*\.linkonce\b[^\n]*\n", re.M)


def localize(asm, rel, addr):
    """The decomp build's symbol policy (tools/build.py: objcopy -G STEM* -G func_ADDR*): an
    object exports only its own function; every other definition (the copies of neighbours and
    helpers that sources repeat so each one compiles alone) becomes local. Returns the new text
    and the exported names."""
    stem = os.path.splitext(os.path.basename(rel))[0].encode()
    own = [b"_" + stem] + ([b"_func_%08X" % addr] if addr is not None else [])
    declared_global = set(GLOBL.findall(asm))
    kept, dropped = [], set()

    def keep(m):
        name = m.group(1)
        if any(name.startswith(o) for o in own):
            kept.append(name.decode())
            return m.group(0)
        dropped.add(name)
        return b""
    asm = GLOBL.sub(keep, asm)
    if dropped:
        # A COFF function's `.def NAME; .scl 2` (external class) would make it global again.
        asm = DEF_EXT.sub(lambda m: m.group(1) + b"3" if m.group(2) in dropped else m.group(0), asm)
    # No section stays a COMDAT: the linker keeps one `.text$NAME` per name program-wide and drops
    # the others, and since every copy is local (each source declares its own String, iterator...
    # helpers, not always alike), calls in the objects whose copy was dropped would land on
    # nothing (a call to the next instruction: 969 of them before this).
    asm = LINKONCE.sub(b"", asm)
    if addr is not None:
        main, _ = main_function(asm, own)
        if main is not None:
            if main not in declared_global and not any(main.startswith(o) for o in own):
                # The address's code is the first function of .text: a file-static helper defined
                # before it (a debugging aid in a game_src copy) would take over the address.
                print(f"warning: {rel}: {main.decode()} is static but takes the address "
                      f"{addr:08X} (define the function itself first)", file=sys.stderr)
            alias = b"_gt4fn_%08X" % addr
            asm = asm.rstrip(b"\r\n") + b"\n.globl %s\n%s = %s\n" % (alias, alias, main)
            kept.append(alias.decode())
    return asm, kept


LABEL_DEF = re.compile(rb"^(_[A-Za-z0-9_$.]+):", re.M)
LCOMM_DEF = re.compile(rb"^[ \t]*\.lcomm[ \t]+(_[A-Za-z0-9_$.]+)", re.M)  # file-static data (bss)
ADDR_IMM = re.compile(rb"(?<![A-Za-z0-9_$.])\$(_[A-Za-z_][A-Za-z0-9_$.]*)")  # not a `$` inside a name
ADDR_LONG = re.compile(rb"^([ \t]*\.long[ \t]+)(_[A-Za-z_][A-Za-z0-9_$.]*)", re.M)


def guest_addresses(asm, exported):
    """Addresses taken as values (`$_f` immediates, `.long _f` words) name `_gt4addr_` + the symbol,
    which tools/game_link.py resolves to the PS2 address when it has one (the trampoline there
    reaches the same native code) and to the symbol itself otherwise. Function pointers then
    compare equal to the ones in the original image's data (get_eh_context in 005BC930 tests whether
    the pointer in data is still itself). Direct calls and jumps keep their host targets.
    Local definitions of this file (statics without a PS2 identity) are left alone."""
    local = ({m.group(1) for m in LABEL_DEF.finditer(asm)} | {m.group(1) for m in LCOMM_DEF.finditer(asm)}) \
        - {e.encode() for e in exported}

    def imm(m):
        return m.group(0) if m.group(1) in local else b"$_gt4addr_" + m.group(1)

    def word(m):
        return m.group(0) if m.group(2) in local else m.group(1) + b"_gt4addr_" + m.group(2)
    return ADDR_LONG.sub(word, ADDR_IMM.sub(imm, asm))


FN_DEF = re.compile(rb"^\s*\.def\s+([^;\s]+);.*\.type\s+32;", re.M)


def main_function(asm, own):
    """The function of the source's own address: in the decomp's build the object's .text is
    placed at that address, so it is the first function of the primary .text section; one named
    after the source (its file name or func_ADDR) is preferred, in case this compiler emitted a
    helper the EE compiler inlined before it."""
    functions = set(FN_DEF.findall(asm))
    section, first, current = None, None, None
    once = []  # [name, section, instructions] of the functions in .text$ sections, in order
    for line in asm.splitlines():
        s = line.strip()
        if s == b".text":
            section, current = b".text", None
        elif s in (b".data", b".bss") or s.startswith(b".section"):
            section, current = s, None
        elif s.endswith(b":") and s[:-1] in functions:
            name = s[:-1]
            current = None
            if section == b".text":
                if any(name.startswith(o) for o in own):
                    return name, None
                if first is None:
                    first = name
            elif section.startswith(b".section\t.text$") or section.startswith(b".section .text$"):
                current = [name, section, 0]
                once.append(current)
        elif current is not None and s and not s.startswith(b".") and not s.endswith(b":"):
            current[2] += 1
    if first is not None:
        return first, None
    # A source that only instantiates a template: its function is in a `.text$` section (the
    # decomp's build takes the object's first .gnu.linkonce.t section as its code). This compiler
    # inlines less, so the helpers the EE compiler inlined (allocator, base destructors...) come
    # out too, sometimes first: take a function nothing else in the object uses (the instance
    # itself), named after the source if one is, else the largest.
    if not once:
        return None, None
    named = [f for f in once if any(f[0].startswith(o) for o in own)]
    if named:
        return named[0][0], named[0][1]
    body = asm
    for f in once:  # remove each function's own label, .def and .globl lines before counting uses
        body = re.sub(rb"^[ \t]*(\.globl[ \t]+|\.def[ \t]+)?" + re.escape(f[0]) + rb"\b[^\n]*\n", b"", body, flags=re.M)
    roots = [f for f in once if not re.search(rb"(?<![A-Za-z0-9_$.])" + re.escape(f[0]) + rb"(?![A-Za-z0-9_$.])", body)]
    pick = max(roots or once, key=lambda f: f[2])
    return pick[0], pick[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="sources, relative to the decomp root (src/...)")
    ap.add_argument("--decomp", required=True, help="the gt4-decomp checkout (read only)")
    ap.add_argument("--out", required=True, help="output directory (under this worktree's build/)")
    ap.add_argument("--all", action="store_true", help="every .c/.cpp under the decomp's src/")
    ap.add_argument("--list", help="a file listing sources, one per line")
    ap.add_argument("--recovered", action="store_true",
                    help="also the matched sources the decomp moved to config/asm_functions.txt (read "
                         "from its git history; objects under obj/recovered/); implied by --all")
    ap.add_argument("--jobs", type=int, default=8, help="parallel compiles in WSL and assemblies on Windows")
    ap.add_argument("--timeout", type=int, default=60, help="seconds per compile before it is killed")
    ap.add_argument("--batch", type=int, default=1500, help="sources per WSL call")
    ap.add_argument("--keep-s", action="store_true", help="keep the .s of sources that assembled")
    ap.add_argument("--export", choices=("own", "all"), default="own",
                    help="own: each object exports only its function, as the decomp's build does "
                         "(default); all: every global the compiler emitted")
    args = ap.parse_args()

    decomp = os.path.abspath(args.decomp)
    out = os.path.abspath(args.out)
    if os.path.commonpath([out, decomp]) == decomp:
        raise SystemExit("--out must not be inside the decomp tree")
    os.makedirs(out, exist_ok=True)
    profiles = load_profiles(decomp)
    files = list_sources(decomp, args) if (args.all or args.files or args.list) else []
    recovered = recovered_sources(decomp) if (args.all or args.recovered) else {}
    files += sorted(recovered)
    if not files:
        raise SystemExit("no sources")
    static = static_tar_members(decomp)
    addresses = source_addresses(decomp) if args.export == "own" else {}
    addresses.update({rel: addr for rel, (addr, _) in recovered.items()})
    census_path = os.path.join(out, "census.jsonl")
    census = open(census_path, "w", encoding="utf-8", newline="\n")
    counts = collections.Counter()
    t0 = time.time()
    pool = concurrent.futures.ThreadPoolExecutor(args.jobs)
    for b in range(0, len(files), args.batch):
        chunk = files[b:b + args.batch]
        batch = []
        for rel in chunk:
            data = recovered[rel][1] if rel in recovered else open(os.path.join(decomp, rel), "rb").read()
            prof = profile_of(data[:200])
            if prof not in profiles:
                prof = None
            batch.append((rel, prof, data))
        res = run_batch(decomp, batch, profiles, static, args)
        records = []
        asm_jobs = {}
        for i, (rel, prof, _) in enumerate(batch):
            rc, secs, err, asm = res[i]
            rec = {"file": rel, "profile": prof or "default", "lang": "c++" if rel.endswith(".cpp") else "c",
                   "ok": False, "stage": None, "first_error": None, "category": None,
                   "compile_seconds": round(secs, 2) if secs is not None else None}
            s_path = os.path.join(out, "s", rel + ".s")
            o_path = os.path.join(out, "obj", rel + ".o")
            if os.path.exists(o_path):
                os.remove(o_path)
            if asm is not None and args.export == "own":
                asm, rec["exports"] = localize(asm, rel, addresses.get(rel) or stem_address(rel))
                asm = guest_addresses(asm, rec["exports"])
            if asm is None:
                rec["stage"] = "timeout" if rc in (124, 137) else "compile"
                rec["first_error"] = first_error(err) if rc not in (124, 137) else f"killed after {args.timeout} s"
                rec["category"] = classify_compile(rc, rec["first_error"] or "")
                os.makedirs(os.path.dirname(s_path), exist_ok=True)
                open(s_path + ".err", "w", encoding="utf-8", newline="\n").write(err)
            else:
                os.makedirs(os.path.dirname(s_path), exist_ok=True)
                open(s_path, "wb").write(asm)
                os.makedirs(os.path.dirname(o_path), exist_ok=True)
                asm_jobs[i] = pool.submit(assemble, s_path, o_path)
            records.append(rec)
        for i, fut in asm_jobs.items():
            ok, msg, cat = fut.result()
            rec = records[i]
            s_path = os.path.join(out, "s", rec["file"] + ".s")
            if ok:
                rec["ok"] = True
                if not args.keep_s:
                    os.remove(s_path)
            else:
                rec["stage"], rec["first_error"], rec["category"] = "assemble", msg, cat
        for rec in records:
            census.write(json.dumps(rec) + "\n")
            counts["ok" if rec["ok"] else rec["category"]] += 1
        census.flush()
        done = b + len(chunk)
        print(f"[{done}/{len(files)}] {time.time() - t0:.0f}s ok={counts['ok']} failed={done - counts['ok']}",
              flush=True)
    census.close()
    total = sum(counts.values())
    lines = [f"{total} sources, {counts['ok']} compiled and assembled ({100 * counts['ok'] / total:.1f}%)"]
    for k, v in counts.most_common():
        if k != "ok":
            lines.append(f"  {v:6d} {100 * v / total:5.1f}%  {k}")
    open(os.path.join(out, "census_summary.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()

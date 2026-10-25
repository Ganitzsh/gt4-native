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
import io
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import time

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
ALWAYS = ["-fno-strict-aliasing"]

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
    (re.compile(rb"^(?:daddu \$(?:28|gp),%0,\$0|move \$(?:28|gp),%0)$"), b"__gt4_ee_set_gp ((void *) ({i0}))"),
    (re.compile(rb"^sw \$(?:28|gp),%0$"), b"*(void **) &({o0}) = __gt4_ee_get_gp ()"),
    (re.compile(rb"^break(?: (\d+))?$"), rb"__gt4_ee_break (0\1)"),
]


def translate_asm(data):
    """One-instruction MIPS asm statements (the decomp's policy, tools/asm_policy.py) with a C
    meaning become C: FPU min/max/sqrt/abs/rsqrt, mtc1/mfc1, plzcw, the barriers (sync, pref,
    cache: an empty asm with a memory clobber) and the EE kernel's privileged instructions (calls
    into the native runtime, see gt4_prelude.h). VU0 and other multimedia asm is left as it is."""
    out, pos = [], 0
    for m in ASM_START.finditer(data):
        if m.start() < pos:
            continue
        end = _close_paren(data, m.end())
        if end is None:
            continue
        parts = _split_top(data[m.end():end - 1], b":")
        strings = re.findall(rb'"((?:[^"\\]|\\.)*)"', parts[0])
        if not strings or re.sub(rb'"(?:[^"\\]|\\.)*"|\s', b"", parts[0]):
            continue
        insn = b"".join(strings).replace(b"\\n", b";").replace(b"\\t", b" ")
        insn = re.sub(rb"\s*([,;()])\s*", rb"\1", insn.strip()).strip(b";")
        insn = re.sub(rb"\s+", b" ", insn)
        ops = []
        for section in parts[1:3]:
            for o in _split_top(section, b","):
                if o.strip():
                    om = _OPERAND.match(o)
                    if not om:
                        ops = None
                        break
                    ops.append(om.group(1).strip())
            if ops is None:
                break
        if ops is None:
            continue
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
                break
            if b"{" in c.replace(b"__GT4", b"") and re.search(rb"\{[oi]\d\}", c):
                break  # an operand the template needs is missing
            out.append(data[pos:m.start()])
            out.append(b"(" + c + b")" if not c.startswith(b"__asm__") else c)
            pos = end
            break
    out.append(data[pos:])
    return b"".join(out)


def transform(data):
    """Source rewrites done on the fly (the decomp stays untouched): every 128-bit mode(TI)
    typedef names one of the 16-byte structures of compat/game/gt4_prelude.h instead, C99
    hexadecimal floating constants become decimal ones of the same value, and the MIPS asm
    statements that have a C meaning become that C (translate_asm)."""
    if b"TI" in data:
        data = TI_TYPEDEF.sub(lambda m: b"typedef struct __gt4_%s128 %s;" % (
            b"u" if m.group(1).startswith(b"unsigned") else b"s", m.group(2)), data)
    if b"0x" in data or b"0X" in data:
        data = HEX_FLOAT.sub(hex_float_decimal, data)
    if b"asm" in data:
        data = translate_asm(data)
    return data


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
                out.append((rel, transform(open(p, "rb").read())))
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
            add_bytes(tar, "tree/" + rel, transform(data))
            flags = ["-Icompat"] + profiles[prof] + ["-include", "compat/gt4_prelude.h"]
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


GLOBL = re.compile(rb"^\.globl[ \t]+(\S+)[ \t]*\r?\n", re.M)
DEF_EXT = re.compile(rb"(\.def[ \t]+([^;\s]+);[ \t]*\.scl[ \t]+)2(?=;)")


def localize(asm, rel, addr):
    """The decomp build's symbol policy (tools/build.py: objcopy -G STEM* -G func_ADDR*): an
    object exports only its own function; every other definition (the copies of neighbours and
    helpers that sources repeat so each one compiles alone) becomes local. Returns the new text
    and the exported names."""
    stem = os.path.splitext(os.path.basename(rel))[0].encode()
    own = [b"_" + stem] + ([b"_func_%08X" % addr] if addr is not None else [])
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
    if addr is not None:
        main, once_section = main_function(asm, own)
        if once_section is not None:
            # The aliased copy must stay: no longer a COMDAT the linker may discard for another
            # object's copy (every copy is local anyway).
            asm = re.sub(re.escape(once_section) + rb"(\r?\n)[ \t]*\.linkonce[^\n]*\n",
                         lambda m: once_section + m.group(1), asm, count=1)
        if main is not None:
            alias = b"_gt4fn_%08X" % addr
            asm = asm.rstrip(b"\r\n") + b"\n.globl %s\n%s = %s\n" % (alias, alias, main)
            kept.append(alias.decode())
    return asm, kept


FN_DEF = re.compile(rb"^\s*\.def\s+([^;\s]+);.*\.type\s+32;", re.M)


def main_function(asm, own):
    """The function of the source's own address: in the decomp's build the object's .text is
    placed at that address, so it is the first function of the primary .text section; one named
    after the source (its file name or func_ADDR) is preferred, in case this compiler emitted a
    helper the EE compiler inlined before it."""
    functions = set(FN_DEF.findall(asm))
    section, first, first_once = None, None, None
    for line in asm.splitlines():
        s = line.strip()
        if s == b".text":
            section = b".text"
        elif s in (b".data", b".bss") or s.startswith(b".section"):
            section = s
        elif s.endswith(b":") and s[:-1] in functions:
            name = s[:-1]
            if section == b".text":
                if any(name.startswith(o) for o in own):
                    return name, None
                if first is None:
                    first = name
            elif section.startswith(b".section\t.text$") or section.startswith(b".section .text$"):
                if first_once is None or (any(name.startswith(o) for o in own)
                                          and not any(first_once[0].startswith(o) for o in own)):
                    first_once = (name, section)
    if first is not None:
        return first, None
    # A source that only instantiates a template: its function is in a link-once section (the
    # decomp's build takes the object's first .gnu.linkonce.t section as its code).
    return first_once if first_once else (None, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="sources, relative to the decomp root (src/...)")
    ap.add_argument("--decomp", required=True, help="the gt4-decomp checkout (read only)")
    ap.add_argument("--out", required=True, help="output directory (under this worktree's build/)")
    ap.add_argument("--all", action="store_true", help="every .c/.cpp under the decomp's src/")
    ap.add_argument("--list", help="a file listing sources, one per line")
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
    files = list_sources(decomp, args)
    if not files:
        raise SystemExit("no sources")
    static = static_tar_members(decomp)
    addresses = source_addresses(decomp) if args.export == "own" else {}
    census_path = os.path.join(out, "census.jsonl")
    census = open(census_path, "w", encoding="utf-8", newline="\n")
    counts = collections.Counter()
    t0 = time.time()
    pool = concurrent.futures.ThreadPoolExecutor(args.jobs)
    for b in range(0, len(files), args.batch):
        chunk = files[b:b + args.batch]
        batch = []
        for rel in chunk:
            data = open(os.path.join(decomp, rel), "rb").read()
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
                asm, rec["exports"] = localize(asm, rel, addresses.get(rel))
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

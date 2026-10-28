#!/usr/bin/env python3
"""Calls whose declared parameters disagree with the callee's definition in a way the EE tolerates and the
native build does not.

    py tools/abi_lint.py --decomp C:\\path\\to\\gt4-decomp [--game build/game] [--all]

On the EE an argument's register depends only on its kind: integers, pointers, 64-bit values and doubles
take the next of $a0-$t3, floats the next of $f12-$f19. A source that declares a callee with fewer
parameters, or with its floats and integers in another order, still matches, and the callee still reads the
right registers (a caller that does not set $a1 leaves its own $a1 there). Natively every argument has its
place on the stack: a missing argument is whatever lies there (game_src/func_004684A8.cpp crashed B-spec
races so), a float where the definition has an int is its bit pattern, a 4-byte value where it has an
8-byte one shifts the rest.

For every function address, the definition the native link uses (game_src/, else the matched source, else
the draft named in build/game/draft_natives.txt) is compared with the declarations in every other source
the link uses. A declaration is reported when, position by position over the definition's parameters,
the kind or size differs, or when it has fewer parameters than the definition (the callee reads garbage
past what was passed). Extra declared parameters are harmless (the callee ignores them; GT4_CALL_PAD keeps
their slots the caller's). Parameters of unknown type (structures by value, typedef names this script
does not know) compare as equal to anything.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FLOAT = {"float", "f32", "F32", "float32"}
WIDE = {"long", "s64", "u64", "double", "f64", "longlong", "ulonglong", "int64_t", "uint64_t", "M2C_UNK64"}
QUAD = {"u128", "s128", "__gt4_u128", "__gt4_s128", "qword"}
NARROW = {"int", "s32", "u32", "unsigned", "signed", "short", "s16", "u16", "char", "s8", "u8", "bool",
          "size_t", "uint32_t", "int32_t", "uint16_t", "int16_t", "uint8_t", "int8_t", "u_int", "uint", "BOOL",
          "enum", "M2C_UNK", "M2C_UNK8", "M2C_UNK16", "M2C_UNK32"}   # m2c's unknown types are int-sized typedefs
KEYWORDS = {"return", "if", "else", "case", "do", "while", "for", "switch", "goto", "sizeof", "new", "delete"}
NAME = re.compile(r"\bfunc_([0-9A-Fa-f]{8})\s*\(")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)) if "\n" not in m.group(0) else
                  "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def split_params(s):
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    last = "".join(cur).strip()
    if last:
        out.append(last)
    if len(out) == 1 and out[0] in ("void", ""):
        return []
    return out


def kind(param):
    """'f' float, 'w' 8-byte, 'q' 16-byte, 'i' 4-byte integer or pointer, '?' unknown, '...' varargs."""
    p = param.strip()
    if p == "...":
        return "..."
    if "(" in p:          # function pointer
        return "i"
    if "*" in p or "&" in p or "[" in p:
        return "i"
    p = re.sub(r"\b(const|volatile|register|struct|union|class|enum)\b", lambda m: "enum" if m.group(1) == "enum" else "", p)
    words = p.split()
    if not words:
        return "?"
    # drop the parameter name (the last word, unless it is a type word: `unsigned long` has no name)
    typewords = FLOAT | WIDE | QUAD | NARROW | {"long"}
    toks = words if len(words) == 1 or words[-1] in typewords else words[:-1]
    joined = "".join(toks)
    if "longlong" in joined:
        return "w"
    if any(t in FLOAT for t in toks):
        return "f"
    if any(t in QUAD for t in toks):
        return "q"
    if any(t in WIDE for t in toks):
        return "w"
    if any(t in NARROW for t in toks):
        return "i"
    if len(words) == 1 and words[0] in FLOAT:
        return "f"
    return "?"


def scan(path, text):
    """[(addr, kinds, is_definition, line)]"""
    found = []
    for m in NAME.finditer(text):
        start = m.end()
        depth, i = 1, start
        while i < len(text) and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        if depth:
            continue
        params = text[start:i - 1]
        j = i
        while True:
            rest = text[j:j + 200]
            mm = re.match(r"\s*(throw\s*\(\s*\)|__attribute__\s*\(\(.*?\)\)|__asm__\s*\([^)]*\))", rest, re.S)
            if not mm:
                break
            j += mm.end()
        tail = text[j:].lstrip()[:1]
        line_start = text.rfind("\n", 0, m.start()) + 1
        prefix = text[line_start:m.start()]
        words = re.findall(r"[A-Za-z_]\w*", prefix)
        if not words or any(w in KEYWORDS for w in words) or re.search(r"[=(,!&|+\-/%<>?:]", prefix.replace("::", "")):
            continue   # a call, not a declaration
        if tail not in ("{", ";"):
            continue
        plist = split_params(params)
        kinds = [kind(p) for p in plist]
        # The return: 'w' 64-bit, 'v' void, 'i' int or pointer, 's' a structure (returned through a hidden
        # first argument, on the EE and natively), 'f' float.
        rwords = [w for w in words if w not in ("extern", "static", "inline", "__inline__", "C", "virtual", "const",
                                                 "volatile", "struct", "class", "union", "enum", "register")]
        if "*" in prefix or "&" in prefix:
            ret = "i"
        elif not rwords or rwords == ["void"]:
            ret = "v"
        else:
            ret = kind(" ".join(rwords) + " r")
            if ret == "?":
                ret = "s"
        if ret == "s":
            kinds = ["i"] + kinds
        if tail == ";" and path.endswith(".c") and params.strip() == "":
            kinds = None   # `f()` in C: unknown parameters
        used = None
        if tail == "{":   # which parameters the body reads
            k = text.find("{", j)
            depth, e = 0, k
            while e < len(text):
                if text[e] == "{":
                    depth += 1
                elif text[e] == "}":
                    depth -= 1
                    if not depth:
                        break
                e += 1
            body = text[k:e]
            used = []
            for prm in plist:
                names = re.findall(r"[A-Za-z_]\w*", prm.split("=")[0])
                name = names[-1] if names else ""
                used.append(bool(name) and re.search(r"\b" + re.escape(name) + r"\b", body) is not None)
        if used is not None and ret == "s":
            used = [True] + used
        found.append((m.group(1).upper(), kinds, tail == "{", text.count("\n", 0, m.start()) + 1, used, ret))
    return found


LABELLED = re.compile(r"(~?[A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:throw\s*\(\s*\)\s*)?"
                      r"__asm__\s*\(\s*\"func_([0-9A-Fa-f]{8})\"\s*\)\s*;")


def scan_labelled(text):
    """Declarations bound to an address by an asm label: [(addr, kinds, line, name)]. A member function's
    `this` is its first integer argument; destructors (gcc 2's in-charge argument) are left out."""
    found = []
    depth_at = []
    depth = 0
    marks = {}
    for m in LABELLED.finditer(text):
        marks[m.start()] = m
    if not marks:
        return found
    pos = 0
    starts = sorted(marks)
    k = 0
    for i, ch in enumerate(text):
        while k < len(starts) and starts[k] == i:
            m = marks[starts[k]]
            name = m.group(1)
            if not name.startswith("~") and name not in KEYWORDS:
                kinds = [kind(p) for p in split_params(m.group(2))]
                if depth > 0:   # inside a class: the object comes first
                    kinds = ["i"] + kinds
                found.append((m.group(3).upper(), kinds, text.count("\n", 0, m.start()) + 1, name))
            k += 1
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth = max(0, depth - 1)
        if k >= len(starts):
            break
    return found


def native_sources(decomp, game):
    """{path: role} of every source the native link compiles."""
    files = {}
    out = subprocess.run(["git", "-C", decomp, "ls-files", "src"], capture_output=True, text=True).stdout
    for rel in out.split():
        if rel.endswith((".c", ".cpp")):
            files[os.path.join(decomp, rel)] = "src"
    for n in os.listdir(os.path.join(ROOT, "game_src")):
        if n.endswith((".c", ".cpp")):
            files[os.path.join(ROOT, "game_src", n)] = "game_src"
    lst = os.path.join(game, "draft_natives.txt")
    if os.path.exists(lst):
        for line in open(lst, encoding="utf-8", errors="replace"):
            parts = line.split()
            if len(parts) >= 2:
                p = parts[1].replace("\\", "/")
                files[os.path.join(decomp, p)] = "draft"
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--game", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--all", action="store_true", help="also report declarations of unknown parameters")
    a = ap.parse_args()
    files = native_sources(a.decomp, a.game)
    rank = {"game_src": 0, "src": 1, "draft": 2}
    definitions = {}     # addr -> (rank, kinds, path, line)
    declarations = collections.defaultdict(list)   # addr -> [(kinds, path, line)]
    overridden = set()
    for path, role in files.items():
        if role == "game_src":
            m = re.search(r"func_([0-9A-Fa-f]{8})", os.path.basename(path))
            if m:
                overridden.add(m.group(1).upper())
    # named sources (MGame__get_money.cpp): their address from the symbol table
    named = {}
    for line in open(os.path.join(a.decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace"):
        mm = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", line)
        if mm:
            named[mm.group(1)] = "%08X" % int(mm.group(2), 16)
    for path, role in files.items():
        base = os.path.basename(path)
        own = re.search(r"func_([0-9A-Fa-f]{8})|^([0-9a-f]{8})_", base)
        own_addr = (own.group(1) or own.group(2)).upper() if own else named.get(os.path.splitext(base)[0])
        if role == "src" and own_addr in overridden:
            continue   # replaced by game_src/
        try:
            text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            continue
        for addr, kinds, is_def, line, used, ret in scan(path, text):
            if is_def:
                if addr != own_addr:
                    continue   # a neighbour copied so the file compiles alone; its own file defines it
                cur = definitions.get(addr)
                if cur is None or rank[role] < cur[0]:
                    definitions[addr] = (rank[role], kinds, path, line, used, ret)
            elif kinds is not None:
                # only declarations the file calls (not ones whose address alone is taken)
                calls = len(re.findall(r"\bfunc_" + addr + r"\s*\(", text, re.I))
                if calls > 1:
                    declarations[addr].append((kinds, path, line, ret))
        for addr, kinds, line, name in scan_labelled(text):
            if addr == own_addr:
                continue
            if len(re.findall(r"(?<![\w~])" + re.escape(name) + r"\s*\(", text)) > 1 or \
                    re.search(r"\b" + re.escape(name) + r"\s+\w+\s*\(", text):   # a call, or a constructed object
                declarations[addr].append((kinds, path, line, "?"))
    problems = []
    for addr, decls in declarations.items():
        d = definitions.get(addr)
        if not d:
            continue
        dk, used = d[1], d[4]
        if "..." in dk:
            continue
        for kinds, path, line, ret in decls:
            if path == d[2]:
                continue
            if "..." in kinds:
                continue
            bad = []
            for i, want in enumerate(dk):
                if i >= len(kinds):
                    if any(used[i:]):   # the callee reads a parameter the caller did not pass
                        bad.append(f"arg {i + 1} ({want}) not passed, read by the callee")
                    break
                if not used[i]:
                    continue
                got = kinds[i]
                if want == "?" or got == "?":
                    continue
                if want != got:
                    bad.append(f"arg {i + 1}: {got} for {want}")
            # 64-bit results: the EE's $v0 holds all of it; natively edx:eax, and a 32-bit declaration drops edx
            if (d[5], ret) in (("w", "i"), ("i", "w")):
                bad.append(f"returns {d[5]}, declared {ret}")
            if bad:
                problems.append((addr, path, line, "".join(dk), "".join(kinds), "; ".join(bad)))
    problems.sort()
    rel = lambda p: os.path.relpath(p, a.decomp) if p.startswith(a.decomp) else os.path.relpath(p, ROOT)
    for addr, path, line, dk, kk, why in problems:
        print(f"{addr}  def({dk})  {rel(path)}:{line}  decl({kk})  {why}")
    print(f"{len(problems)} declarations disagree with the native definition "
          f"({len(definitions)} definitions, {sum(len(v) for v in declarations.values())} declarations)", file=sys.stderr)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Calls across the boundary between game code and the runtime whose types disagree in size or kind.

    py tools/hook_lint.py --decomp C:\\path\\to\\gt4-decomp [--game build/game] [--all]

Game code is compiled for the EE's data model (long is 64-bit, as long long) with -mno-fp-ret-in-387 (float
results in eax, double results in edx:eax); the runtime is ordinary i686 C++ (long is 32-bit, float and double
results in st(0)). Where one calls the other, the EE's registers never showed a disagreement that natively
gives silent wrong values: a runtime `long` result is eax and the game reads edx:eax too (9bc1531:
gt4_days_floor counted -1 days in every run for five days), a 64-bit argument where the other side takes 32
bits shifts every later one, a result in st(0) where the other side reads eax is never seen.

Both directions are checked:
  1. game -> runtime: each runtime function the game calls (a replacement `gt4_replace_<target>` of a game or
     SDK function, or a helper `gt4_*`), typed as i686 C++, against every declaration or definition of it the
     game sources make (the decomp's src/, game_src/, the drafts the link uses, compat/game; typed in the EE's
     data model) that the source calls (a declaration that only takes the address is left out);
  2. runtime -> game: each call the runtime makes into game code by address (`reinterpret_cast<F>(guest_ptr(0xA))`,
     `guest_fn<F>(0xA)`, with `using F = ...` aliases), typed as i686 C++, against the game's definition of A.
Reported: an argument of another kind or size (int / 64-bit / float / 128-bit); fewer arguments passed than
the callee reads; a result of another size or in another place (eax, edx:eax, st(0)); a value expected where
none is returned. Listed for review (not errors): runtime functions that return a structure by value to game
code (an 8-byte structure that is not 8-aligned comes back in memory from game code but in edx:eax from
MinGW). --all adds narrow results (char, short) declared on one side only. Structures by value and unknown
types compare equal to anything; simple typedefs and #defines of scalar types (mode(SI/DI/SF/DF) included)
are resolved within each file.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import abi_lint  # noqa: E402

game_kind = abi_lint.kind
game_address = abi_lint.address_of
RT_WIDE = {"int64_t", "uint64_t", "double", "s64", "u64", "f64", "int64", "uint64", "__int64", "LONGLONG",
           "ULONGLONG", "DWORD64", "QWORD"}
RT_NARROW = {"long", "DWORD", "ULONG", "LONG", "UINT", "BOOL", "size_t", "ssize_t", "uintptr_t", "intptr_t",
             "ptrdiff_t", "HANDLE"}
NARROW_RESULT = re.compile(r"\b(char|short|s8|u8|s16|u16|int8_t|uint8_t|int16_t|uint16_t|bool)\b")
SCALAR_WORDS = {"int", "unsigned", "signed", "long", "short", "char", "float", "double", "void", "const", "volatile"}
MODES = {"SI": "int", "DI": "long long", "SF": "float", "DF": "double", "QI": "char", "HI": "short", "TI": "u128"}
# Reviewed and harmless (function: why); still printed with --all.
KNOWN = {
    "gt4_replace_func_004A6140": "the result 00497020 passes on is whatever the original left in $v0; its only "
                                 "caller (00497C48) declares it void",
}


def runtime_kind(param):
    """As abi_lint.kind, for i686 C++: long is 32-bit, long long 64-bit."""
    p = param.strip()
    if p == "...":
        return "..."
    if "(" in p or "*" in p or "&" in p or "[" in p:
        return "i"
    p2 = re.sub(r"\b(const|volatile|register|struct|union|class|enum)\b", " ", p.replace("std::", ""))
    sign = re.search(r"\b(unsigned|signed)\b", p2) is not None
    p2 = re.sub(r"\b(unsigned|signed)\b", " ", p2)
    words = p2.split()
    if not words:
        return "i" if sign else "?"
    types = abi_lint.FLOAT | RT_WIDE | RT_NARROW | abi_lint.NARROW | abi_lint.QUAD | {"long"}
    toks = words if len(words) == 1 or words[-1] in types else words[:-1]
    if not toks and sign:
        return "i"
    if "longlong" in "".join(toks):
        return "w"
    if any(t in abi_lint.FLOAT for t in toks):
        return "f"
    if any(t in abi_lint.QUAD for t in toks):
        return "q"
    if any(t in RT_WIDE for t in toks):
        return "w"
    if any(t in RT_NARROW or t in abi_lint.NARROW for t in toks):
        return "i"
    if sign and not any(t in types for t in toks):
        return "i"
    return "?"


def resolve_typedefs(text):
    """Simple scalar typedefs and #defines replaced by their type, so `FLO_type x` reads as `double x`."""
    names = {}
    for m in re.finditer(r"typedef\s+([A-Za-z_][\w\s]*?)\s+(\w+)\s*(__attribute__\s*\(\(\s*(?:__)?mode(?:__)?\s*\(\s*(?:__)?(\w\w)(?:__)?\s*\)\s*\)\))?\s*;", text):
        base, name, mode = m.group(1).strip(), m.group(2), m.group(4)
        if mode in MODES:
            base = ("unsigned " if "unsigned" in base else "") + MODES[mode]
        names[name] = base
    for m in re.finditer(r"^\s*#\s*define\s+(\w+)\s+([A-Za-z_][\w ]*?)\s*$", text, re.M):
        names.setdefault(m.group(1), m.group(2).strip())
    resolved = {}
    for name in names:
        t, seen = names[name], set()
        while t in names and t not in seen:   # chains: FLO_type -> DFtype -> double
            seen.add(t)
            t = names[t]
        if t.split() and all(w in SCALAR_WORDS for w in t.split()):
            resolved[name] = t
    if not resolved:
        return text
    pat = re.compile(r"\b(" + "|".join(map(re.escape, sorted(resolved, key=len, reverse=True))) + r")\b(?!\s*\()")
    return pat.sub(lambda m: resolved[m.group(1)], text)


def scan_names(path, text, names, kind_fn):
    """abi_lint.scan for functions named in `names` (a callable: name -> key or None), with kind_fn's typing.
    Each hit also gets whether its result is narrow (char, short) and the text before its name."""
    saved_addr, saved_kind = abi_lint.address_of, abi_lint.kind
    abi_lint.address_of = names
    abi_lint.kind = kind_fn
    try:
        found = abi_lint.scan(path, text)
    finally:
        abi_lint.address_of, abi_lint.kind = saved_addr, saved_kind
    starts = [0] + [i + 1 for i, ch in enumerate(text) if ch == "\n"]
    out = []
    for key, kinds, is_def, line, used, ret, name in found:
        start = starts[line - 1] if line - 1 < len(starts) else 0
        head = text[start:text.find(name, start)]
        out.append((key, kinds, is_def, line, used, ret, name, bool(NARROW_RESULT.search(head)), head))
    return out


def in_class_body(text, pos):
    """Whether pos lies directly in a class, struct or union body (the line before its brace names it)."""
    depth = 0
    for i in range(pos - 1, -1, -1):
        if text[i] == "}":
            depth += 1
        elif text[i] == "{":
            if depth == 0:
                line = text[text.rfind("\n", 0, i) + 1:i]
                prev = text[text.rfind("\n", 0, max(0, text.rfind("\n", 0, i))) + 1:i]
                return re.search(r"\b(class|struct|union)\s+\w+[^;(){}]*$", line.strip() or prev.strip()) is not None
            depth -= 1
    return False


def runtime_files():
    out = []
    for root, _, names in os.walk(os.path.join(ROOT, "runtime")):
        for n in names:
            if n.endswith((".cpp", ".c", ".h", ".hpp")):
                out.append(os.path.join(root, n))
    return out


def result_place(ret, fp):
    """Where a result comes back: eax, edx:eax, st(0), void, memory or unknown."""
    if fp:
        return "st0"
    return {"i": "eax", "f": "eax", "w": "edx:eax", "v": "void", "s": "struct", "q": "?"}.get(ret, "?")


def compare(callee_kinds, callee_used, callee_place, caller_kinds, caller_place, caller_uses):
    """The disagreements of one call site's declaration with its callee."""
    bad = []
    for i, want in enumerate(callee_kinds):
        if i >= len(caller_kinds):
            if callee_used is None or any(callee_used[i:]):
                bad.append(f"arg {i + 1} ({want}) read by the callee, not passed")
            break
        if callee_used is not None and i < len(callee_used) and not callee_used[i]:
            continue
        got = caller_kinds[i]
        if "?" in (want, got) or want == got:
            continue
        bad.append(f"arg {i + 1}: caller {got}, callee {want}")
    if caller_uses and caller_place not in ("void", "?", "struct") and callee_place not in ("?", "struct"):
        if callee_place == "void":
            bad.append(f"result: caller reads {caller_place}, callee returns nothing")
        elif callee_place != caller_place:
            bad.append(f"result: callee returns in {callee_place}, caller reads {caller_place}")
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--game", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--all", action="store_true", help="also report narrow results (char, short) on one side")
    a = ap.parse_args()

    by_name = {}
    for line in open(os.path.join(a.decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace"):
        mm = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", line)
        if mm and "type:func" in line:
            by_name.setdefault(mm.group(1), "%08X" % int(mm.group(2), 16))
            abi_lint.FUNC_NAMES.setdefault(mm.group(1), "%08X" % int(mm.group(2), 16))

    def target_key(target):
        m = re.fullmatch(r"func_([0-9A-Fa-f]{8})", target)
        return m.group(1).upper() if m else by_name.get(target, "name:" + target)

    rel = lambda p: os.path.relpath(p, a.decomp) if p.startswith(a.decomp) else os.path.relpath(p, ROOT)

    # Runtime sources: definitions of gt4_* functions, and calls into game code by address.
    rt = {}
    calls_out = []   # (addr, kinds, place, path, line, text of the type)
    for path in runtime_files():
        try:
            raw = open(path, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        text = abi_lint.strip_comments(raw)
        for key, kinds, is_def, line, used, ret, name, narrow, head in scan_names(
                path, text, lambda n: n if n.startswith("gt4_") else None, runtime_kind):
            if not is_def or kinds is None:
                continue
            head = re.sub(r"\[\[\w+\]\]", "", head)
            if "noreturn" in head:
                ret = "v"
            k = target_key(name[len("gt4_replace_"):]) if name.startswith("gt4_replace_") else "name:" + name
            fp = re.search(r"\b(float|double)\b", head) is not None and "*" not in head and "&" not in head
            rt.setdefault(k, (kinds, ret, narrow, path, line, name, used, fp))
        aliases = {m.group(1): m.group(2) for m in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;]*\(\s*\*\s*\)\s*\([^;]*\))\s*;", text)}
        aliases.update({m.group(2): m.group(1) + "(*)(" + m.group(3) + ")" for m in
                        re.finditer(r"\btypedef\s+([^;(]*?)\(\s*\*\s*(\w+)\s*\)\s*\(([^;]*)\)\s*;", text)})
        for m in re.finditer(r"(?:reinterpret_cast\s*<\s*([^>]+?)\s*>\s*\(\s*guest_ptr\s*\(\s*0x([0-9A-Fa-f]+)\s*\)\s*\)"
                             r"|guest_fn\s*<\s*([^>]+?)\s*>\s*\(\s*0x([0-9A-Fa-f]+)\s*\))", text):
            ftype, addr = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
            ftype = aliases.get(ftype.strip(), ftype)
            fm = re.match(r"(.*?)\(\s*\*\s*\)\s*\((.*)\)\s*$", ftype.strip(), re.S)
            if not fm:
                continue
            rtype, params = fm.group(1).strip(), fm.group(2)
            kinds = [runtime_kind(p) for p in abi_lint.split_params(params)]
            rk = runtime_kind(rtype + " r") if rtype not in ("void",) else "v"
            fp = re.search(r"\b(float|double)\b", rtype) is not None and "*" not in rtype
            after = text[m.end():m.end() + 1]
            uses = rk != "v"
            calls_out.append(("%08X" % int(addr, 16), kinds, result_place(rk, fp) if uses else "void", path,
                              text.count("\n", 0, m.start()) + 1, ftype.strip()))

    # Game sources: declarations/definitions of the runtime's functions, and definitions of the game functions
    # the runtime calls.
    files = dict(abi_lint.native_sources(a.decomp, a.game))
    for n in os.listdir(os.path.join(ROOT, "compat", "game")):
        if n.endswith(".h"):
            files[os.path.join(ROOT, "compat", "game", n)] = "compat"
    for root, _, names in os.walk(os.path.join(a.decomp, "include")):   # the SDK's and the game's headers
        for n in names:
            if n.endswith((".h", ".hpp")):
                files[os.path.join(root, n)] = "include"
    overridden = {m.group(1).upper() for p, r in files.items() if r == "game_src"
                  for m in [re.search(r"func_([0-9A-Fa-f]{8})", os.path.basename(p))] if m}
    wanted_defs = {c[0] for c in calls_out}
    rank = {"game_src": 0, "src": 1, "draft": 2, "compat": 3, "include": 4}
    game_defs = {}   # addr -> (rank, kinds, ret, path, line, used)

    def game_names(name):
        if name.startswith("gt4_"):
            return "name:" + name
        addr = game_address(name)
        if addr:
            return addr
        return "name:" + name if "name:" + name in rt else None   # an SDK or libc name the runtime replaces

    game = collections.defaultdict(list)
    for path, role in files.items():
        base = os.path.basename(path)
        own = re.search(r"func_([0-9A-Fa-f]{8})", base)
        own_addr = own.group(1).upper() if own else by_name.get(os.path.splitext(base)[0])
        if role == "src" and own_addr in overridden:
            continue
        try:
            text = resolve_typedefs(abi_lint.strip_comments(open(path, encoding="utf-8", errors="replace").read()))
        except OSError:
            continue
        for key, kinds, is_def, line, used, ret, name, narrow, _ in scan_names(path, text, game_names, game_kind):
            if kinds is None:
                continue
            if is_def and key in wanted_defs and key == own_addr:
                cur = game_defs.get(key)
                if cur is None or rank[role] < cur[0]:
                    game_defs[key] = (rank[role], kinds, ret, path, line, used)
            if key not in rt:
                continue
            if key.startswith("name:") and not name.startswith("gt4_"):
                at = text.find(name, sum(len(l) + 1 for l in text.split("\n")[:line - 1]))
                if at >= 0 and in_class_body(text, at):
                    continue   # a class's method of the same name (hIO::write), not the SDK's or libc's function
            ncalls = len(re.findall(r"\b" + re.escape(name) + r"\s*\(", text))
            if not is_def and ncalls < 2 and role != "include":
                continue   # declared, never called here (its address is taken); a header's prototype serves its includers
            uses = role == "include" or re.search(r"(?:[=(,!?:+\-*/<>]|return)\s*" + re.escape(name) + r"\s*\(", text) is not None
            game[key].append((kinds, ret, narrow, path, line, is_def, uses))

    problems = []
    for key, decls in sorted(game.items()):
        rk, rret, rnarrow, rpath, rline, rname, rused, rfp = rt[key]
        if "..." in rk:
            continue
        for kinds, ret, narrow, path, line, is_def, uses in decls:
            if "..." in kinds:
                continue
            bad = compare(rk, rused, result_place(rret, rfp), kinds, result_place(ret, False), uses)
            if a.all and narrow != rnarrow and "v" not in (ret, rret):
                bad.append("narrow result on one side (char/short)")
            if bad and rname in KNOWN and not a.all:
                continue
            if bad:
                problems.append(f"game->runtime  {rname}({''.join(rk) or '-'})->{result_place(rret, rfp)} "
                                f"{rel(rpath)}:{rline}  |  {rel(path)}:{line} ({''.join(kinds) or '-'})->"
                                f"{result_place(ret, False)}  :  {'; '.join(bad)}")
    for addr, kinds, place, path, line, ftype in calls_out:
        d = game_defs.get(addr)
        if not d:
            continue
        _, gk, gret, gpath, gline, gused = d
        bad = compare(gk, gused, result_place(gret, False), kinds, place, place != "void")
        if bad:
            problems.append(f"runtime->game  {addr} as {ftype}  {rel(path)}:{line}  |  defined {rel(gpath)}:{gline} "
                            f"({''.join(gk) or '-'})->{result_place(gret, False)}  :  {'; '.join(bad)}")
    for line in problems:
        print(line)
    structs = sorted((v[5], rel(v[3]), v[4]) for k, v in rt.items() if v[1] == "s" and k in game)
    for name, path, line in structs:
        print(f"review: {name} ({path}:{line}) returns a structure by value to game code")
    print(f"{len(problems)} mismatches; {len(rt)} runtime functions, {len(game)} declared by game code "
          f"({sum(len(v) for v in game.values())} declarations); {len(calls_out)} runtime calls into game code, "
          f"{sum(1 for c in calls_out if c[0] in game_defs)} with a game definition", file=sys.stderr)


if __name__ == "__main__":
    main()

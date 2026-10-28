#!/usr/bin/env python3
"""Calls whose argument list depends on the order its arguments are evaluated in.

    py tools/evalorder_lint.py --decomp C:\\path\\to\\gt4-decomp

C leaves that order open. EE gcc 2.96 (arguments in registers) evaluates them left to right (00393D20 reads
its third argument's field before the fourth argument's call fills the struct, the later ones after), and a
matched source can depend on it; the native i386 compiler pushes them right to left and evaluates the last
first. RaceLicense::initialize called func_003E0940(d, v->fn(self, buf), buf[0], 0): the virtual fills buf,
natively buf[0] was read before it ran, and every licence result read "License B-0". The fix is to evaluate
the arguments up to the nested call, and the call, as statements of their own, in that order.

This lists the calls where one argument writes a variable (a nested call given &x or an array x, or x++,
x = ...) that another argument of the same call reads (only taking its address, &x, is not a read but is
listed too: check). It reads the sources the native link compiles
(tools/abi_lint.py native_sources): matched sources, curated game_src copies and the drafts in use.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import abi_lint  # noqa: E402

KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "do", "else", "case"}
# Callees that only read what they are given: strlen and strcmp (0057F260, 0057F238), and the STL helpers that
# take an address for its type or to read through it.
PURE = {"func_0057F260", "func_0057F238", "strlen", "strcmp", "value_type", "deref", "cstr",
        "at", "distance_type", "iterator_category", "__value_type", "__distance_type"}


def strip(text):
    text = abi_lint.strip_comments(text)
    text = re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", "", text, flags=re.M)   # preprocessor lines (macro bodies)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return re.sub(r"'(?:\\.|[^'\\])*'", "''", text)


def args_at(text, i):
    """The top-level arguments of the call whose '(' is at text[i], and the index after its ')'."""
    depth, start, out = 0, i + 1, []
    j = i
    while j < len(text):
        c = text[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                out.append(text[start:j])
                return out, j + 1
        elif c == "," and depth == 1:
            out.append(text[start:j])
            start = j + 1
        elif c == ";" and depth <= 1:
            return None, j
        j += 1
    return None, j


def written(arg, arrays):
    """Names an argument expression writes: &x or an array x given to a nested call, x++/--x, x = / x += ..."""
    out = set()
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", arg):
        if m.group(1) in KEYWORDS or m.group(1) in PURE:
            continue
        inner, _ = args_at(arg, m.end() - 1)
        for a in inner or []:
            a = a.strip()
            x = re.match(r"^\(?\s*&\s*([A-Za-z_]\w*)", a) or re.match(r"^\(?\s*\([^()]*\*\s*\)\s*&\s*([A-Za-z_]\w*)", a)
            if x:
                out.add(x.group(1))
            elif re.match(r"^[A-Za-z_]\w*$", a) and a in arrays:
                out.add(a)
    out |= set(re.findall(r"([A-Za-z_]\w*)\s*(?:\+\+|--)", arg))
    out |= set(re.findall(r"(?:\+\+|--)\s*([A-Za-z_]\w*)", arg))
    out |= set(re.findall(r"\b([A-Za-z_]\w*)\s*(?:[-+*/|&^]|<<|>>)?=(?!=)", arg))
    return out


def scan(text):
    """[(line, call text)] of the order-dependent calls in one source."""
    arrays = set(re.findall(r"\b([A-Za-z_]\w*)\s*\[\s*\w+\s*\]\s*[;=,]", text))
    found = []
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        name = m.group(1)
        if name in KEYWORDS:
            continue
        args, end = args_at(text, m.end() - 1)
        if not args or len(args) < 2:
            continue
        writes = [written(a, arrays) for a in args]
        hit = None
        for i, w in enumerate(writes):
            for x in w:
                for j, a in enumerate(args):
                    if j != i and re.search(r"\b%s\b" % re.escape(x), a):
                        hit = x
        if hit:
            line = text.count("\n", 0, m.start()) + 1
            found.append((line, hit, re.sub(r"\s+", " ", text[m.start():end])[:160]))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    a = ap.parse_args()
    files = abi_lint.native_sources(a.decomp, os.path.join(ROOT, "build", "game"))
    n = 0
    for path in sorted(files):
        try:
            text = strip(open(path, encoding="utf-8", errors="replace").read())
        except OSError:
            continue
        for line, var, call in scan(text):
            n += 1
            rel = os.path.relpath(path, a.decomp if path.startswith(a.decomp) else ROOT)
            print("%s:%d: %s written and read in one argument list: %s" % (rel, line, var, call))
    print("%d order-dependent calls" % n, file=sys.stderr)


if __name__ == "__main__":
    main()

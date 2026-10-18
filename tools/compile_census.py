#!/usr/bin/env python3
"""How much of the matched decomp source already compiles with a modern host compiler?

Syntax-only compile of each source of a gt4-decomp checkout (no object, no link) with the host
g++/gcc, as 32-bit (-m32, the PS2's pointer width) or 64-bit, using the decomp's own include
directories. The first error of each failing file is classified, so a mechanical fix (a script,
not a model) can be written per category.

    compile_census.py --decomp /mnt/e/PROJECTS/GT4 [--sample N] [--jobs 3] [--m64] [--include-set A|B]
                      [--out census.jsonl]

include sets: A = include, include/shim, include/stl (the decomp's own compat headers);
              B = include only.
Reads sources only; writes nothing in the decomp checkout. Run it on Linux (WSL).
"""
import argparse
import collections
import concurrent.futures
import json
import os
import random
import re
import subprocess
import sys

CATEGORIES = [
    ("missing header", re.compile(r"fatal error: [^\s]+: No such file")),
    ("32-bit libs missing", re.compile(r"bits/c\+\+config|gnu/stubs-32|sys/cdefs")),
    ("ps2 inline asm", re.compile(r"(unknown|invalid) (register|constraint)|impossible constraint|asm operand|unrecognized|\$vf|qmtc2|qmfc2|lqc2|sqc2|vcallms|mfc0|mtc0")),
    ("ee builtin or attribute", re.compile(r"__builtin_(ee|mips)|attribute .* ignored|mode\(TI\)|unsupported .* mode")),
    ("undeclared identifier", re.compile(r"was not declared in this scope|undeclared \(first use")),
    ("old libstdc++ v2 / stl", re.compile(r"stl_|_Alloc|__STL|basic_string|streambuf|iostream|_IO_")),
    ("cast / pointer width", re.compile(r"cast (from|to) (pointer|integer)|different size|invalid conversion from|loses precision")),
    ("type or syntax", re.compile(r"expected|invalid use|does not name a type|has no member|conflicting|redeclared|ambiguous")),
    ("linkage / extern C", re.compile(r"conflicting declaration of|C linkage|overloaded")),
]


def classify(msg):
    for name, rx in CATEGORIES:
        if rx.search(msg):
            return name
    return "other"


LONG = re.compile(r"(?<![\w.])(?<!long )(?<!long	)(unsigned\s+long|long)(?!\s+(?:long|double|int))(?![\w])")


def guest_long(text):
    """The EE's `long` is 64-bit: spell it `long long` so a 32-bit host agrees (unsigned long ->
    unsigned long long). `long long`, `long double` and `long int` are left as they are."""
    return LONG.sub(lambda m: m.group(1) + " long", text)


def includes(decomp, which):
    base = [os.path.join(decomp, "include")]
    if which == "A":
        base += [os.path.join(decomp, "include", "shim"), os.path.join(decomp, "include", "stl")]
    return sum((["-I", d] for d in base), [])


def compile_one(path, decomp, m64, which, guest32=False):
    cxx = path.endswith(".cpp")
    cmd = ["g++", "-fsyntax-only", "-fpermissive"] if cxx else ["gcc", "-fsyntax-only"]
    cmd += ["-Wpointer-to-int-cast", "-Wint-to-pointer-cast"]
    cmd += ["-std=gnu++17" if cxx else "-std=gnu99"]
    if not m64:
        cmd += ["-m32"]
    inc = includes(decomp, which)
    if guest32:
        inc = ["-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "compat", "host32")] + inc             + ["-I", os.path.dirname(path)]
        cmd += ["-x", "c++" if cxx else "c", "-"]
        stdin = guest_long(open(path, encoding="utf-8", errors="replace").read())
    else:
        cmd += [path]
        stdin = None
    cmd[1:1] = inc
    try:
        r = subprocess.run(cmd, input=stdin, capture_output=True, text=True, timeout=120, errors="replace")
    except subprocess.TimeoutExpired:
        return {"file": path, "ok": False, "first": "timeout", "category": "other"}
    hazards = len(re.findall(r"cast from pointer to integer|cast to pointer from integer", r.stderr))
    if r.returncode == 0:
        return {"file": path, "ok": True, "ptr_casts": hazards}
    err = next((l for l in r.stderr.splitlines() if "error:" in l), r.stderr.strip().splitlines()[0] if r.stderr.strip() else "")
    return {"file": path, "ok": False, "first": err[:300], "category": classify(err)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--m64", action="store_true")
    ap.add_argument("--guest32", action="store_true",
                    help="32-bit host with the EE types: `long` spelled long long, 128-bit as a struct")
    ap.add_argument("--include-set", default="A", choices=("A", "B"))
    ap.add_argument("--out", default="census.jsonl")
    a = ap.parse_args()
    files = []
    for root, _, names in os.walk(os.path.join(a.decomp, "src")):
        files += [os.path.join(root, n) for n in names if n.endswith((".c", ".cpp"))]
    files.sort()
    if a.sample:
        random.Random(1).shuffle(files)
        files = files[:a.sample]
    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex, open(a.out, "w") as out:
        for r in ex.map(lambda f: compile_one(f, a.decomp, a.m64, a.include_set, a.guest32), files):
            results.append(r)
            out.write(json.dumps(r) + "\n")
    ok = sum(r["ok"] for r in results)
    print(f"{ok}/{len(results)} compile ({100 * ok / max(1, len(results)):.1f}%) "
          f"[{'64' if a.m64 else '32'}-bit, include set {a.include_set}]")
    hz = [r for r in results if r.get("ptr_casts")]
    print(f"  of those that compile, {len(hz)} cast between pointers and integers of another width "
          f"({sum(r['ptr_casts'] for r in hz)} casts): the guest's 32-bit pointers do not survive there")
    cats = collections.Counter(r["category"] for r in results if not r["ok"])
    for name, n in cats.most_common():
        print(f"  {n:6}  {name}")


if __name__ == "__main__":
    main()

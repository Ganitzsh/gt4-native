#!/usr/bin/env python3
"""Link gt4_game.exe: the game's code compiled from gt4-decomp (objects made by tools/game_cc.py), the
native runtime (runtime/) and generated tables, on the PS2 memory map.

    py tools/game_link.py --decomp C:\\path\\to\\gt4-decomp [--objs build/game/obj] [--out build/game]

What it does:
  1. compiles runtime/*.cpp and runtime/*.s with the 32-bit MinGW toolchain (C:\\msys64\\mingw32);
  2. reads the symbols of every object (nm);
  3. generates the tables the runtime needs: every function of the original image (from the decomp's
     build/functions.csv, minus config/not_functions.txt) and every native function with its original
     address (func_XXXXXXXX names, and named functions from config/symbol_addrs.txt);
  4. defines every symbol the objects use but do not define at its PS2 address (an implicit linker
     script of PROVIDE lines): func_/D_/jtbl_ names by their hex, others from config/symbol_addrs.txt.
     Data then resolves into the loaded original image and calls into trampolines. Names it cannot
     place get a stub that reports the name and stops;
  5. links with a script that puts the guest RAM section (runtime/guest.s) at 0x00011000 and the
     program from 0x02000000 (tools/game_link.py makes it from ld's default script).
Writes LINK_REPORT.txt next to the executable.
"""
import argparse
import collections
import csv
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MINGW = os.environ.get("GT4_MINGW32", r"C:\msys64\mingw32\bin")
ADDR_NAME = re.compile(r"^_(?:func|D|jtbl|lbl|jumptable|L)_([0-9A-Fa-f]{8})$")


def tool(name):
    return os.path.join(MINGW, name + ".exe")


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
        raise SystemExit(f"failed: {os.path.basename(cmd[0])}")
    return r.stdout


def mingw_env():
    env = dict(os.environ)
    env["PATH"] = MINGW + os.pathsep + env.get("PATH", "")
    return env


def compile_runtime(out):
    rt = os.path.join(ROOT, "runtime")
    objdir = os.path.join(out, "runtime")
    os.makedirs(objdir, exist_ok=True)
    jobs = []
    for name in sorted(os.listdir(rt)):
        src = os.path.join(rt, name)
        obj = os.path.join(objdir, os.path.splitext(name)[0] + (".s.o" if name.endswith(".s") else ".o"))
        if name.endswith(".cpp"):
            cmd = [tool("g++"), "-c", "-std=c++20", "-O2", "-g", "-msse2", "-mfpmath=sse", "-Wall", "-Wextra",
                   "-I", rt, src, "-o", obj]
        elif name.endswith(".c"):
            cmd = [tool("gcc"), "-c", "-std=gnu11", "-O2", "-g", "-msse2", "-mfpmath=sse", "-Wall", "-I", rt, src,
                   "-o", obj]
        elif name.endswith(".s"):
            cmd = [tool("as"), "--32", src, "-o", obj]
        else:
            continue
        jobs.append((cmd, obj, src))

    def build(job):
        cmd, obj, src = job
        if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src) and not name_newer_headers(rt, obj):
            return obj
        run(cmd, env=mingw_env())
        return obj

    with ThreadPoolExecutor(8) as ex:
        return list(ex.map(build, jobs))


def name_newer_headers(rt, obj):
    t = os.path.getmtime(obj)
    return any(os.path.getmtime(os.path.join(rt, h)) > t for h in os.listdir(rt) if h.endswith(".h"))


def nm_symbols(objs):
    """{object: (defined globals, undefined)} with COFF names (leading underscore)."""
    defined, undefined = {}, collections.defaultdict(list)
    chunks = [objs[i:i + 400] for i in range(0, len(objs), 400)]

    def one(chunk):
        return run([tool("nm"), "-A", "--defined-only", "-g"] + chunk, env=mingw_env()), \
            run([tool("nm"), "-A", "-u"] + chunk, env=mingw_env())

    with ThreadPoolExecutor(8) as ex:
        for d_out, u_out in ex.map(one, chunks):
            for line in d_out.splitlines():
                path, _, rest = line.rpartition(":")
                parts = rest.split()
                if len(parts) == 3 and parts[1] in "TDBRCV":
                    defined.setdefault(parts[2], path)
            for line in u_out.splitlines():
                path, _, rest = line.rpartition(":")
                parts = rest.split()
                if parts and parts[0] == "U":
                    undefined[parts[1]].append(path)
    return defined, undefined


def read_symbol_addrs(decomp):
    syms = {}
    pat = re.compile(r"^\s*([^\s=;/]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;\s*(?://\s*(.*))?$")
    with open(os.path.join(decomp, "config", "symbol_addrs.txt"), encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if m:
                kind = "func" if "type:func" in (m.group(3) or "") else "data"
                syms[m.group(1)] = (int(m.group(2), 16), kind)
    return syms


def read_sdk_names(decomp):
    """{name: address} of the Sony SDK functions linked in the image (config/sdk_functions.tsv)."""
    names = {}
    p = os.path.join(decomp, "config", "sdk_functions.tsv")
    for line in open(p, encoding="utf-8", errors="replace"):
        if line.startswith("#"):
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2 and re.fullmatch(r"[0-9A-Fa-f]{8}", parts[0]):
            names.setdefault(parts[1], int(parts[0], 16))
    return names


def replacement_address(name, syms, sdk):
    """The PS2 address a runtime replacement `gt4_replace_<target>` takes over."""
    target = name[len("_gt4_replace_"):]
    m = re.fullmatch(r"func_([0-9A-Fa-f]{8})", target)
    if m:
        return int(m.group(1), 16)
    if target in sdk:
        return sdk[target]
    if target in syms:
        return syms[target][0]
    raise SystemExit(f"runtime replacement {name}: no address known for {target}")


def read_functions(decomp):
    not_fn = set()
    p = os.path.join(decomp, "config", "not_functions.txt")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            m = re.match(r"\s*(?:0x)?([0-9A-Fa-f]{8})", line)
            if m:
                not_fn.add(int(m.group(1), 16))
    fns = []
    with open(os.path.join(decomp, "build", "functions.csv"), newline="") as f:
        for row in csv.DictReader(f):
            a = int(row["address"], 16)
            if a not in not_fn:
                fns.append((a, int(row["max_size"])))
    return sorted(fns)


def c_label(name):
    """A C declaration naming the exact COFF symbol (names may hold $ or .)."""
    return f'__asm__("{name}")'


def generate(out, decomp, defined, undefined, replaced):
    gen = os.path.join(out, "gen")
    os.makedirs(gen, exist_ok=True)
    syms = read_symbol_addrs(decomp)
    fns = read_functions(decomp)
    fn_addrs = {a for a, _ in fns}

    natives = []  # (addr, coff name)
    for name in defined:
        m = ADDR_NAME.match(name)
        if m and name.startswith("_func_"):
            natives.append((int(m.group(1), 16), name))
        elif name[1:] in syms and syms[name[1:]][1] == "func":
            natives.append((syms[name[1:]][0], name))
    natives = [(a, n) for a, n in natives if a not in replaced] + sorted(replaced.items())
    natives.sort()
    with open(os.path.join(gen, "guest_functions.c"), "w") as f:
        f.write("/* generated by tools/game_link.py: the functions of the original image */\n")
        f.write("typedef struct { unsigned addr, size; } GuestFunction;\n")
        f.write(f"const unsigned gt4_guest_function_count = {len(fns)};\n")
        f.write("const GuestFunction gt4_guest_functions[] = {\n")
        for a, s in fns:
            f.write(f"{{0x{a:08X},{s}}},\n")
        f.write("};\n")
    with open(os.path.join(gen, "native_functions.c"), "w") as f:
        f.write("/* generated by tools/game_link.py: native implementations by original address */\n")
        f.write("typedef struct { unsigned addr; const void *fn; const char *name; } NativeFunction;\n")
        for i, (a, n) in enumerate(natives):
            f.write(f"extern char gt4_n{i} {c_label(n)};\n")
        f.write(f"const unsigned gt4_native_function_count = {len(natives)};\n")
        f.write("const NativeFunction gt4_native_functions[] = {\n")
        for i, (a, n) in enumerate(natives):
            f.write(f'{{0x{a:08X},&gt4_n{i},"{n[1:]}"}},\n')
        if not natives:
            f.write("{0,0,0}\n")
        f.write("};\n")

    provides, unresolved = [], []
    for name in sorted(undefined):
        if name in defined:
            continue
        m = ADDR_NAME.match(name)
        addr = int(m.group(1), 16) if m else (syms[name[1:]][0] if name[1:] in syms else None)
        if addr is None:
            unresolved.append(name)
        elif addr in replaced and (name.startswith("_func_") or syms.get(name[1:], (0, ""))[1] == "func"):
            provides.append((name, replaced[addr]))  # calls by name go straight to the replacement
        else:
            provides.append((name, addr))
    host = {n for n in unresolved if is_host_symbol(n)}
    unresolved = [n for n in unresolved if n not in host]
    with open(os.path.join(gen, "provide.ld"), "w") as f:
        f.write("/* generated by tools/game_link.py: symbols at their PS2 addresses */\n")
        for name, a in provides:
            value = f'"{a}"' if isinstance(a, str) else f"0x{a:08X}"
            f.write(f'PROVIDE("{name}" = {value});\n')
    with open(os.path.join(gen, "unresolved.c"), "w") as f:
        f.write("/* generated by tools/game_link.py: symbols with no native definition and no known address */\n")
        f.write("extern void gt4_unresolved(const char *);\n")
        for i, n in enumerate(unresolved):
            f.write(f'void gt4_u{i}(void) {c_label(n)};\nvoid gt4_u{i}(void) {{ gt4_unresolved("{n}"); }}\n')
    return fns, fn_addrs, natives, provides, unresolved, host


HOST_PREFIXES = ("__imp_", "___chkstk", "__alloca", "___divdi3", "___moddi3", "___udivdi3", "___umoddi3",
                 "___muldi3", "___ashldi3", "___ashrdi3", "___lshrdi3", "___fix", "___float", "___cmpdi2",
                 "___ucmpdi2", "___negdi2", "___main")


def is_host_symbol(name):
    return name.startswith(HOST_PREFIXES)


def link_script(out):
    """ld's default i386pe script with the guest RAM first and the program from 0x02000000."""
    text = run([tool("ld"), "--verbose", "-m", "i386pe"], env=mingw_env())
    body = text.split("==================================================")[1]
    lines = body.strip("\n").splitlines()
    for i, line in enumerate(lines):
        if line.lstrip().startswith(".text") and "__image_base__" in line:
            lines[i] = "  .guest 0x00011000 : { *(.guest) }\n  .text 0x02000000 :"
            break
    else:
        raise SystemExit("unexpected ld default script")
    path = os.path.join(out, "gen", "game.ld")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--objs", default="", help="directory of game objects (.o), searched recursively")
    ap.add_argument("--exclude", default="", help="file of object basenames to leave out (one per line)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rt_objs = compile_runtime(a.out)
    exclude = set()
    if a.exclude:
        exclude = {l.strip() for l in open(a.exclude) if l.strip() and not l.startswith("#")}
    game_objs = []
    if a.objs:
        for root, _, names in os.walk(a.objs):
            game_objs += [os.path.join(root, n) for n in names if n.endswith(".o") and n not in exclude]
    game_objs.sort()
    rt_defined, _ = nm_symbols(rt_objs)  # runtime replacements and services; its own undefined are host libraries
    syms, sdk = read_symbol_addrs(a.decomp), read_sdk_names(a.decomp)
    replaced = {}  # PS2 address -> runtime symbol
    for name in rt_defined:
        if name.startswith("_gt4_replace_"):
            replaced[replacement_address(name, syms, sdk)] = name
    # A game object that defines a replaced function is left out (the replacement wins).
    defined, undefined = nm_symbols(game_objs)
    dropped = set()
    for name, path in defined.items():
        m = ADDR_NAME.match(name)
        addr = int(m.group(1), 16) if m and name.startswith("_func_") else             (syms[name[1:]][0] if name[1:] in syms and syms[name[1:]][1] == "func" else None)
        if addr in replaced:
            dropped.add(path)
    if dropped:
        game_objs = [o for o in game_objs if o not in dropped]
        defined, undefined = nm_symbols(game_objs)
    for name, path in rt_defined.items():
        defined.setdefault(name, path)
    fns, fn_addrs, natives, provides, unresolved, host = generate(a.out, a.decomp, defined, undefined, replaced)
    gen = os.path.join(a.out, "gen")
    gen_objs = []
    for c in ("guest_functions.c", "native_functions.c", "unresolved.c"):
        o = os.path.join(gen, c[:-2] + ".o")
        run([tool("gcc"), "-c", "-O1", os.path.join(gen, c), "-o", o], env=mingw_env())
        gen_objs.append(o)
    script = link_script(a.out)
    exe = os.path.join(a.out, "gt4_game.exe")
    rsp = os.path.join(gen, "objects.rsp")
    with open(rsp, "w") as f:
        for o in game_objs + rt_objs + gen_objs:
            f.write('"' + o.replace("\\", "/") + '"\n')
    cmd = [tool("g++"), "-o", exe, "@" + rsp,
           "-Wl,-T," + script, "-Wl,--image-base=0x10000", "-Wl,--disable-dynamicbase",
           "-Wl,--section-alignment=0x1000", "-Wl,--large-address-aware", "-Wl,--stack,0x400000",
           "-static", "-static-libgcc", "-static-libstdc++", "-Wl,-Map," + os.path.join(a.out, "gt4_game.map")]
    run(cmd, env=mingw_env())
    with open(os.path.join(a.out, "LINK_REPORT.txt"), "w") as f:
        f.write(f"game objects: {len(game_objs)} ({len(dropped)} left out for runtime replacements)\n")
        f.write(f"runtime replacements: {len(replaced)}\n")
        f.write(f"native functions with an original address: {len(natives)}\n")
        f.write(f"functions in the image: {len(fns)}; without native code: {len(fn_addrs - {x for x, _ in natives})}\n")
        f.write(f"symbols placed at PS2 addresses: {len(provides)}\n")
        f.write(f"host library symbols: {len(host)}\n")
        f.write(f"unresolved (stubbed): {len(unresolved)}\n")
        for n in unresolved:
            f.write(f"  {n}\n")
    print(open(os.path.join(a.out, "LINK_REPORT.txt")).read().split("unresolved")[0], end="")
    print(f"unresolved (stubbed): {len(unresolved)}\n-> {exe}")


if __name__ == "__main__":
    main()

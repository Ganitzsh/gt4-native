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
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

# Children start without a console window: each would get a console host of its own (the machine's process
# rule, docs/modern/CAPTURES.md).
NO_WINDOW = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# FFmpeg (static, i686) for movies (runtime/native/movie.cpp): tools/build_ffmpeg.sh builds it here
# (absolute and normalised once: the -I flag and the prefix map in compile_runtime must see the same string)
FFMPEG = os.path.abspath(os.environ.get("GT4_FFMPEG", os.path.join(ROOT, "build", "ffmpeg-i686")))
FFMPEG_LIBS = ["avformat", "avcodec", "swscale", "swresample", "avutil"]
MINGW = os.environ.get("GT4_MINGW32", r"C:\msys64\mingw32\bin")
ADDR_NAME = re.compile(r"^_(?:func|D|jtbl|lbl|jumptable|L)_([0-9A-Fa-f]{8})$")


def tool(name):
    return os.path.join(MINGW, name + ".exe")


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw, **NO_WINDOW)
    if r.returncode != 0:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
        raise SystemExit(f"failed: {os.path.basename(cmd[0])}")
    return r.stdout


def chunked(items, path=lambda x: x, most=150, chars=24000):
    """items in lists of at most `most`, each list's paths at most `chars` characters together: a Windows command line
    holds 32,767, and 150 objects of a checkout in a long folder went past it (WinError 206)."""
    out, current, length = [], [], 0
    for item in items:
        n = len(path(item)) + 3   # (a space and quotes)
        if current and (len(current) >= most or length + n > chars):
            out.append(current)
            current, length = [], 0
        current.append(item)
        length += n
    if current:
        out.append(current)
    return out


_LAP = [time.time()]


def lap(what):
    """The time since the last lap, printed (where a link spends its time)."""
    now = time.time()
    print(f"  {what}: {now - _LAP[0]:.1f} s", flush=True)
    _LAP[0] = now


def mingw_env():
    env = dict(os.environ)
    env["PATH"] = MINGW + os.pathsep + env.get("PATH", "")
    return env


# Host libraries a source the game shares with the host must never include (they are not on the game's paths).
HOST_ONLY_INCLUDES = ("SDL", "vulkan", "volk", "stb_", "imgui")


def shared_sources():
    """The .cpp files the game takes from the host's folders: platform/ui (the settings pages' engine) and
    platform/audio/spu (the SPU2), only those folders themselves (platform/ui/host is the host's alone)."""
    ui = os.path.join(ROOT, "platform", "ui")
    spu = os.path.join(ROOT, "platform", "audio", "spu")
    return ([(os.path.join(ui, n), "ui_" + n) for n in sorted(os.listdir(ui)) if n.endswith(".cpp")] +
            [(os.path.join(spu, n), "spu_" + n) for n in sorted(os.listdir(spu)) if n.endswith(".cpp")])


def check_shared_sources():
    """Problems with the shared sources' includes, empty when there are none: every quoted include, followed through
    the shared headers, must be found on the game's include paths (the file's own folder, runtime/, include/,
    platform/ui/, platform/audio/), and no angle include may name a host library. A host-only file left in
    platform/ui broke every release game build from 7755399 to 1463624 (E5); this finds it before the release."""
    paths = [os.path.join(ROOT, "runtime"), os.path.join(ROOT, "include"), os.path.join(ROOT, "platform", "ui"),
             os.path.join(ROOT, "platform", "audio")]
    problems, seen = [], set()
    todo = [src for src, _ in shared_sources()]
    while todo:
        src = todo.pop()
        if src in seen:
            continue
        seen.add(src)
        with open(src, encoding="utf-8", errors="replace") as f:
            for number, line in enumerate(f, 1):
                m = re.match(r'\s*#\s*include\s*([<"])([^>"]+)[>"]', line)
                if not m:
                    continue
                where = f"{os.path.relpath(src, ROOT)}:{number}"
                if m.group(1) == "<":
                    if any(part in m.group(2) for part in HOST_ONLY_INCLUDES):
                        problems.append(f"{where}: <{m.group(2)}> is a host library")
                    continue
                found = [os.path.join(d, m.group(2)) for d in [os.path.dirname(src)] + paths]
                found = [p for p in found if os.path.isfile(p)]
                if not found:
                    problems.append(f'{where}: "{m.group(2)}" is not on the game\'s include paths')
                else:
                    todo.append(os.path.normpath(found[0]))
    return problems


def compile_runtime(out):
    problems = check_shared_sources()
    if problems:
        raise SystemExit("game_link: shared sources include host-only headers (move such files to "
                         "platform/ui/host):\n  " + "\n  ".join(problems))
    rt = os.path.join(ROOT, "runtime")
    objdir = os.path.join(out, "runtime")
    os.makedirs(objdir, exist_ok=True)
    # The checkout's path stays out of the objects (debug info: file and directory names), so the same commits give
    # the same exe in any folder (tools/release_game.py). Both spellings: the paths gcc sees use either slash.
    prefix_map = [f"-ffile-prefix-map={p}=gt4-native" for p in sorted({ROOT, ROOT.replace("\\", "/")})]
    # FFmpeg's headers too, where they are (GT4_FFMPEG: a checkout without its own build/ffmpeg-i686 uses another's),
    # under the name they have in a checkout's build folder: an FFmpeg outside the checkout put its absolute path in
    # the movie and IPU objects' debug info, and the exe changed with the folder it was built in.
    ffmpeg = FFMPEG
    prefix_map += [f"-ffile-prefix-map={p}=gt4-native/build/ffmpeg-i686" for p in sorted({ffmpeg, ffmpeg.replace("\\", "/")})]
    jobs = []
    files = []
    for dirpath, _, names in os.walk(rt):  # runtime/ and its subdirectories (runtime/native/...)
        sub = os.path.relpath(dirpath, rt)
        prefix = "" if sub == "." else sub.replace(os.sep, "_") + "_"
        files += [(os.path.join(dirpath, n), prefix + n) for n in sorted(names)]
    # The settings pages' engine (platform/ui: the page model, layout and UI records), the same sources the host
    # builds: runtime/menu runs it in the game (docs/modern/MENU_DESIGN.md 3.1); and the SPU2 (platform/audio/spu:
    # voices, reverb, the register image and GT4's status), one source with the host's mixer (PLAN.md D5). Only the
    # folders' own .cpp files (shared_sources; checked above); no SDL or GPU.
    ui = os.path.join(ROOT, "platform", "ui")
    audio = os.path.join(ROOT, "platform", "audio")
    files += shared_sources()
    for src, name in files:
        obj = os.path.join(objdir, os.path.splitext(name)[0] + (".s.o" if name.endswith(".s") else ".o"))
        if name.endswith(".cpp"):
            # -mstackrealign: game code (gcc 2.9) calls into the runtime with a 4-byte aligned stack
            # -Werror=return-type: a function that can flow off its end without a return is undefined behaviour, which GCC
            # turns into unreachable code (lane D's D14 crash, 2026-11-08: a lost "return true" ran into the next function)
            cmd = [tool("g++"), "-c", "-std=c++20", "-O2", "-g", "-msse2", "-mfpmath=sse", "-mstackrealign", "-Wall",
                   "-Wextra", "-Werror=return-type"] + prefix_map + [
                   "-I", rt, "-I", os.path.join(ROOT, "include"), "-I", ui, "-I", audio,
                   "-I", os.path.join(FFMPEG, "include"),
                   src, "-o", obj]
        elif name.endswith(".c"):
            cmd = [tool("gcc"), "-c", "-std=gnu11", "-O2", "-g", "-msse2", "-mfpmath=sse", "-Wall", "-Werror=return-type",
                   "-Werror=implicit-function-declaration"] + prefix_map + [
                   "-I", rt, "-I", os.path.join(ROOT, "include"), src, "-o", obj]
        elif name.endswith(".s"):
            cmd = [tool("as"), "--32", src, "-o", obj]
        else:
            continue
        jobs.append((cmd, obj, src))

    def build(job):
        cmd, obj, src = job
        if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(src) and not name_newer_headers(rt, obj):
            return obj
        run(cmd, env=mingw_env(), cwd=ROOT)   # the debug info's directory is ROOT too, which the prefix map covers
        return obj

    with ThreadPoolExecutor(8) as ex:
        return list(ex.map(build, jobs))


def compile_eeorig(out, gen):
    """The translated original code and its bridge (--eeorig), or an empty gt4_orig_install."""
    objdir = os.path.join(out, "eeorig")
    os.makedirs(objdir, exist_ok=True)
    if not gen:
        stub = os.path.join(objdir, "orig_stub.c")
        with open(stub, "w") as f:
            f.write("void gt4_orig_install(void) {}\n")
        obj = stub[:-2] + ".o"
        run([tool("gcc"), "-c", "-O1", stub, "-o", obj], env=mingw_env())
        return [obj]
    inc = os.path.join(ROOT, "third_party", "ps2-web-recomp", "runtime", "include")
    flags = ["-std=c++20", "-O1", "-msse2", "-mfpmath=sse", "-mstackrealign", "-fno-strict-aliasing", "-w", "-I", inc,
             "-I", gen]
    jobs = []
    for n in sorted(os.listdir(gen)):
        if n.endswith(".cpp"):
            jobs.append((os.path.join(gen, n), os.path.join(objdir, n[:-4] + ".o"), []))
    bridge = os.path.join(ROOT, "tools", "eeref", "orig_bridge.cpp")
    jobs.append((bridge, os.path.join(objdir, "orig_bridge.o"),
                 ["-I", os.path.join(ROOT, "runtime"), "-I", os.path.join(ROOT, "include")]))
    # the original code's vcallms: GT4's VU0 programs in the VU's arithmetic (a test reference; the game has none)
    jobs.append((os.path.join(ROOT, "tools", "eeref", "vu0_programs.cpp"), os.path.join(objdir, "vu0_programs.o"), []))
    for n in sorted(os.listdir(os.path.join(ROOT, "tools", "eeref"))):   # GT4_DIFFTEST's tests (difftest.h)
        if n.startswith("difftest") and n.endswith(".cpp"):
            jobs.append((os.path.join(ROOT, "tools", "eeref", n), os.path.join(objdir, n[:-4] + ".o"),
                         ["-I", os.path.join(ROOT, "runtime"), "-I", os.path.join(ROOT, "include"), "-I",
                          os.path.join(ROOT, "platform")]))
    # the host's VIF1 decoder: the tests of functions that send packets compare what the packets deliver (Test::stream)
    jobs.append((os.path.join(ROOT, "platform", "gs", "vif1.cpp"), os.path.join(objdir, "vif1.o"),
                 ["-I", os.path.join(ROOT, "include")]))

    # the bridge and the tests also depend on tools/eeref's headers, the runtime's headers they include, the record
    # layouts and the unpack formats (include/gt4) and the decoder's header
    eeref = os.path.join(ROOT, "tools", "eeref")
    headers = [os.path.join(eeref, h) for h in os.listdir(eeref) if h.endswith(".h")] + \
        [os.path.join(ROOT, "runtime", h) for h in ("shared_state.h", "guest.h", "image.h", "log.h")] + \
        [os.path.join(ROOT, "include", "gt4", h) for h in ("gfx.h", "unpack.h")] + \
        [os.path.join(ROOT, "platform", "gs", "vif1.h")]
    headers_time = max(os.path.getmtime(h) for h in headers)

    def build(job):
        src, obj, extra = job
        newest = os.path.getmtime(src)
        if os.path.dirname(src) == eeref or os.path.basename(src) == "vif1.cpp":
            newest = max(newest, headers_time)
        if not (os.path.exists(obj) and os.path.getmtime(obj) >= newest):
            run([tool("g++"), "-c"] + flags + extra + [src, "-o", obj], env=mingw_env())
        return obj

    with ThreadPoolExecutor(os.cpu_count() or 8) as ex:
        objs = list(ex.map(build, jobs))
    print(f"original code for GT4_ORIG: {len(gen and [n for n in os.listdir(gen) if n.endswith('.cpp')])} translated units")
    return objs


def name_newer_headers(rt, obj):
    # the runtime's own headers (runtime/menu's too), the page engine's (platform/ui), the SPU2's (platform/audio/spu)
    # and the shared ones (include/gt4: the record layouts both ends use)
    t = os.path.getmtime(obj)
    shared = [os.path.join(ROOT, "include", "gt4"), os.path.join(ROOT, "include", "gt4", "link"),
              os.path.join(rt, "menu"), os.path.join(ROOT, "platform", "ui"),
              os.path.join(ROOT, "platform", "audio", "spu")]
    paths = [os.path.join(rt, h) for h in os.listdir(rt) if h.endswith(".h")]
    for d in shared:
        if os.path.isdir(d):
            paths += [os.path.join(d, h) for h in os.listdir(d) if h.endswith(".h")]
    return any(os.path.getmtime(p) > t for p in paths)


_NM_CACHE = {}       # object path -> [mtime, defined [[type, name]], undefined [name]]
_NM_CACHE_FILE = None


def nm_cache_load(out):
    """The per-object nm results of earlier links (gen/nm_cache.json), reused while an object is unchanged."""
    global _NM_CACHE_FILE
    _NM_CACHE_FILE = os.path.join(out, "gen", "nm_cache.json")
    try:
        with open(_NM_CACHE_FILE) as f:
            _NM_CACHE.update(json.load(f))
    except (OSError, ValueError):
        pass


def nm_cache_save():
    if _NM_CACHE_FILE:
        os.makedirs(os.path.dirname(_NM_CACHE_FILE), exist_ok=True)
        with open(_NM_CACHE_FILE, "w") as f:
            json.dump(_NM_CACHE, f)


def nm_lines(objs, args):
    """{object: the lines `nm -A ARGS` printed for it, each without the object's path}: nm in chunks on 8 threads.
    (Not kept in tools/objcache.py: reading 127,000 kept listings, one file each, took longer than nm.)"""
    result = {}

    def one(chunk):
        return chunk, run([tool("nm"), "-A"] + list(args) + chunk, env=mingw_env())

    with ThreadPoolExecutor(8) as ex:
        for chunk, text in ex.map(one, chunked(list(objs))):
            found = {o: [] for o in chunk}
            index = {os.path.normcase(os.path.normpath(o)): o for o in chunk}
            for line in text.splitlines():
                path, _, rest = line.rpartition(":")
                o = index.get(os.path.normcase(os.path.normpath(path)))
                if o:
                    found[o].append(rest)
            result.update(found)
    return result


def nm_objects(objs):
    """{object: (defined [(type, name)], undefined [name])}, from the cache or nm."""
    result, todo = {}, []
    for o in objs:
        c = _NM_CACHE.get(o)
        m = os.path.getmtime(o)
        if c and c[0] == m:
            result[o] = (c[1], c[2])
        else:
            todo.append((o, m))
    if todo:
        paths = [o for o, _ in todo]
        defined, undefined = nm_lines(paths, ("--defined-only", "-g")), nm_lines(paths, ("-u",))
        for o, m in todo:
            d = [[p[1], p[2]] for p in (l.split() for l in defined[o]) if len(p) == 3 and p[1] in "TDBRCV"]
            u = [p[1] for p in (l.split() for l in undefined[o]) if p and p[0] == "U"]
            result[o] = (d, u)
            _NM_CACHE[o] = [m, d, u]
    return result


def bound_functions(objs):
    """{object: (alias, [names of the functions at the alias])}: each object's gt4fn_ alias (tools/game_cc.py) and
    the functions defined where it points, from objdump -t (a symbol's section and value: nm's values are per section,
    and a function at offset 0 of a .text$ section would look like the one at 0 of .text)."""
    result = {}

    def one(chunk):
        return chunk, run([tool("objdump"), "-t"] + chunk, env=mingw_env())

    sym = re.compile(r"\(sec\s+(-?\d+)\)\(fl [^)]*\)\(ty\s+([0-9a-f]+)\)\(scl\s+(\d+)\)\s+"
                     r"\(nx \d+\)\s+0x([0-9a-f]+)\s+(\S+)")
    with ThreadPoolExecutor(8) as ex:
        for chunk, text in ex.map(one, chunked(list(objs))):
            current, table = None, {}
            for line in text.splitlines():
                if line.endswith("file format pe-i386"):
                    current = next((o for o in chunk if os.path.normcase(os.path.normpath(o)) ==
                                    os.path.normcase(os.path.normpath(line.split(":     file format")[0]))), None)
                    if current:
                        table[current] = []
                    continue
                m = sym.search(line)
                if current and m:
                    table[current].append((int(m.group(1)), int(m.group(2), 16), int(m.group(4), 16), m.group(5)))
            for o, syms in table.items():
                alias = next(((s, v, n) for s, ty, v, n in syms if n.startswith("_gt4fn_")), None)
                if alias:
                    names = [n for s, ty, v, n in syms if ty == 0x20 and s == alias[0] and v == alias[1]]
                    result[o] = (alias[2], names)
    return result


def check_curated_bindings(curated_objs, others_by_name):
    """A curated object whose address is not bound by name (its function is a method the file is not named after:
    tools/game_cc.py binds the first function of .text) must bind the same function as the decomp's object it
    replaces: a helper defined before the replaced function took lane A's 004506F0 (a crash on a night run)."""
    bound = bound_functions(curated_objs)
    pairs = {}
    for o, (alias, names) in bound.items():
        stem = "_" + os.path.basename(o).split(".")[0]
        if any(n == stem or re.fullmatch(r"_func_[0-9A-Fa-f]{8}", n) for n in names):
            continue   # named after its file or its address: bound by name
        replaced = others_by_name.get(alias, [])
        if replaced:
            pairs[o] = (alias, names, replaced)
    theirs = bound_functions(sorted({r for _, _, rs in pairs.values() for r in rs}))

    def same(a, b):   # one name, or the same with gcc 2.9's signature after it (a C-linkage copy of a C++ function)
        return a == b or a.startswith(b + "__") or b.startswith(a + "__")
    problems = []
    for o, (alias, names, replaced) in sorted(pairs.items()):
        for r in replaced:
            want = theirs.get(r, (None, []))[1]
            if want and not any(same(n, w) for n in names for w in want):
                problems.append(f"{os.path.basename(o)}: {alias[1:]} goes to {', '.join(names) or '(nothing)'}, the "
                                f"decomp's source binds {', '.join(want)}: define that function first in the file")
    if problems:
        raise SystemExit("curated sources bind their address to another function than the decomp's:\n  " +
                         "\n  ".join(problems))
    return len(pairs)


def nm_symbols(objs, every=False):
    """{object: (defined globals, undefined)} with COFF names (leading underscore). every: each defined name maps
    to the list of all the objects defining it (else to the first)."""
    defined, undefined = {}, collections.defaultdict(list)
    per = nm_objects(objs)
    for o in objs:
        defs, undefs = per[o]
        for _, name in defs:
            if every:
                defined.setdefault(name, []).append(o)
            else:
                defined.setdefault(name, o)
        for name in undefs:
            undefined[name].append(o)
    return defined, undefined


HOST_LIBS = ("libmsvcrt.a", "libmingw32.a", "libmingwex.a", "libmoldname.a", "libkernel32.a", "libgcc.a",
             "libgcc_eh.a", "libstdc++.a", "libwinpthread.a", "libpthread.a", "libssp.a")
GAME_PREFIX = "gt4g_"


def host_library_symbols(out):
    """Every global name the host libraries define (cached in gen/host_syms.txt)."""
    cache = os.path.join(out, "gen", "host_syms.txt")
    if os.path.exists(cache):
        return set(open(cache).read().split())
    libdirs = [os.path.join(MINGW, "..", "i686-w64-mingw32", "lib"), os.path.join(MINGW, "..", "lib")]
    libdirs += [os.path.join(MINGW, "..", "lib", "gcc", "i686-w64-mingw32", d)
                for d in os.listdir(os.path.join(MINGW, "..", "lib", "gcc", "i686-w64-mingw32"))]
    names = set()
    for d in libdirs:
        for lib in HOST_LIBS:
            p = os.path.join(d, lib)
            if os.path.exists(p):
                r = subprocess.run([tool("nm"), "-g", "--defined-only", p], capture_output=True, text=True,
                                   env=mingw_env(), **NO_WINDOW)
                for line in r.stdout.splitlines():
                    parts = line.split()
                    if len(parts) == 3:
                        names.add(parts[2])
                        if parts[2].startswith("__imp_"):
                            names.add(parts[2][len("__imp_"):])
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    with open(cache, "w") as f:
        f.write("\n".join(sorted(names)))
    return names


def rename_collisions(out, game_objs):
    """Game objects that define a name the host libraries also define (newlib's memset, malloc...) would
    take the place of the host C runtime's functions. Such names are renamed gt4g_<name> in every game
    object (definitions and references), so the game keeps its own copy and the runtime the host's.
    Returns the new object list and the renamed names."""
    host = host_library_symbols(out)
    defined, undefined = nm_symbols(game_objs)
    # The game keeps its own copy of what manages its memory or ends it, even where it calls the
    # function without defining it natively (the call then reaches the original address).
    # So do libgcc's 64-bit integer to float conversions: game code takes float results in eax
    # (runtime/fpret.s), the host's return them on the x87 stack.
    must_own = {"_" + n for n in ("__floatdisf", "__floatdidf", "malloc", "free", "calloc", "realloc", "memalign", "sbrk", "_sbrk", "exit",
                                  "_exit", "abort", "atexit", "setjmp", "longjmp", "_setjmp")}
    clash = sorted(n for n in set(defined) | set(undefined) if n in host and (n in defined or n in must_own))
    if not clash:
        return game_objs, []
    clash_set = set(clash)
    users = set(defined[n] for n in clash if n in defined)
    for n in clash:
        users.update(undefined.get(n, []))
    mapfile = os.path.join(out, "gen", "rename.syms")
    with open(mapfile, "w") as f:
        for n in clash:
            f.write(f"{n} _{GAME_PREFIX}{n[1:]}\n")
    rdir = os.path.join(out, "gen", "renamed")
    result = []
    jobs = []
    out_abs = os.path.abspath(out)
    for o in game_objs:
        if o in users:
            # Under OUT (the usual case: OUT/obj/...), the object's path relative to OUT; elsewhere, a short digest of
            # its folder. Not its absolute path: that put the checkout's path in twice, and a checkout a few folders
            # deep (tools/release.py's worktree inside another worktree) took objects past Windows' 260 characters.
            o_abs = os.path.abspath(o)
            try:
                # (normcase both: on Windows commonpath matches any letter case but returns the first path's)
                under_out = os.path.normcase(os.path.commonpath([o_abs, out_abs])) == os.path.normcase(out_abs)
            except ValueError:   # (another drive)
                under_out = False
            if under_out:
                rel = os.path.relpath(o_abs, out_abs)
            else:
                folder = hashlib.sha1(os.path.dirname(o_abs).encode()).hexdigest()[:12]
                rel = os.path.join("elsewhere", folder, os.path.basename(o_abs))
            dst = os.path.join(rdir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            jobs.append((o, dst))
            result.append(dst)
        else:
            result.append(o)

    def one(job):
        src, dst = job
        if not (os.path.exists(dst) and os.path.getmtime(dst) >= max(os.path.getmtime(src), os.path.getmtime(mapfile))):
            run([tool("objcopy"), "--redefine-syms=" + mapfile, src, dst], env=mingw_env())

    with ThreadPoolExecutor(8) as ex:
        list(ex.map(one, jobs))
    return result, clash


def read_symbol_addrs(decomp):
    syms = {}
    pat = re.compile(r"^\s*([^\s=;/]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;\s*(?://\s*(.*))?$")
    files = [os.path.join(decomp, "config", f) for f in ("symbol_addrs.txt", "stl_symbols.txt", "resolved_symbols.txt")]
    libs = os.path.join(decomp, "config", "libs")
    if os.path.isdir(libs):  # per-library lists (libgcc, libio, newlib...)
        files += [os.path.join(libs, f) for f in sorted(os.listdir(libs)) if f.endswith("_symbols.txt")]
    for path in files:
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = pat.match(line)
                if m:
                    kind = "func" if "type:func" in (m.group(3) or "") else "data"
                    syms.setdefault(m.group(1), (int(m.group(2), 16), kind))
    # The script engine's native methods (config/adhoc_methods.txt: `Class method 0xADDR`), named
    # Class__method in the sources that register them, and GT HD's names (config/gthd_names.txt).
    p = os.path.join(decomp, "config", "adhoc_methods.txt")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8", errors="replace"):
            parts = line.split()
            if len(parts) >= 3 and not line.startswith("#") and parts[2].startswith("0x"):
                syms.setdefault(f"{parts[0]}__{parts[1]}", (int(parts[2], 16), "func"))
    p = os.path.join(decomp, "config", "gthd_names.txt")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8", errors="replace"):
            parts = line.split("\t")
            if len(parts) >= 2 and parts[0].startswith("0x"):
                syms.setdefault(parts[1], (int(parts[0], 16), "func"))
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
    # 00616F24: the static-constructor list at the end of .text (data the game reads), listed in
    # the function inventory by mistake; a trampoline there would overwrite its count word.
    not_fn = {0x00616F24}
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
    known = {a for a, _ in fns}
    fns += [(a, s) for a, s in extra_functions() if a not in known]
    return sorted(fns)


def extra_functions():
    """tools/extra_functions.txt: functions the decomp's inventory misses (address, size)."""
    path = os.path.join(ROOT, "tools", "extra_functions.txt")
    out = []
    if os.path.exists(path):
        for line in open(path):
            parts = line.split()
            if len(parts) >= 2 and not line.startswith("#"):
                out.append((int(parts[0], 16), int(parts[1])))
    return out


def export_extra_functions(out, game_objs):
    """Each function of tools/extra_functions.txt is defined only as a local symbol (the compile
    driver exports a file's own function only); make one defining object export it."""
    want = {f"_func_{a:08X}" for a, _ in extra_functions()}
    if not want:
        return game_objs
    found = {}
    lines = nm_lines(game_objs, ("--defined-only",))
    for o in game_objs:   # (the first object in link order that defines it, as nm listed them)
        for rest in lines[o]:
            parts = rest.split()
            if len(parts) == 3 and parts[1] == "t" and parts[2] in want and parts[2] not in found:
                found[parts[2]] = o
    rdir = os.path.join(out, "gen", "exported")
    result = list(game_objs)
    index = {os.path.normcase(os.path.normpath(p)): i for i, p in enumerate(result)}
    for name, path in found.items():
        i = index[os.path.normcase(os.path.normpath(path))]
        dst = os.path.join(rdir, os.path.basename(path).replace(".o", f".{name}.o"))
        os.makedirs(rdir, exist_ok=True)
        run([tool("objcopy"), f"--globalize-symbol={name}", result[i], dst], env=mingw_env())
        result[i] = dst
    return result


def c_label(name):
    """A C declaration naming the exact COFF symbol (names may hold $ or .)."""
    return f'__asm__("{name}")'


# func_/D_ names, also as gcc 2.x mangled functions (func_X__Fi) and methods (func_X__10D_00659E80if)
ADDR_IN_NAME = re.compile(r"^_?(func|D|jtbl|lbl|jumptable|L|gt4fn)_([0-9A-Fa-f]{8})(__[A-Za-z0-9_$.]*)?$")
MANGLED_PARAMS = re.compile(r"__F[A-Za-z0-9_$.]*$")
DTOR_OF_ADDR = re.compile(r"^_?_\$_\d+func_([0-9A-Fa-f]{8})$")
RTTI_NAME = re.compile(r"^_?(_vt\$|__tf|__ti)\d+([A-Za-z_][A-Za-z0-9_]*)$")
# the constructor of a class named after its constructor's address: __13func_0055F4B0 (+ params)
CTOR_OF_ADDR = re.compile(r"^_?__\d+func_([0-9A-Fa-f]{8})[A-Za-z0-9_$.]*$")


def demangle_member(name):
    """Class__Sub__method for a gcc 2.x mangled member function (method__<class><params>), where
    <class> is <len><name> or Q<count><len><name>... (Q_<count>_ from ten levels); None otherwise."""
    i = name.find("__", 1)
    while i > 0:
        method, rest = name[:i], name[i + 2:]
        parts = []
        if rest[:1] == "Q":
            m = re.match(r"Q(?:_(\d+)_|(\d))", rest)
            count, pos = (int(m.group(1) or m.group(2)), m.end()) if m else (0, 0)
        else:
            count, pos = 1, 0
        for _ in range(count):
            m = re.match(r"(\d+)", rest[pos:])
            if not m:
                break
            n = int(m.group(1))
            pos += m.end()
            parts.append(rest[pos:pos + n])
            pos += n
        if parts and len(parts) == count and all(p and re.fullmatch(r"[A-Za-z_]\w*", p) for p in parts):
            return "__".join(parts + [method])
        i = name.find("__", i + 1)
    return None


def resolve(name, syms):
    """(PS2 address, "func" or "data") for a COFF symbol name, or None.

    Accepted forms: func_/D_/jtbl_ names (with or without the COFF underscore, which asm labels
    lack), their gcc 2.x C++ mangling (func_0012ABCD__Fi), names in config/symbol_addrs.txt or
    config/stl_symbols.txt (also with a mangled parameter suffix), and the destructor of a class named
    after its own address (_$_13func_00273CA0)."""
    m = ADDR_IN_NAME.match(name)
    if m:
        return int(m.group(2), 16), "data" if m.group(1) == "D" else "func"
    bare = name[1:] if name.startswith("_") else name
    if bare.startswith(GAME_PREFIX):
        bare = bare[len(GAME_PREFIX):]
    for candidate in (bare, name, MANGLED_PARAMS.sub("", bare)):
        if candidate in syms:
            return syms[candidate]
    # names that contain "__F" themselves (GT4MC__FileGT4GameData__loadInstance__FiPv...): try every
    # "__F" as the start of the parameter suffix, the last first
    i = bare.rfind("__F")
    while i > 0:
        if bare[:i] in syms:
            return syms[bare[:i]]
        i = bare.rfind("__F", 0, i)
    m = DTOR_OF_ADDR.match(name) or CTOR_OF_ADDR.match(name)
    if m:
        return int(m.group(1), 16), "func"
    # a member function, gcc 2.x mangled: clear__Q210PitmenTiny4Team (+ parameters) is the
    # symbol map's PitmenTiny__Team__clear, init__7MCarObj is MCarObj__init
    member = demangle_member(bare)
    if member and member in syms:
        return syms[member]
    # gcc 2.x vtables, type-info functions and objects of a class: _vt$10D_00689C88, __tf7hObject...
    m = RTTI_NAME.match(name)
    if m:
        kind, cls = m.group(1), m.group(2)
        if kind == "_vt$":
            a = re.fullmatch(r"D_([0-9A-Fa-f]{8})", cls)
            if a:
                return int(a.group(1), 16), "data"  # classes named after their vtable
            for cand in (f"_vt${len(cls)}{cls}", f"{cls}__vtable"):
                if cand in syms:
                    return syms[cand][0], "data"
        elif kind == "__tf":
            for cand in (f"__tf{len(cls)}{cls}", f"{cls}__tf"):
                if cand in syms:
                    return syms[cand][0], "func"
        elif kind == "__ti":
            for cand in (f"__ti{len(cls)}{cls}", f"{cls}__ti"):
                if cand in syms:
                    return syms[cand][0], "data"
    return None


def generate(out, decomp, defined, undefined, replaced):
    gen = os.path.join(out, "gen")
    os.makedirs(gen, exist_ok=True)
    syms = read_symbol_addrs(decomp)
    fns = read_functions(decomp)
    fn_addrs = {a for a, _ in fns}

    natives = {}  # addr -> coff name (one per address; an exact func_ name wins)
    for name in sorted(defined):
        r = resolve(name, syms)
        if r and r[1] == "func" and r[0] not in replaced:
            # the object's own alias (gt4fn_, from tools/game_cc.py) wins, then an exact func_ name
            rank = 2 if name.startswith("_gt4fn_") else 1 if re.fullmatch(r"_func_[0-9A-Fa-f]{8}", name) else 0
            if r[0] not in natives or rank > natives[r[0]][1]:
                natives[r[0]] = (name, rank)
    natives = {a: n for a, (n, _) in natives.items()}
    natives = sorted(list(natives.items()) + list(replaced.items()))
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

    host_syms = host_library_symbols(out)
    provides, unresolved, host_bound = [], [], []
    for name in sorted(undefined):
        if name in defined:
            continue
        if name.startswith("_gt4addr_"):
            # an address taken as a value (tools/game_cc.py guest_addresses): the PS2 address when
            # the symbol has one, else the symbol itself
            orig = name[len("_gt4addr_"):]
            r = resolve(orig, syms)
            if r:
                provides.append((name, r[0]))
            elif orig in defined or orig in host_syms:
                provides.append((name, orig))
            else:
                unresolved.append(name)
            continue
        if name in host_syms:
            host_bound.append(name)
            continue  # memcpy, strlen...: the host's (game copies of such names were renamed gt4g_)
        r = resolve(name, syms)
        if r is None:
            unresolved.append(name)
        elif r[0] in replaced and r[1] == "func":
            provides.append((name, replaced[r[0]]))  # calls by name go straight to the replacement
        else:
            provides.append((name, r[0]))
    host = {n for n in unresolved if is_host_symbol(n)}
    unresolved = [n for n in unresolved if n not in host]
    # Unresolved names get addresses in the never-mapped first 64 KB (16 bytes apart from
    # UNRESOLVED_BASE): any call or data access faults there, and runtime/crash.cpp names the symbol.
    if len(unresolved) * 16 + UNRESOLVED_BASE > 0x10000:
        raise SystemExit(f"{len(unresolved)} unresolved symbols: more than the unmapped page range holds")
    with open(os.path.join(gen, "provide.ld"), "w") as f:
        f.write("/* generated by tools/game_link.py: symbols at their PS2 addresses */\n")
        for i, n in enumerate(unresolved):
            f.write(f'PROVIDE("{n}" = 0x{UNRESOLVED_BASE + 16 * i:08X});\n')
        for name, a in provides:
            value = f'"{a}"' if isinstance(a, str) else f"0x{a:08X}"
            f.write(f'PROVIDE("{name}" = {value});\n')
    with open(os.path.join(gen, "unresolved.c"), "w") as f:
        f.write("/* generated by tools/game_link.py: symbols with no native definition and no known address */\n")
        f.write(f"const unsigned gt4_unresolved_base = 0x{UNRESOLVED_BASE:X}, gt4_unresolved_count = {len(unresolved)};\n")
        f.write("const char *const gt4_unresolved_names[] = {\n")
        for n in unresolved:
            f.write(f'"{n}",\n')
        f.write('""};\n')
    return fns, fn_addrs, natives, provides, unresolved, host, host_bound


UNRESOLVED_BASE = 0x1000

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
        # The symbols placed at PS2 addresses (outside SECTIONS: plain assignments).
        f.write(open(os.path.join(out, "gen", "provide.ld")).read())
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check-shared", action="store_true", help="only check the sources shared with the host "
                    "(platform/ui, platform/audio/spu) for host-only includes, and exit (ctest's game_link_shared)")
    ap.add_argument("--decomp")
    ap.add_argument("--objs", default="", help="directory of game objects (.o), searched recursively")
    ap.add_argument("--objlist", default="", help="file listing the game objects to link (instead of --objs)")
    ap.add_argument("--exclude", default="", help="file of object basenames to leave out (one per line)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game"))
    ap.add_argument("--eeorig", default="", help="eerecomp's output (tools/eeref/build.cmd: build/eeref/genh, the "
                    "hook-wrapped code): link the original code, translated, for GT4_ORIG "
                    "(tools/eeref/orig_bridge.cpp)")
    ap.add_argument("--strict-drafts", action="store_true", help="stop, instead of leaving it out, on a provisional "
                    "draft at an address a matched source provides (tools/release_game.py)")
    a = ap.parse_args()
    if a.check_shared:
        problems = check_shared_sources()
        for p in problems:
            print(p)
        print(f"game_link: {len(shared_sources())} shared sources, {len(problems)} problem(s)")
        sys.exit(1 if problems else 0)
    if not a.decomp:
        ap.error("--decomp is required")
    os.makedirs(a.out, exist_ok=True)
    nm_cache_load(a.out)
    rt_objs = compile_runtime(a.out) + compile_eeorig(a.out, a.eeorig)
    lap("runtime compiled")
    exclude = set()
    if a.exclude:
        exclude = {l.strip() for l in open(a.exclude) if l.strip() and not l.startswith("#")}
    game_objs = []
    if a.objs:
        for root, _, names in os.walk(a.objs):
            game_objs += [os.path.join(root, n) for n in names if n.endswith(".o") and n not in exclude]
    if a.objlist:
        game_objs = [l.strip() for l in open(a.objlist) if l.strip()]
    game_objs.sort()
    # Curated native sources (game_src/, staged as build/native_curated) replace any other object
    # defining the same function.
    curated_objs = [o for o in game_objs if "native_curated" in o.replace("\\", "/")]
    if curated_objs:
        cur_defined, _ = nm_symbols(curated_objs)
        cur_fns = {n for n in cur_defined if n.startswith("_gt4fn_")}
        others = [o for o in game_objs if o not in curated_objs]
        other_defined, _ = nm_symbols(others, every=True)   # all of them: a source and a leftover draft may both
        replaced_objs = {p for n, paths in other_defined.items() if n in cur_fns for p in paths}
        replaced_by = {n: [p for p in ps if "native_drafts" not in p.replace("\\", "/")]
                       for n, ps in other_defined.items() if n in cur_fns}
        checked = check_curated_bindings(curated_objs, replaced_by)
        print(f"{checked} curated sources not named after their function: each binds the decomp's function")
        if replaced_objs:
            print(f"{len(replaced_objs)} objects replaced by curated native sources (game_src/)")
            game_objs = [o for o in game_objs if o not in replaced_objs]
    # Provisional drafts (tools/draft_natives.py) give way to any other native code of the same function. "The
    # same" is by address: the other may define it under its own name only (the runtime's __floatdisf is
    # 0057F480), and a gt4fn_ alias outranks a name in the address table, so a check of the aliases alone let the
    # draft take the address.
    drafts = [o for o in game_objs if "native_drafts" in o.replace("\\", "/")]
    if drafts:
        syms = read_symbol_addrs(a.decomp)

        def function_addresses(objs):
            per = nm_objects(objs)
            found = collections.defaultdict(set)
            for o in objs:
                for kind, name in per[o][0]:
                    r = resolve(name, syms) if kind == "T" else None
                    if r and r[1] == "func":
                        found[o].add(r[0])
            return found

        # the runtime's own versions count too (runtime/fpret.s: gt4g___floatdisf), not only matched sources
        regular = function_addresses([o for o in game_objs if o not in drafts] + rt_objs)
        taken = set().union(*regular.values()) if regular else set()
        stale = {o: sorted(addrs & taken) for o, addrs in function_addresses(drafts).items() if addrs & taken}
        if stale and a.strict_drafts:
            raise SystemExit(f"{len(stale)} provisional drafts are at addresses a matched source provides (stage the "
                             f"drafts again: tools/draft_natives.py):\n  " + "\n  ".join(
                                 f"{os.path.basename(o)}: {' '.join(f'{x:08X}' for x in xs)}"
                                 for o, xs in sorted(stale.items())))
        if stale:
            print(f"{len(stale)} provisional drafts left out: their functions now compile from source")
            game_objs = [o for o in game_objs if o not in stale]
    game_objs = export_extra_functions(a.out, game_objs)
    lap("objects chosen (nm)")
    game_objs, renamed = rename_collisions(a.out, game_objs)
    if renamed:
        print(f"renamed {len(renamed)} game symbols that clash with host libraries (e.g. "
              f"{', '.join(n[1:] for n in renamed[:6])})")
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
        r = resolve(name, syms)
        if r and r[1] == "func" and r[0] in replaced:
            dropped.add(path)
    if dropped:
        game_objs = [o for o in game_objs if o not in dropped]
        defined, undefined = nm_symbols(game_objs)
    for name, path in rt_defined.items():
        defined.setdefault(name, path)
    fns, fn_addrs, natives, provides, unresolved, host, host_bound = generate(a.out, a.decomp, defined, undefined, replaced)
    gen = os.path.join(a.out, "gen")
    gen_objs = []
    for c in ("guest_functions.c", "native_functions.c", "unresolved.c"):
        o = os.path.join(gen, c[:-2] + ".o")
        run([tool("gcc"), "-c", "-O1", os.path.join(gen, c), "-o", o], env=mingw_env())
        gen_objs.append(o)
    lap("tables generated and compiled")
    script = link_script(a.out)
    exe = os.path.join(a.out, "gt4_game.exe")
    rsp = os.path.join(gen, "objects.rsp")
    # A marker between the game's code and the runtime's, for reports (image.cpp: native_name_for).
    marker_c = os.path.join(gen, "game_text_end.c")
    with open(marker_c, "w") as f:
        f.write("void gt4_game_text_end(void) {}\n")
    marker_o = marker_c[:-2] + ".o"
    run([tool("gcc"), "-c", "-O1", marker_c, "-o", marker_o], env=mingw_env())
    with open(rsp, "w") as f:
        for o in game_objs + [marker_o] + rt_objs + gen_objs:
            f.write('"' + o.replace("\\", "/") + '"\n')
    cmd = [tool("g++"), "-o", exe, "@" + rsp,
           "-Wl,-T," + script, "-Wl,--image-base=0x10000", "-Wl,--disable-dynamicbase",
           "-Wl,--section-alignment=0x1000", "-Wl,--large-address-aware", "-Wl,--stack,0x400000",
           "-Wl,--no-insert-timestamp",   # the same inputs give the same file (tools/release_game.py)
           "-static", "-static-libgcc", "-static-libstdc++", "-Wl,-Map," + os.path.join(a.out, "gt4_game.map"),
           "-L" + os.path.join(FFMPEG, "lib")] + ["-l" + n for n in FFMPEG_LIBS] + ["-lbcrypt"]
    if a.eeorig:  # the translated code's RAM and scratchpad are the guest's, at their PS2 addresses
        cmd += ["-Wl,--defsym,__ZN3ps25g_ramE=0", "-Wl,--defsym,__ZN3ps25g_sprE=0x70000000"]
    nm_cache_save()
    run(cmd, env=mingw_env())
    lap("linked")
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
        f.write(f"bound to the host C runtime: {len(host_bound)}\n")
        for n in host_bound:
            f.write(f"  {n}\n")
        missing = sorted(fn_addrs - {x for x, _ in natives})
        f.write(f"without native code (runtime/missing.cpp stops on a call): {len(missing)}\n")
        for x in missing:
            f.write(f"  {x:08X}\n" if isinstance(x, int) else f"  {x}\n")
    print(open(os.path.join(a.out, "LINK_REPORT.txt")).read().split("unresolved")[0], end="")
    print(f"unresolved (stubbed): {len(unresolved)}\n-> {exe}")


if __name__ == "__main__":
    main()

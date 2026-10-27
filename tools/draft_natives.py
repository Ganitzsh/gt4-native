#!/usr/bin/env python3
"""Provisional native code for functions that have no matched source, from the decomp's drafts.

    py tools/draft_natives.py --decomp C:\\path\\to\\gt4-decomp [--game build/game]

For every function of the original image without native code in the last link
(build/game/gen/native_functions.c, plus the ones this tool provided last time), candidates are,
in order:
  1. game_src/func_ADDR.c|cpp in this repository (curated: drafts fixed by hand or written for
     the port);
  2. the decomp's attempt store (drafts/ADDR/): judged drafts differing in at most half of their
     instructions, best difference class first (registers, order, branch-form, length, other),
     then the smallest share of differing instructions;
  3. the workers' kept drafts (<decomp>/build/swarm/*/ADDR_best.c|cpp, newest first);
  4. any other judged draft of the store, closest first.
The first candidate is staged as <decomp>/build/native_drafts/func_ADDR.ext (with its compiler
marker) and compiled with tools/game_cc.py into build/game/obj; when it does not compile, the
next candidate is tried (up to 4 rounds). tools/game_link.py then uses the objects, and leaves out
any draft whose function also compiles from a matched source.

A draft differs from the original in some instructions: usually register allocation or scheduling,
sometimes more. It is provisional native code, so the game can run on; matching the function in
the decomp replaces it. Writes build/game/draft_natives.txt: address, source, kind, differ/of.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLASS_RANK = {"registers": 0, "order": 1, "branch-form": 2, "length": 3, "other": 4}


def store_drafts(store, addr):
    """Judged drafts of the store: [(key, path, row)], best first."""
    log = os.path.join(store, addr, "log.jsonl")
    out = []
    if not os.path.exists(log):
        return out
    seen = set()
    for line in open(log, encoding="utf-8"):
        try:
            r = json.loads(line)
        except ValueError:
            continue
        if r.get("event") != "try" or r.get("verdict") != "differs" or r.get("sha") in seen:
            continue
        differ, of = r.get("differ"), r.get("of")
        if not isinstance(differ, int) or not of:
            continue
        path = os.path.join(store, addr, f"{addr}_{r['sha']}.{r.get('ext', 'c')}")
        if os.path.exists(path):
            seen.add(r["sha"])
            out.append(((CLASS_RANK.get(r.get("class"), 5), differ / of), path, r))
    return sorted(out, key=lambda t: t[0])


def candidates(addr, store, swarm, curated):
    c = []
    if addr in curated:
        c.append((curated[addr], None, "curated", "0/0"))
    drafts = store_drafts(store, addr)
    for key, path, r in drafts:
        if key[1] <= 0.5:
            c.append((path, r.get("compiler"), r.get("class"), f"{r['differ']}/{r['of']}"))
    best = sorted(glob.glob(os.path.join(swarm, "*", f"{addr}_best.c*")) +
                  glob.glob(os.path.join(swarm, "*", f"{addr.upper()}_best.c*")),
                  key=os.path.getmtime, reverse=True)
    for path in best:
        c.append((path, None, "worker-best", "?"))
    for key, path, r in drafts:
        if key[1] > 0.5:
            c.append((path, r.get("compiler"), r.get("class"), f"{r['differ']}/{r['of']}"))
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--game", default=os.path.join(ROOT, "build", "game"))
    a = ap.parse_args()
    gen = os.path.join(a.game, "gen")
    image = re.findall(r"\{0x([0-9A-F]{8}),\d+\}", open(os.path.join(gen, "guest_functions.c")).read())
    natives = set(re.findall(r"\{0x([0-9A-F]{8}),&gt4_n", open(os.path.join(gen, "native_functions.c")).read()))
    manifest = os.path.join(a.game, "draft_natives.txt")
    drafted = set()
    if os.path.exists(manifest):
        drafted = {l.split()[0].upper() for l in open(manifest) if l.strip() and not l.startswith("#")}
    # the last run's curated sources count as provided by this tool too (one removed from game_src/
    # since then falls back to a draft)
    last_curated = os.path.join(a.game, "curated_list.txt")
    if os.path.exists(last_curated):
        drafted |= set(re.findall(r"FUNC_([0-9A-F]{8})\.", open(last_curated).read().upper()))
    missing = [x.lower() for x in image if x not in natives or x in drafted]
    store = os.path.join(a.decomp, "drafts")
    swarm = os.path.join(a.decomp, "build", "swarm")
    curated = {}
    gs = os.path.join(ROOT, "game_src")
    if os.path.isdir(gs):
        for n in os.listdir(gs):
            m = re.fullmatch(r"func_([0-9A-Fa-f]{8})\.(c|cpp)", n)
            if m:
                curated[m.group(1).lower()] = os.path.join(gs, n)
    stage = os.path.join(a.decomp, "build", "native_drafts")
    for d in (stage, os.path.join(a.decomp, "build", "native_curated")):
        if os.path.isdir(d):
            shutil.rmtree(d)
        os.makedirs(d)
    for d in ("native_drafts", "native_curated"):
        old = os.path.join(a.game, "obj", "build", d)
        if os.path.isdir(old):
            shutil.rmtree(old)
    # Curated sources (game_src/) are compiled for every address they name, matched or not:
    # tools/game_link.py lets them replace any other object defining the function (native-only
    # corrections of matched sources, docs/NATIVE_GAME.md).
    if curated:
        cstage = os.path.join(a.decomp, "build", "native_curated")
        crels = []
        for addr, path in sorted(curated.items()):
            shutil.copy(path, os.path.join(cstage, os.path.basename(path)))
            crels.append(f"build/native_curated/{os.path.basename(path)}")
        lst = os.path.join(a.game, "curated_list.txt")
        with open(lst, "w") as f:
            f.write("\n".join(crels) + "\n")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "game_cc.py"), "--decomp", a.decomp,
                        "--out", a.game, "--list", lst], check=False, stdout=subprocess.DEVNULL)
        print(f"{len(crels)} curated sources compiled (game_src/)")
    missing = [x for x in missing if x not in curated]
    pending = {addr: candidates(addr, store, swarm, curated) for addr in missing}
    pending = {k: v for k, v in pending.items() if v}
    chosen = {}
    census = os.path.join(a.game, "census.jsonl")
    keep = census + ".keep"
    if os.path.exists(census):
        shutil.copy(census, keep)
    for round_ in range(4):
        rels = []
        for addr, cands in pending.items():
            if not cands:
                continue
            path, compiler, kind, diff = cands.pop(0)
            ext = os.path.splitext(path)[1]
            text = open(path, encoding="utf-8", errors="replace").read()
            if compiler and not text.lstrip().startswith("/* compiler:"):
                text = f"/* compiler: {compiler} */\n" + text
            for e in (".c", ".cpp"):  # a previous round's candidate of the other language
                p = os.path.join(stage, f"func_{addr.upper()}{e}")
                if os.path.exists(p):
                    os.remove(p)
            name = f"func_{addr.upper()}{ext}"
            with open(os.path.join(stage, name), "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            rels.append(f"build/native_drafts/{name}")
            chosen[addr] = (os.path.relpath(path, a.decomp if path.startswith(a.decomp) else ROOT), kind, diff, rels[-1])
        if not rels:
            break
        lst = os.path.join(a.game, "draft_list.txt")
        with open(lst, "w") as f:
            f.write("\n".join(rels) + "\n")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "game_cc.py"), "--decomp", a.decomp,
                        "--out", a.game, "--list", lst], check=False, stdout=subprocess.DEVNULL)
        failed = set()
        for line in open(census, encoding="utf-8"):
            r = json.loads(line)
            if not r.get("ok"):
                failed.add(r["file"])
        ok = 0
        for addr in list(pending):
            if addr in chosen and chosen[addr][3] in rels:
                if chosen[addr][3] in failed:
                    del chosen[addr]
                else:
                    ok += 1
                    del pending[addr]
        print(f"round {round_ + 1}: {len(rels)} staged, {ok} compiled")
        pending = {k: v for k, v in pending.items() if v}
    if os.path.exists(keep):
        shutil.copy(census, os.path.join(a.game, "census.drafts.jsonl"))
        shutil.move(keep, census)
    print(f"{len(missing)} functions without matched native code; provisional code for {len(chosen)}")
    with open(manifest, "w") as f:
        f.write("# address source kind differ/of (provisional native code)\n")
        for addr in sorted(chosen):
            src, kind, diff, _ = chosen[addr]
            f.write(f"{addr} {src} {kind} {diff}\n")


if __name__ == "__main__":
    main()

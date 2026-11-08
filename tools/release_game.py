#!/usr/bin/env python3
"""Build gt4_game.exe for a release: from an empty folder, from committed sources only, with a stamp of every input.

    py tools/release_game.py --decomp C:\\path\\to\\gt4-decomp [--drafts DIR [--draft-list FILE]]
                             [--out build/game_release]

An incremental build folder (build/game) keeps objects of sources that have changed since, and misses sources
added since: the 0.1.1 candidate's first build lacked two game_src corrections and had 19 objects whose source had
changed. A release game is therefore always built here, from nothing:

  1. refuses uncommitted or untracked changes in the inputs: the decomp's src/, include/, config/, project.toml
     and this repository's game_src/, compat/, runtime/, include/ and the compile/link tools;
  2. empties OUT, compiles every decomp source (game_cc.py --all), stages game_src/ afresh as
     build/release_stage/native_curated_release in this checkout and compiles it;
  3. stages the provisional drafts for this build itself: links once without drafts, so the functions without
     native code are the ones these commits leave, and runs tools/draft_natives.py on that link into an empty
     native_drafts_release. A folder staged earlier holds drafts of functions matched since
     (rc-0.1.1c and d took 417 from 2026-10-31's staging: 58 were left out at the link, and one took the address
     of the runtime's __floatdisf). --drafts DIR takes a given set instead (a release's sources/native_drafts, to
     rebuild it). The drafts come from the decomp's draft store (drafts/, build/swarm/), which git does not
     version, so the stamp records the set and its hash. Both are staged in this checkout's
     build/release_stage/ (game_cc.py --stage), never in the decomp: two checkouts build at once (they shared
     DECOMP/build/native_*_release and emptied each other's), and the decomp is only read;
  4. links with --strict-drafts: a draft at an address that a matched source or the runtime provides stops the
     build. Neither a time stamp nor the checkout's path ends up in the file (game_link.py), so the same inputs
     give the same exe in any folder;
  5. copies the curated and draft sources it compiled to OUT/sources/ and writes OUT/BUILD-STAMP.json: both
     commits, the SHA-256 of every curated and draft source, the draft set's SHA-256 and how it was staged, the
     drafts list (where each draft came from), the sources that did not compile, the exe's SHA-256, and what the
     object cache gave (object_cache: objects compiled or taken from it).

Objects come from a machine-wide content-addressed cache when the same inputs made them before
(tools/objcache.py: GT4_OBJ_CACHE=DIR, or =0 to build without it), so a build of commits that change a handful of
sources compiles only those. The exe is the same with or without the cache.

tools/package_game.py --release takes the game from OUT only, and refuses it when the stamp does not match the exe
or the trees have changed since (BUILD-INFO.txt and the game-sources zip are made from the stamp).
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

# Children start without a console window: each would get a console host of its own (the machine's process
# rule, docs/modern/CAPTURES.md).
NO_WINDOW = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP_INPUTS = ["src", "include", "config", "project.toml"]
NATIVE_INPUTS = ["game_src", "compat", "runtime", "platform/ui", "platform/audio/spu", "include", "tools/game_cc.py",
                 "tools/game_link.py", "tools/draft_natives.py", "tools/game_toolchain", "tools/objcache.py"]


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True, check=True, **NO_WINDOW).stdout


def dirty(repo, paths):
    """Uncommitted or untracked changes under paths (git status lines)."""
    return [l for l in git(repo, "status", "--porcelain", "--untracked-files=all", "--", *paths).splitlines() if l]


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def stage(src, dst):
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    names = sorted(n for n in os.listdir(src) if n.endswith((".c", ".cpp")))
    for n in names:
        shutil.copy2(os.path.join(src, n), os.path.join(dst, n))
    return names


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    start = time.time()
    subprocess.run(cmd, cwd=ROOT, check=True, **NO_WINDOW)
    print(f"  ({time.time() - start:.0f} s)", flush=True)


def failed_sources(census):
    out = []
    for line in open(census, encoding="utf-8"):
        try:
            r = json.loads(line)
        except ValueError:
            continue
        if not r.get("ok"):
            out.append(r["file"])
    return sorted(out)


def cache_summary(path):
    """What tools/objcache.py gave this build (game_cc.py's lines in OUT/object_cache.jsonl): objects compiled or
    taken from the cache. None when the cache was off."""
    if not os.path.isfile(path):
        return None
    total = {}
    for line in open(path, encoding="utf-8"):
        for k, v in json.loads(line).items():
            total[k] = total.get(k, 0) + v
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--drafts", help="a given set of provisional drafts (a release's sources/native_drafts) instead "
                    "of staging them from the draft store for these commits")
    ap.add_argument("--draft-list", help="with --drafts: draft_natives.py's list of them (a release's DRAFTS.txt)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game_release"))
    ap.add_argument("--jobs", default="8")
    a = ap.parse_args()
    decomp = os.path.abspath(a.decomp)
    out = os.path.abspath(a.out)
    drafts = os.path.abspath(a.drafts) if a.drafts else None

    problems = [f"gt4-decomp: {l}" for l in dirty(decomp, DECOMP_INPUTS)]
    problems += [f"gt4-native: {l}" for l in dirty(ROOT, NATIVE_INPUTS)]
    if problems:
        raise SystemExit("uncommitted changes in the game's inputs (commit or remove them first):\n  "
                         + "\n  ".join(problems[:40]))
    if drafts and not os.path.isdir(drafts):
        raise SystemExit(f"no drafts folder {drafts}")
    if os.path.normcase(out) in (os.path.normcase(os.path.join(ROOT, "build", "game")), os.path.normcase(ROOT)):
        raise SystemExit(f"refusing to empty {out}")

    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    stamp = {
        "decomp_commit": git(decomp, "rev-parse", "HEAD").strip(),
        "native_commit": git(ROOT, "rev-parse", "HEAD").strip(),
        "built": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    }

    run([sys.executable, "tools/game_cc.py", "--decomp", decomp, "--out", out, "--all", "--jobs", a.jobs])
    os.replace(os.path.join(out, "census.jsonl"), os.path.join(out, "census_src.jsonl"))

    # (named build/native_*_release/... as if in the decomp, read from here: the objects, the census and the link are
    # the same as when they were staged there)
    stage_root = os.path.join(ROOT, "build", "release_stage")
    cc_stage = ["--stage", stage_root]
    curated_dir = os.path.join(stage_root, "native_curated_release")
    drafts_dir = os.path.join(stage_root, "native_drafts_release")
    curated = stage(os.path.join(ROOT, "game_src"), curated_dir)
    listing = os.path.join(out, "extra.txt")
    with open(listing, "w", newline="\n") as f:
        for n in curated:
            f.write(f"build/native_curated_release/{n}\n")
    link = [sys.executable, "tools/game_link.py", "--decomp", decomp, "--objs", os.path.join(out, "obj"), "--out", out]
    draft_list = os.path.join(out, "draft_natives.txt")
    shutil.rmtree(drafts_dir, ignore_errors=True)
    if drafts:
        draft_names = stage(drafts, drafts_dir)
        with open(listing, "a", newline="\n") as f:
            for n in draft_names:
                f.write(f"build/native_drafts_release/{n}\n")
        if a.draft_list:
            shutil.copy(a.draft_list, draft_list)
        stamp["drafts_staged"] = f"given: {drafts}"
    run([sys.executable, "tools/game_cc.py", "--decomp", decomp, "--out", out, "--list", listing, "--jobs", a.jobs] + cc_stage)
    os.replace(os.path.join(out, "census.jsonl"), os.path.join(out, "census_extra.jsonl"))
    if not drafts:
        run(link)   # without drafts: the functions these commits leave without native code
        run([sys.executable, "tools/draft_natives.py", "--decomp", decomp, "--game", out, "--stage-suffix", "_release"] + cc_stage)
        # a candidate that did not compile stays staged without an object: not part of the build
        objs = os.path.join(out, "obj", "build", "native_drafts_release")
        draft_names = []
        for n in sorted(os.listdir(drafts_dir)):
            if os.path.isfile(os.path.join(objs, n + ".o")):
                draft_names.append(n)
            else:
                os.remove(os.path.join(drafts_dir, n))
        stamp["drafts_staged"] = ("by tools/draft_natives.py for these commits, from the decomp's draft store "
                                  "(drafts/, build/swarm/: not versioned)")
    run(link + ["--strict-drafts"])

    snap = os.path.join(out, "sources")
    shutil.copytree(curated_dir, os.path.join(snap, "native_curated"))
    shutil.copytree(drafts_dir, os.path.join(snap, "native_drafts"))
    stamp["curated"] = {n: sha256(os.path.join(curated_dir, n)) for n in curated}
    stamp["drafts"] = {n: sha256(os.path.join(drafts_dir, n)) for n in draft_names}
    # the set in one hash ("name sha" lines, sorted): two builds took the same drafts when it is equal
    stamp["drafts_set_sha256"] = hashlib.sha256("".join(
        f"{n} {d}\n" for n, d in sorted(stamp["drafts"].items())).encode()).hexdigest()
    stamp["draft_list"] = (open(draft_list, encoding="utf-8").read().splitlines()
                           if os.path.isfile(draft_list) else None)
    stamp["not_compiled"] = failed_sources(os.path.join(out, "census_src.jsonl")) + \
        failed_sources(os.path.join(out, "census_extra.jsonl"))
    stamp["game_sha256"] = sha256(os.path.join(out, "gt4_game.exe"))
    stamp["object_cache"] = cache_summary(os.path.join(out, "object_cache.jsonl"))
    with open(os.path.join(out, "BUILD-STAMP.json"), "w", newline="\n") as f:
        json.dump(stamp, f, indent=1)
        f.write("\n")
    shutil.rmtree(curated_dir, ignore_errors=True)
    shutil.rmtree(drafts_dir, ignore_errors=True)
    print(f"gt4_game.exe {stamp['game_sha256']}\n  decomp {stamp['decomp_commit']}\n  native {stamp['native_commit']}\n"
          f"  {len(curated)} curated, {len(draft_names)} drafts, {len(stamp['not_compiled'])} sources not compiled "
          f"(their functions come from game_src/, drafts or nowhere: see LINK_REPORT.txt)")


if __name__ == "__main__":
    main()

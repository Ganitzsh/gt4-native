#!/usr/bin/env python3
"""Build gt4_game.exe for a release: from an empty folder, from committed sources only, with a stamp of every input.

    py tools/release_game.py --decomp C:\\path\\to\\gt4-decomp [--drafts DIR] [--draft-list FILE]
                             [--out build/game_release]

An incremental build folder (build/game) keeps objects of sources that have changed since, and misses sources
added since: the 0.1.1 candidate's first build lacked two game_src corrections and had 19 objects whose source had
changed. A release game is therefore always built here, from nothing:

  1. refuses uncommitted or untracked changes in the inputs: the decomp's src/, include/, config/, project.toml
     and this repository's game_src/, compat/, runtime/, include/ and the compile/link tools;
  2. empties OUT, compiles every decomp source (game_cc.py --all), stages game_src/ afresh as
     DECOMP/build/native_curated_release and the provisional drafts (--drafts, default DECOMP/build/native_drafts:
     the folder tools/draft_natives.py staged for the tested builds) as DECOMP/build/native_drafts_release,
     compiles both and links (game_link.py: no time stamp in the file, so the same inputs give the same exe);
  3. copies the curated and draft sources it compiled to OUT/sources/ and writes OUT/BUILD-STAMP.json: both
     commits, the SHA-256 of every curated and draft source, the drafts list (--draft-list, default
     build/game/draft_natives.txt: where each draft came from), the sources that did not compile, and the exe's
     SHA-256.

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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP_INPUTS = ["src", "include", "config", "project.toml"]
NATIVE_INPUTS = ["game_src", "compat", "runtime", "include", "tools/game_cc.py", "tools/game_link.py",
                 "tools/game_toolchain"]


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True, check=True).stdout


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
    subprocess.run(cmd, cwd=ROOT, check=True)


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--drafts", help="the staged provisional drafts (default DECOMP/build/native_drafts)")
    ap.add_argument("--draft-list", default=os.path.join(ROOT, "build", "game", "draft_natives.txt"),
                    help="draft_natives.py's list of the staged drafts (where each came from)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game_release"))
    ap.add_argument("--jobs", default="8")
    a = ap.parse_args()
    decomp = os.path.abspath(a.decomp)
    out = os.path.abspath(a.out)
    drafts = os.path.abspath(a.drafts or os.path.join(decomp, "build", "native_drafts"))

    problems = [f"gt4-decomp: {l}" for l in dirty(decomp, DECOMP_INPUTS)]
    problems += [f"gt4-native: {l}" for l in dirty(ROOT, NATIVE_INPUTS)]
    if problems:
        raise SystemExit("uncommitted changes in the game's inputs (commit or remove them first):\n  "
                         + "\n  ".join(problems[:40]))
    if not os.path.isdir(drafts):
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

    curated_dir = os.path.join(decomp, "build", "native_curated_release")
    drafts_dir = os.path.join(decomp, "build", "native_drafts_release")
    curated = stage(os.path.join(ROOT, "game_src"), curated_dir)
    draft_names = stage(drafts, drafts_dir)
    listing = os.path.join(out, "extra.txt")
    with open(listing, "w", newline="\n") as f:
        for n in curated:
            f.write(f"build/native_curated_release/{n}\n")
        for n in draft_names:
            f.write(f"build/native_drafts_release/{n}\n")
    run([sys.executable, "tools/game_cc.py", "--decomp", decomp, "--out", out, "--list", listing, "--jobs", a.jobs])
    os.replace(os.path.join(out, "census.jsonl"), os.path.join(out, "census_extra.jsonl"))
    run([sys.executable, "tools/game_link.py", "--decomp", decomp, "--objs", os.path.join(out, "obj"), "--out", out])

    snap = os.path.join(out, "sources")
    shutil.copytree(curated_dir, os.path.join(snap, "native_curated"))
    shutil.copytree(drafts_dir, os.path.join(snap, "native_drafts"))
    stamp["curated"] = {n: sha256(os.path.join(curated_dir, n)) for n in curated}
    stamp["drafts"] = {n: sha256(os.path.join(drafts_dir, n)) for n in draft_names}
    stamp["draft_list"] = (open(a.draft_list, encoding="utf-8").read().splitlines()
                           if os.path.isfile(a.draft_list) else None)
    stamp["not_compiled"] = failed_sources(os.path.join(out, "census_src.jsonl")) + \
        failed_sources(os.path.join(out, "census_extra.jsonl"))
    stamp["game_sha256"] = sha256(os.path.join(out, "gt4_game.exe"))
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

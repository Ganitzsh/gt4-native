#!/usr/bin/env python3
"""The hash memo's live gates: scripted headless sessions, each with its own save copy and env, logs kept.

    py -I memo_sessions.py --wt WORKTREE --host HOST --game GAME --out DIR NAME[:K=V,...] ...

NAME is race, garage or photo; K=V pairs go into the game's environment. One host at a time; each session has a
deadline and its host is stopped by PID. Run under tools/slot.py (a heavy slot).
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import time

MAIN = r"C:\Users\julie\gt4-native-game"
ORIG = r"C:\Users\julie\gt4-decomp\orig\SCUS-97328"


def tap(button, wait):
    return [f"button {button} 1", "vwait 6", f"button {button} 0", f"vwait {wait}"]


def route(name, wt):
    if name == "race":
        lines = open(os.path.join(wt, "docs", "modern", "routes", "race_gt_pe2_drive.txt")).read().split("\n")
        return [l.strip() for l in lines if l.strip()], os.path.join(MAIN, "build", "package", "saves-pe-race2", "odin-saves-pe-race2.tgz")
    if name == "garage":   # CAPTURES.md, garage_r6's presses
        r = (["vwait 1400"] + tap("start", 600) + tap("cross", 240) + tap("dpdown", 60) + tap("dpdown", 90)
             + tap("cross", 1500) + ["quit"])
        return r, os.path.join(MAIN, "build", "r5_saves")
    if name == "photo":    # tools/photo_check.py's route, then the photo view held
        r = (["vwait 1400"] + tap("start", 600) + tap("triangle", 180) + tap("dpleft", 60) * 2 + tap("dpup", 60) * 3
             + tap("cross", 300) + tap("cross", 600) + tap("dpdown", 40) + tap("dpright", 40) * 4
             + ["capture screen"] + tap("cross", 600) + ["capture photo", "vwait 1800", "capture photo2", "quit"])
        return r, os.path.join(MAIN, "build", "r5_saves")
    raise SystemExit(f"unknown session {name}")


def run(a, spec, index):
    name, _, envs = spec.partition(":")
    extra = dict(kv.split("=", 1) for kv in envs.split(",") if kv)
    commands, saves_from = route(name, a.wt)
    if not os.path.exists(saves_from):
        raise SystemExit(f"no save {saves_from}")
    tag = f"{index:02d}_{name}" + "".join(f"_{k}{v}" for k, v in extra.items())
    work = os.path.join(a.out, tag)
    shutil.rmtree(work, ignore_errors=True)
    session, saves = os.path.join(work, "session"), os.path.join(work, "saves")
    os.makedirs(os.path.join(session, "commands"))
    if os.path.isfile(saves_from):   # the approved archive: its top folder is saves/
        import tarfile
        with tarfile.open(saves_from) as t:
            t.extractall(work)
    else:
        shutil.copytree(saves_from, saves)
    env = dict(os.environ, GT4_ELF=os.path.join(ORIG, "CORE.GT4.elf"),
               GT4_ISO=os.path.join(ORIG, "Gran Turismo 4 (USA) (v1.01).iso"), **extra)
    log = open(os.path.join(work, "host.log"), "wb")
    start = time.time()
    proc = subprocess.Popen([a.host, "--headless", "--no-audio", "--size", "640x448", "--session-dir", session,
                             "--game", a.game, "--saves", saves, "--preset", "original",
                             "--set", "display.frame_interpolation=off", "--set", "texture.hd_pack=off"],
                            cwd=a.wt, env=env, stdout=log, stderr=subprocess.STDOUT,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    try:
        for k, c in enumerate(commands):
            tmp = os.path.join(session, "commands", f"{k:06d}.tmp")
            with open(tmp, "w") as f:
                f.write(c + "\n")
            os.replace(tmp, tmp[:-4] + ".cmd")
        try:
            code = proc.wait(timeout=a.deadline)
        except subprocess.TimeoutExpired:
            code = "deadline"
    finally:
        if proc.poll() is None:
            with open(os.path.join(session, "commands", "999999.cmd"), "w") as f:
                f.write("quit\n")
            try:
                proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                proc.kill()
        log.close()
        shutil.rmtree(saves, ignore_errors=True)
    logs = [p for p in glob.glob(os.path.join(work, "**", "game.log"), recursive=True)]
    print(f"== {tag}: exit {code}, {time.time() - start:.0f} s, game logs {logs}", flush=True)
    for path in logs + [os.path.join(work, "host.log")]:
        for line in open(path, errors="replace"):
            if re.search(r"hash memo|GS: |identity|VBlanks? |walks ", line) and len(line) < 2500:
                print(f"   {os.path.basename(path)}: {line.rstrip()[:1800]}", flush=True)
    return code


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wt", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--deadline", type=int, default=1500)
    ap.add_argument("specs", nargs="+")
    a = ap.parse_args()
    sys.path.insert(0, os.path.join(MAIN, "tools"))
    try:
        import slot
        slot.kill_with_me()
    except Exception as e:   # (not fatal: the finally above stops each host)
        print(f"(no kill_with_me: {e})")
    bad = 0
    for k, spec in enumerate(a.specs):
        code = run(a, spec, k)
        bad += code != 0
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

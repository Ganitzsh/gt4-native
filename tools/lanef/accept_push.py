#!/usr/bin/env python3
"""Rebase on origin/main, run accept.py (clean build, ctest, validation), and push through WSL right after a pass,
but only if origin/main has not moved meanwhile (else report and stop). One process; git and wsl without consoles."""
import subprocess
import sys
from pathlib import Path

CLONE = "C:/Users/julie/gt4-lane-F"
NW = subprocess.CREATE_NO_WINDOW


def git(*args):
    r = subprocess.run(["git", "-C", CLONE, *args], capture_output=True, text=True, creationflags=NW)
    if r.returncode:
        raise SystemExit(f"git {' '.join(args)}: {r.stderr.strip()}")
    return r.stdout.strip()


git("fetch", "-q", "origin")
git("rebase", "-q", "origin/main")
base = git("rev-parse", "origin/main")
head = git("rev-parse", "HEAD")
print(f"rebased: HEAD {head[:7]} on origin/main {base[:7]}", flush=True)
r = subprocess.run([sys.executable, "-W", "ignore", str(Path(__file__).with_name("accept.py"))], capture_output=True,
                   text=True, creationflags=NW)
print(r.stdout, flush=True)
ok = r.returncode == 0 and "100% tests passed" in r.stdout and "0 validation messages" in r.stdout \
    and "light_list: all checks passed" in r.stdout
if not ok:
    raise SystemExit("acceptance did not pass: not pushing")
git("fetch", "-q", "origin")
if git("rev-parse", "origin/main") != base:
    # Moved: push only when the new commits change docs alone (the accepted code is then what gets pushed).
    # The coordinator's rule: commits that landed meanwhile and touch none of my files need no rebuild (CI checks the
    # result after the push); one that touches a file of mine means a rebuild and a new acceptance.
    changed = [f for f in git("diff", "--name-only", base, "origin/main").splitlines() if f]
    mine = set(f for f in git("diff", "--name-only", base, "HEAD").splitlines() if f)
    overlap = sorted(set(changed) & mine)
    if overlap:
        raise SystemExit(f"origin/main moved during the acceptance with changes to my files ({overlap[:5]}): not pushing")
    git("rebase", "-q", "origin/main")
    print(f"origin/main moved by {len(changed)} files, none of mine: rebased onto {git('rev-parse', 'origin/main')[:7]}")
p = subprocess.run(["wsl.exe", "-e", "git", "-C", "/mnt/c/Users/julie/gt4-lane-F", "push",
                    "git@github.com:Ganitzsh/gt4-native.git", "HEAD:main"], capture_output=True, text=True,
                   creationflags=NW)
print(p.stdout + p.stderr)
if p.returncode != 0:
    raise SystemExit("push failed (exit %d)" % p.returncode)
# The coordinator's push check: a push counts only once origin/main contains the commit.
head = git("rev-parse", "HEAD")
git("fetch", "-q", "origin")
landed = subprocess.run(["git", "-C", CLONE, "merge-base", "--is-ancestor", head, "origin/main"], creationflags=NW).returncode == 0
if not landed:
    raise SystemExit(f"push not on origin/main ({head[:7]} is not an ancestor): not pushed")
print(f"pushed: {head[:7]} is on origin/main")

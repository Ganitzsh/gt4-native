"""Keeps the machine's disk from filling with dead worktrees and old build output (the coordinator's rules).

    py -I tools/prune.py                         a dry run: what would go, what stays and why, the sizes
    py -I tools/prune.py --apply                 deletes what the dry run lists
    py -I tools/prune.py --apply --worktrees-approved   the coordinator's one-time OK for removing worktrees
    py -I tools/prune.py --self-test

What goes, under --home (default C:/Users/julie), only in checkouts of gt4-native (origin Ganitzsh/ or
gustavocanepa/gt4-native) and the worktrees of gt4-native-game and gt4-decomp; other projects there are never read:
1. Worktrees and extra clones that are clean (git status shows nothing, untracked files included), whose HEAD and
   branches are on a remote, with no stash (clones), not locked, holding no junction, idle 24 h. A worktree goes
   with `git worktree remove` from its own repository, never --force: git refuses whatever is uncommitted. The lanes'
   own clones (gt4-lane-*, everything under gt4-lane-U, gt4-native-arch, -audit, -sprite, -game, gt4-decomp) stay;
   only their build/ items below are pruned. The first run that would remove a worktree lists them and stops
   until --worktrees-approved (which leaves the marker prune-worktrees.ok in --home).
2. build/packages: the newest 3 per checkout stay.
3. Release folders build/rel-*, build/rel_* and build/release-work/*: idle 24 h.
4. The object cache (tools/objcache.py's folder): capped at 10 GB, oldest entries first (missing entries are
   compiled again).
5. build/sessions/* and stray .ppm/.png files in build/ (three levels down, outside the trees above): idle 3 days.
6. Build trees, per checkout: game, game_*, *_game and host-*, *-host, each family's newest 2 stay, older ones go
   when idle 3 days.
7. Claude session scratchpads (the temp folder's claude/*/*/scratchpad): a scratchpad with a file changed in the
   last 24 h is skipped whole (its session may be alive); in the others, folders (frame dumps, session folders) and
   loose .ppm/.png files idle 3 days go. Scripts, notes and logs at a scratchpad's top stay. Nothing else in the temp
   folder is looked at (DiagOutputDir and Windows' files are the owner's).
Never: gt4-native-game's or gt4-decomp's own files, C:/Users/julie/locks, gt4-native-sprite, an item with a
saves*, *_saves, caps, captures or refs (REF stills) folder in its top two levels (in a checkout, among the files
git doesn't track), any .iso/.elf/.ps2/.tgz/.mcd/.mc2 or BIOS file, a file named in docs/CAPTURES.md, a path or
name listed in --home's prune-keep.txt (one per line). An item a slot lock, a process's command line or a live
arc session's folder names is in use and stays. A directory junction or link is removed with `cmd /c rmdir`
(without /s) and never followed. Every removal is logged to --home's prune.log. If any check fails, nothing is
deleted.
"""
import argparse
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

DAY = 86400
WINDOWS = os.name == "nt"
ORIGIN = re.compile(r"[/:](ganitzsh|gustavocanepa)/gt4-native(\.git)?/?$", re.I)
KEEP_CLONES = re.compile(r"^(gt4-lane-.*|gt4-native-(arch|audit|sprite|game)|gt4-decomp)$", re.I)
PROTECTED_REPOS = ("gt4-native-game", "gt4-decomp")
PROTECTED_EXT = {".iso", ".elf", ".ps2", ".tgz", ".mcd", ".mc2"}
# Folders of saves (saves, r5_saves, saves-ss-r5), captures and the REF stills: they sit near the top of an item
# (build/saves, build/refs/<set>), so only the first two levels count; deeper, build output repeats source paths
# (CMakeFiles/gt4_saves.dir/platform/saves/...). BIOS files and the game's file types count at any depth.
PROTECTED_DIR = re.compile(r"^(saves.*|.*[_-]saves|caps|captures|refs)$", re.I)
PROTECTED_FILE = re.compile(r"bios|^scph.*\.bin$", re.I)
NAME_DEPTH = 2


def protected_parts(parts, keep_names=()):
    """Whether a relative path (its components) is or holds a save, capture, game file or a listed name."""
    last = parts[-1].lower()
    return (any(PROTECTED_DIR.match(p) for p in parts[:NAME_DEPTH]) or bool(PROTECTED_FILE.search(last)) or
            os.path.splitext(last)[1] in PROTECTED_EXT or last in keep_names)
CAPTURE_TOKEN = re.compile(r"[\w.+-]+\.(?:gs|gsd|ppm|png|gz|bin|dump|cap|raw|zst|p2s)\b", re.I)
GAME_TREE = re.compile(r"^(game|game_.+|.+_game)$", re.I)
HOST_TREE = re.compile(r"^(host-.+|.+-host)$", re.I)
REL_TREE = re.compile(r"^rel[-_].+$", re.I)
DUMP_EXT = {".ppm", ".png"}
PROGRAM_EXT = {".exe", ".dll", ".so"}
NOT_DUMPS = ("ffmpeg", "sysroot", "eeref", "venv", "pcsx2", "resources", "third_party", "_deps")
REPARSE = 0x400   # FILE_ATTRIBUTE_REPARSE_POINT


def norm(p):
    return os.path.abspath(p).replace("\\", "/").rstrip("/").lower()


def under(p, root):
    p, root = norm(p), norm(root)
    return p == root or p.startswith(root + "/")


def long_path(p):
    p = os.path.abspath(p)
    return "\\\\?\\" + p.replace("/", "\\") if WINDOWS and not p.startswith("\\\\?\\") else p


def is_link(entry_or_path):
    """A symlink or junction (any reparse point): removed as a link, never followed."""
    st = os.lstat(entry_or_path) if isinstance(entry_or_path, str) else entry_or_path.stat(follow_symlinks=False)
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, "st_file_attributes", 0) & REPARSE)


class Scan:
    """One tree: its size, its newest change, the links and protected files in it."""

    def __init__(self, path, protected_names=(), names=True):
        self.size, self.newest, self.links, self.protected = 0, 0.0, [], []
        stack = [(path, ())]
        st = os.lstat(long_path(path))
        self.newest = st.st_mtime
        while stack:
            d, rel = stack.pop()
            with os.scandir(long_path(d)) as it:
                for e in it:
                    full = os.path.join(d, e.name)
                    st = e.stat(follow_symlinks=False)
                    if is_link(e):   # its target is elsewhere; its own time is when it was made, not work in the tree
                        self.links.append(full)
                        continue
                    self.newest = max(self.newest, st.st_mtime)
                    if names and protected_parts(rel + (e.name,), protected_names):
                        self.protected.append(full)
                    if e.is_dir(follow_symlinks=False):
                        stack.append((full, rel + (e.name,)))
                    else:
                        self.size += st.st_size


def gib(n):
    return f"{n / (1 << 30):.2f} GB"


class Pruner:
    def __init__(self, home, objcache, now=None, cap=10 << 30, in_use=None, origin=ORIGIN, log=None, scratch=None):
        self.home, self.objcache, self.now, self.cap = home, objcache, now or time.time(), cap
        # rule 7's root: Claude Code's session folders in the temp folder (claude/<project hash>/<session>/scratchpad)
        self.scratch = scratch if scratch is not None else os.path.join(tempfile.gettempdir(), "claude")
        self.origin = origin
        self.log_path = log or os.path.join(home, "prune.log")
        self.plan, self.kept, self.errors = [], [], []
        self.keep_paths, self.keep_names = self.read_keep()
        self.never = [os.path.join(home, n) for n in PROTECTED_REPOS + ("locks", "gt4-native-sprite")]
        self.never += [os.path.join(os.path.dirname(objcache), n) for n in ("saves", "textures")]
        self.in_use = in_use if in_use is not None else self.find_in_use()

    # --- what may not go -----------------------------------------------------------------------------------------
    def read_keep(self):
        paths, names = [], set()
        keep = os.path.join(self.home, "prune-keep.txt")
        if os.path.exists(keep):
            for line in open(keep, encoding="utf-8", errors="replace"):
                line = line.strip()
                if line and not line.startswith("#"):
                    (paths.append(line) if "/" in line or "\\" in line else names.add(line.lower()))
        captures = os.path.join(self.home, "gt4-native-game", "docs", "CAPTURES.md")
        if os.path.exists(captures):
            text = open(captures, encoding="utf-8", errors="replace").read()
            names |= {t.lower() for t in CAPTURE_TOKEN.findall(text)}
        return paths, names

    def find_in_use(self):
        """Lowercase, slash-normalised texts naming paths in use: slot locks, process command lines, arc sessions."""
        texts = []
        locks = os.path.join(self.home, "locks")
        for root, _, files in os.walk(locks):
            for f in files:
                try:
                    texts.append(open(os.path.join(root, f), encoding="utf-8", errors="replace").read())
                except OSError as e:
                    self.errors.append(f"cannot read the slot lock {f}: {e}")
        if WINDOWS:
            ps = "Get-CimInstance Win32_Process | Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress"
            p = subprocess.run(["powershell.exe", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
            if p.returncode or not p.stdout.strip():
                self.errors.append("cannot list the processes: " + p.stderr.strip()[:200])
            else:
                for proc in json.loads(p.stdout):
                    texts += [proc.get("ExecutablePath") or "", proc.get("CommandLine") or ""]
        else:
            for pid in os.listdir("/proc"):
                try:
                    line = open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ")
                    texts.append(line.decode("utf-8", "replace"))
                    texts.append(os.readlink(f"/proc/{pid}/cwd"))
                except OSError:
                    pass
        if shutil.which("arc"):
            p = subprocess.run(["arc", "peers"], capture_output=True, text=True, encoding="utf-8", errors="replace")
            try:   # every live session's folder ("/C:/Users/..." on Windows); another machine's can't match here
                for peer in json.loads(p.stdout):
                    cwd = peer.get("cwd") or ""
                    if cwd and not peer.get("endedAt"):
                        texts.append("\0cwd:" + (cwd[1:] if re.match(r"^/[A-Za-z]:", cwd) else cwd))
            except (json.JSONDecodeError, TypeError, AttributeError) as e:
                self.errors.append(f"cannot read arc peers: {e}")
        return [t.replace("\\", "/").lower() for t in texts if t]

    def use_of(self, path):
        p = norm(path)
        for t in self.in_use:
            if t.startswith("\0cwd:"):
                cwd = norm(t[5:])
                if under(cwd, p) or under(p, cwd):
                    return "a live session works in " + t[5:]
            elif p in t:
                return "named by a slot lock or a process"
        return None

    def refusal(self, path, scan=None):
        """Why path may not go, or None."""
        if not under(path, self.home) and not under(path, self.objcache) and not under(path, self.scratch):
            return "outside the folders this tool prunes"
        for n in self.never:
            if under(path, n) or under(n, path):
                return "protected (" + os.path.basename(n) + ")"
        for k in self.keep_paths:
            if under(path, k) or under(k, path):
                return "in prune-keep.txt"
        if os.path.basename(path).lower() in self.keep_names:
            return "named in prune-keep.txt or CAPTURES.md"
        if protected_parts([os.path.basename(path)]):
            return "a save, capture or game file"
        use = self.use_of(path)
        if use:
            return use
        if scan and scan.protected:
            return "holds " + os.path.basename(scan.protected[0]) + " (a save, capture, game file or a listed name)"
        return None

    # --- finding ---------------------------------------------------------------------------------------------------
    def git(self, cwd, *args):
        p = subprocess.run(["git", "-C", cwd, *args], capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        if p.returncode:
            raise RuntimeError(f"git {' '.join(args)} in {cwd}: {p.stderr.strip()[:200]}")
        return p.stdout

    def is_ours(self, path):
        """A gt4-native checkout, or a worktree of a protected repository (gt4-native-game, gt4-decomp)."""
        try:
            common = norm(self.git(path, "rev-parse", "--path-format=absolute", "--git-common-dir").strip())
        except RuntimeError:
            return False
        if any(common == norm(os.path.join(self.home, r, ".git")) for r in PROTECTED_REPOS):
            return True
        try:
            return bool(self.origin.search(self.git(path, "remote", "get-url", "origin").strip().replace("\\", "/")))
        except RuntimeError:
            return False

    def checkouts(self):
        """(path, kind) for the home's gt4-native checkouts: 'lane' (kept) or 'extra'."""
        out = []
        for name in sorted(os.listdir(self.home)):
            path = os.path.join(self.home, name)
            if not os.path.isdir(path) or is_link(path):
                continue
            if name.lower() == "gt4-lane-u":
                out += [(os.path.join(path, n), "lane") for n in sorted(os.listdir(path))
                        if os.path.exists(os.path.join(path, n, ".git")) and self.is_ours(os.path.join(path, n))]
            elif os.path.exists(os.path.join(path, ".git")) and self.is_ours(path):
                out.append((path, "lane" if KEEP_CLONES.match(name) else "extra"))
        return out

    def nested_worktrees(self, checkouts):
        """Registered worktrees inside a checkout's build/ (lane C's build/d10a, ...)."""
        found = {}
        for path, _ in checkouts:
            for line in self.git(path, "worktree", "list", "--porcelain").splitlines():
                if line.startswith("worktree "):
                    wt = line[9:].strip()
                    if "/build/" in norm(wt) and os.path.isdir(wt):
                        found[norm(wt)] = wt
        return sorted(found.values())

    # --- the rules -------------------------------------------------------------------------------------------------
    def add(self, rule, path, scan, why=None, worktree=None):
        why = why or self.refusal(path, scan)
        size = scan.size if scan else 0
        if why:
            self.kept.append((rule, path, size, why))
        else:
            self.plan.append((rule, path, size, worktree))

    def check_checkout(self, rule, path, idle_days=1):
        """Rule 1 (and a release folder that is a worktree): clean, pushed, not locked, no links, idle."""
        is_worktree = os.path.isfile(os.path.join(path, ".git"))
        scan = Scan(path, self.keep_names, names=False)   # the source tree's own names (platform/saves) don't count
        if self.git(path, "status", "--porcelain", "--untracked-files=all").strip():
            return self.add(rule, path, scan, "changes or untracked files")
        # what git doesn't track (clean, so only ignored files): saves, captures, game files or listed names stay
        for rel in filter(None, self.git(path, "ls-files", "--others", "--ignored", "--exclude-standard",
                                         "-z").split("\0")):
            parts = rel.rstrip("/").split("/")
            if rel.endswith("/") and "_deps" not in parts:   # CMake's fetched dependencies come back on a rebuild
                return self.add(rule, path, scan, "holds a checkout of its own: " + rel)
            if protected_parts(parts, self.keep_names):
                return self.add(rule, path, scan, "holds " + rel + " (a save, capture, game file or a listed name)")
        if not self.git(path, "branch", "-r", "--contains", "HEAD").strip():
            return self.add(rule, path, scan, "HEAD is on no remote branch (unpushed)")
        owner = None
        if is_worktree:
            common = self.git(path, "rev-parse", "--path-format=absolute", "--git-common-dir").strip()
            owner = os.path.dirname(common.rstrip("/\\"))
            for block in self.git(owner, "worktree", "list", "--porcelain").strip().split("\n\n"):
                lines = block.splitlines()
                if lines and norm(lines[0][9:]) == norm(path) and "\nlocked" in "\n" + block:
                    return self.add(rule, path, scan, "a locked worktree")
            if scan.links:
                return self.add(rule, path, scan, "holds a junction or link: remove it by hand")
        else:
            if self.git(path, "log", "--branches", "--not", "--remotes", "--oneline", "-1").strip():
                return self.add(rule, path, scan, "a local branch is not on any remote")
            if self.git(path, "stash", "list").strip():
                return self.add(rule, path, scan, "has stashes")
            if len(self.git(path, "worktree", "list").strip().splitlines()) > 1:
                return self.add(rule, path, scan, "has worktrees of its own")
        if self.now - scan.newest < idle_days * DAY:
            return self.add(rule, path, scan, f"changed {(self.now - scan.newest) / 3600:.0f} h ago")
        self.add(rule, path, scan, worktree=owner)

    def tree(self, rule, path, idle_days):
        if os.path.exists(os.path.join(path, ".git")):
            return self.check_checkout(rule, path, idle_days)
        scan = Scan(path, self.keep_names)
        if self.now - scan.newest < idle_days * DAY:
            return self.add(rule, path, scan, f"changed {(self.now - scan.newest) / 86400:.1f} days ago")
        self.add(rule, path, scan)

    def newest_first(self, paths):
        scans = {p: Scan(p, self.keep_names) for p in paths}
        return sorted(paths, key=lambda p: scans[p].newest, reverse=True), scans

    def build_rules(self, checkout):
        build = os.path.join(checkout, "build")
        if not os.path.isdir(build) or is_link(build):
            return
        names = sorted(os.listdir(build))
        dirs = [os.path.join(build, n) for n in names if os.path.isdir(os.path.join(build, n))
                and not is_link(os.path.join(build, n))]
        skip = set()
        # 2. packages: the newest 3 stay
        pk = os.path.join(build, "packages")
        if pk in dirs:
            skip.add(pk)
            order, scans = self.newest_first([os.path.join(pk, n) for n in os.listdir(pk)])
            for i, p in enumerate(order):
                self.add("2 packages", p, scans[p], "one of the newest 3" if i < 3 else None)
        # 3. release folders
        rels = [d for d in dirs if REL_TREE.match(os.path.basename(d))]
        rw = os.path.join(build, "release-work")
        if rw in dirs:
            skip.add(rw)
            rels += [os.path.join(rw, n) for n in os.listdir(rw) if os.path.isdir(os.path.join(rw, n))]
        for d in rels:
            skip.add(d)
            self.tree("3 release folder", d, 1)
        # 5. sessions
        se = os.path.join(build, "sessions")
        if se in dirs:
            skip.add(se)
            for n in os.listdir(se):
                p = os.path.join(se, n)
                if os.path.isdir(p) and not is_link(p):
                    self.tree("5 session", p, 3)
                else:
                    self.small_file("5 session", p, 3)
        # 6. build trees: each family's newest 2 stay
        for family, pattern in (("game", GAME_TREE), ("host", HOST_TREE)):
            trees = [d for d in dirs if pattern.match(os.path.basename(d)) and d not in skip
                     and not os.path.exists(os.path.join(d, ".git"))]
            skip.update(trees)
            order, scans = self.newest_first(trees)
            for i, p in enumerate(order):
                if i < 2:
                    self.add(f"6 {family} tree", p, scans[p], "one of the newest 2")
                elif self.now - scans[p].newest < 3 * DAY:
                    self.add(f"6 {family} tree", p, scans[p],
                             f"changed {(self.now - scans[p].newest) / 86400:.1f} days ago")
                else:
                    self.add(f"6 {family} tree", p, scans[p])
        # 5. stray dumps, three levels down, outside the trees above, worktrees and protected folders
        stack = [(build, 0)]
        while stack:
            d, depth = stack.pop()
            entries = list(os.scandir(long_path(d)))
            if depth and any(os.path.splitext(e.name)[1].lower() in PROGRAM_EXT for e in entries):
                continue   # a program's folder (PCSX2's, a host's): its images are its own, not dumps
            for e in entries:
                full = os.path.join(d, e.name)
                if is_link(e) or full in skip or PROTECTED_DIR.match(e.name) or PROTECTED_FILE.search(e.name):
                    continue
                if e.is_dir(follow_symlinks=False):
                    if depth < 2 and not os.path.exists(os.path.join(full, ".git")) and \
                            not e.name.lower().startswith(NOT_DUMPS):
                        stack.append((full, depth + 1))
                elif os.path.splitext(e.name)[1].lower() in DUMP_EXT:
                    self.small_file("5 stray dump", full, 3)

    def small_file(self, rule, path, idle_days):
        st = os.lstat(long_path(path))

        class One:
            size, newest, links, protected = st.st_size, st.st_mtime, [], []
        if self.now - st.st_mtime < idle_days * DAY:
            return self.add(rule, path, One, f"changed {(self.now - st.st_mtime) / 86400:.1f} days ago")
        self.add(rule, path, One)

    def objcache_rule(self):
        if not os.path.isdir(self.objcache):
            return
        files = []
        for root, dirs, names in os.walk(long_path(self.objcache)):
            dirs[:] = [d for d in dirs if not is_link(os.path.join(root, d))]
            for n in names:
                st = os.lstat(os.path.join(root, n))
                files.append((st.st_mtime, st.st_size, os.path.join(root, n)))
        total = sum(f[1] for f in files)
        files.sort()
        drop, freed = [], 0
        for mtime, size, path in files:
            if total - freed <= self.cap:
                break
            drop.append(path)
            freed += size
        self.objcache_total, self.objcache_drop, self.objcache_freed = total, drop, freed

    def scratch_rule(self):
        """Rule 7: idle Claude session scratchpads' old folders and image dumps."""
        if not os.path.isdir(self.scratch) or is_link(self.scratch):
            return
        for project in sorted(os.listdir(self.scratch)):
            pdir = os.path.join(self.scratch, project)
            if not os.path.isdir(pdir) or is_link(pdir):
                continue
            for session in sorted(os.listdir(pdir)):
                pad = os.path.join(pdir, session, "scratchpad")
                if not os.path.isdir(pad) or is_link(pad):
                    continue
                scan = Scan(pad, self.keep_names, names=False)
                if self.now - scan.newest < DAY:
                    self.kept.append(("7 scratchpad", pad, scan.size,
                                      "a file changed in the last 24 h (live session)"))
                    continue
                for e in os.scandir(long_path(pad)):
                    full = os.path.join(pad, e.name)
                    if is_link(e):
                        continue
                    if e.is_dir(follow_symlinks=False):
                        self.tree("7 scratchpad folder", full, 3)
                    elif os.path.splitext(e.name)[1].lower() in DUMP_EXT:
                        self.small_file("7 scratchpad dump", full, 3)

    def find(self):
        self.objcache_drop, self.objcache_freed, self.objcache_total = [], 0, 0
        try:
            checkouts = self.checkouts()
            for path, kind in checkouts:
                if kind == "extra":
                    self.check_checkout("1 worktree or clone", path)
            for wt in self.nested_worktrees([c for c in checkouts if c[1] == "lane"]):
                if not any(under(wt, os.path.join(c[0], "build", "release-work")) for c in checkouts):
                    self.check_checkout("1 worktree or clone", wt)
            for path, kind in checkouts:
                if not any(under(path, n) for n in self.never):
                    self.build_rules(path)
            self.objcache_rule()
            self.scratch_rule()
        except (OSError, RuntimeError, ValueError) as e:
            self.errors.append(f"{type(e).__name__}: {e}")

    # --- deleting --------------------------------------------------------------------------------------------------
    def log(self, text):
        with open(self.log_path, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + text + "\n")

    def remove_link(self, path):
        if os.path.isdir(path):
            p = subprocess.run(["cmd", "/c", "rmdir", path.replace("/", "\\")] if WINDOWS else ["rm", path],
                               capture_output=True, text=True)
            if p.returncode or os.path.lexists(long_path(path)):
                raise OSError(f"cmd /c rmdir {path}: {p.stderr.strip() or p.stdout.strip()}")
        else:
            os.unlink(long_path(path))

    def rmtree(self, path):
        """Deletes path; a link inside is removed as a link (cmd /c rmdir, without /s) and never followed."""
        if self.refusal(path) or norm(path) == norm(self.home) or len(norm(path).split("/")) < 4:
            raise OSError(f"refusing to delete {path}")
        if is_link(long_path(path)):
            return self.remove_link(path)
        if not os.path.isdir(long_path(path)):
            os.chmod(long_path(path), stat.S_IWRITE)
            return os.unlink(long_path(path))
        stack, order = [path], []
        while stack:
            d = stack.pop()
            order.append(d)
            with os.scandir(long_path(d)) as it:
                for e in it:
                    full = os.path.join(d, e.name)
                    if is_link(e):
                        self.remove_link(full)
                    elif e.is_dir(follow_symlinks=False):
                        stack.append(full)
                    else:
                        os.chmod(long_path(full), stat.S_IWRITE)
                        os.unlink(long_path(full))
        for d in reversed(order):
            os.rmdir(long_path(d))

    def apply(self):
        for rule, path, size, owner in self.plan:
            if owner:
                p = subprocess.run(["git", "-c", "core.longpaths=true", "-C", owner, "worktree", "remove", path],
                                   capture_output=True, text=True)
                if p.returncode:
                    self.log(f"FAILED {rule}: git worktree remove {path}: {p.stderr.strip()[:200]}")
                    raise RuntimeError(f"git worktree remove {path}: {p.stderr.strip()[:200]}")
            else:
                self.rmtree(path)
            self.log(f"removed {rule}: {path} ({gib(size)})")
        if self.objcache_drop:
            for path in self.objcache_drop:
                os.unlink(path)
            self.log(f"removed 4 objcache: {len(self.objcache_drop)} oldest entries ({gib(self.objcache_freed)}), "
                     f"{gib(self.objcache_total - self.objcache_freed)} left")

    def report(self):
        show = lambda p: p.replace("\\", "/")
        for rule, path, size, owner in self.plan:
            print(f"GO    {rule:22} {gib(size):>10}  {show(path)}" + ("  (git worktree remove)" if owner else ""))
        recent = {}   # sessions and dumps kept only for their age: counted, not listed
        for rule, path, size, why in self.kept:
            if rule.startswith(("5", "7")) and why.startswith("changed"):
                n, s = recent.get(rule, (0, 0))
                recent[rule] = (n + 1, s + size)
                continue
            print(f"keep  {rule:22} {gib(size):>10}  {show(path)}: {why}")
        for rule, (n, s) in sorted(recent.items()):
            print(f"keep  {rule:22} {gib(s):>10}  {n} changed in the last 3 days")
        print(f"objcache: {gib(self.objcache_total)}; {len(self.objcache_drop)} oldest entries go "
              f"({gib(self.objcache_freed)}) for the {gib(self.cap)} cap")
        total = sum(p[2] for p in self.plan) + self.objcache_freed
        print(f"to remove: {len(self.plan)} items + objcache, {gib(total)}")


def free(path):
    return shutil.disk_usage(path).free


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--home", default="C:/Users/julie" if WINDOWS else os.path.expanduser("~"))
    ap.add_argument("--apply", action="store_true", help="delete (the default is a dry run)")
    ap.add_argument("--worktrees-approved", action="store_true", help="the coordinator OK'd removing worktrees")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        sys.exit(self_test())
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import objcache  # noqa: E402
    cache = objcache.folder() or os.path.join(os.environ.get("LOCALAPPDATA", a.home), "gt4-native", "objcache")
    before = free(a.home)
    pr = Pruner(a.home, cache)
    if not pr.errors:
        pr.find()
    print(f"free before: {gib(before)}")
    pr.report()
    if pr.errors:
        print("checks failed, nothing deleted:\n  " + "\n  ".join(pr.errors))
        sys.exit(2)
    if not a.apply:
        print("dry run: nothing deleted (--apply deletes)")
        return
    marker = os.path.join(a.home, "prune-worktrees.ok")
    worktrees = [p for p in pr.plan if p[3]]
    if worktrees and not os.path.exists(marker):
        if not a.worktrees_approved:
            print("worktree removals need the coordinator's one-time OK; nothing deleted:\n  " +
                  "\n  ".join(p[1] for p in worktrees))
            sys.exit(3)
        with open(marker, "w") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S") + " the coordinator's OK for worktree removals\n")
    try:
        pr.apply()
    except (OSError, RuntimeError) as e:
        print(f"stopped: {e}")
        print(f"free after: {gib(free(a.home))}")
        sys.exit(2)
    print(f"free after: {gib(free(a.home))} (was {gib(before)})")


def self_test():
    import tempfile
    failed = []

    def expect(ok, what):
        if not ok:
            failed.append(what)
            print("FAIL  " + what)

    def age(path, days):
        t = time.time() - days * DAY
        for root, dirs, files in os.walk(path):
            for n in files + dirs:
                if not is_link(os.path.join(root, n)):   # a link's own time: Windows can't set it; it isn't read
                    os.utime(os.path.join(root, n), (t, t))
        os.utime(path, (t, t))

    with tempfile.TemporaryDirectory() as tmp:
        home = os.path.join(tmp, "home")
        cache = os.path.join(tmp, "local", "gt4-native", "objcache")
        os.makedirs(cache)
        g = ["git", "-c", "user.name=t", "-c", "user.email=t@t"]
        run = lambda *a, cwd=tmp: subprocess.run(g + list(a), cwd=cwd, capture_output=True, check=True)
        origin = os.path.join(tmp, "o", "Ganitzsh", "gt4-native.git")
        run("init", "-q", "--bare", origin)
        lane = os.path.join(home, "gt4-lane-Z")
        run("clone", "-q", origin, lane)
        open(os.path.join(lane, "a.txt"), "w").write("a\n")
        open(os.path.join(lane, ".gitignore"), "w").write("build/\n")
        os.makedirs(os.path.join(lane, "platform", "saves"))   # tracked source named like saves: doesn't count
        open(os.path.join(lane, "platform", "saves", "saves.cpp"), "w").write("int x;\n")
        run("add", ".", cwd=lane)
        run("commit", "-qm", "a", cwd=lane)
        run("push", "-q", "origin", "HEAD:main", cwd=lane)
        run("fetch", "-q", cwd=lane)
        old_wt, new_wt, dirty_wt, saves_wt = (os.path.join(home, n) for n in ("old-wt", "new-wt", "dirty-wt",
                                                                             "saves-wt"))
        for wt in (old_wt, new_wt, dirty_wt, saves_wt):
            run("worktree", "add", "-q", "--detach", wt, "HEAD", cwd=lane)
        os.makedirs(os.path.join(saves_wt, "build", "r5_saves"))   # an ignored copy of a save: the worktree stays
        open(os.path.join(saves_wt, "build", "r5_saves", "card.ps2"), "w").write("s")
        age(saves_wt, 3)
        os.makedirs(os.path.join(old_wt, "build"))
        open(os.path.join(old_wt, "build", "x.o"), "w").write("o")   # ignored: git worktree remove takes it
        open(os.path.join(dirty_wt, "new.txt"), "w").write("n")
        build = os.path.join(lane, "build")
        for i in range(5):
            os.makedirs(os.path.join(build, "packages", f"p{i}"))
            open(os.path.join(build, "packages", f"p{i}", "f.zip"), "w").write("z")
            age(os.path.join(build, "packages", f"p{i}"), 10 - i)
        for n, days in (("game_a", 9), ("game_b", 8), ("game_c", 1), ("game_d", 7), ("host-x", 9)):
            os.makedirs(os.path.join(build, n))
            open(os.path.join(build, n, "o.o"), "w").write("o")
            age(os.path.join(build, n), days)
        os.makedirs(os.path.join(build, "game_e", "saves"))   # old and not newest, but holds saves: stays
        age(os.path.join(build, "game_e"), 20)
        deep = os.path.join("CMakeFiles", "gt4_saves.dir", "platform", "saves")   # build output's source path
        os.makedirs(os.path.join(build, "game_f", deep))   # ... is not a save: the old tree goes
        open(os.path.join(build, "game_f", deep, "save_import.cpp.obj"), "w").write("o")
        age(os.path.join(build, "game_f"), 10)
        os.makedirs(os.path.join(old_wt, "build", deep))   # nor in an ignored build of a worktree
        open(os.path.join(old_wt, "build", deep, "save_import.cpp.obj"), "w").write("o")
        dep = os.path.join(old_wt, "build", "_deps", "lib-src")   # a fetched dependency's own checkout
        run("init", "-q", dep)
        open(os.path.join(dep, "lib.c"), "w").write("int y;\n")
        run("add", ".", cwd=dep)
        run("commit", "-qm", "lib", cwd=dep)
        nested_wt = os.path.join(home, "nested-wt")   # a worktree holding a checkout of ours: stays
        run("worktree", "add", "-q", "--detach", nested_wt, "HEAD", cwd=lane)
        run("init", "-q", os.path.join(nested_wt, "build", "inner"))
        age(nested_wt, 3)
        os.makedirs(os.path.join(build, "sessions", "s_old")); age(os.path.join(build, "sessions", "s_old"), 5)
        os.makedirs(os.path.join(build, "sessions", "s_new"))
        open(os.path.join(build, "shot.ppm"), "w").write("p"); age(os.path.join(build, "shot.ppm"), 5)
        open(os.path.join(build, "kept.ppm"), "w").write("p"); age(os.path.join(build, "kept.ppm"), 5)
        open(os.path.join(home, "prune-keep.txt"), "w").write("kept.ppm\n")
        os.makedirs(os.path.join(build, "refs", "x"))   # REF stills (CAPTURES.md: build/refs/<set>/*.png)
        open(os.path.join(build, "refs", "x", "old.png"), "w").write("p")
        age(os.path.join(build, "refs"), 4)
        os.makedirs(os.path.join(build, "tool"))   # a program's folder: its old image is not a dump
        open(os.path.join(build, "tool", "app.exe"), "w").write("x")
        open(os.path.join(build, "tool", "icon.png"), "w").write("p")
        age(os.path.join(build, "tool"), 5)
        target = os.path.join(tmp, "target")
        os.makedirs(target)
        open(os.path.join(target, "precious.txt"), "w").write("keep me")
        os.makedirs(os.path.join(build, "rel-old"))
        link = os.path.join(build, "rel-old", "link")
        if WINDOWS:
            subprocess.run(["cmd", "/c", "mklink", "/J", link.replace("/", "\\"), target.replace("/", "\\")],
                           capture_output=True, check=True)
        else:
            os.symlink(target, link)
        age(os.path.join(build, "rel-old"), 3)
        for i in range(4):
            open(os.path.join(cache, f"e{i}"), "wb").write(b"x" * 1000)
            os.utime(os.path.join(cache, f"e{i}"), (time.time() - (10 - i) * DAY,) * 2)
        age(old_wt, 3)
        age(dirty_wt, 3)

        scratch = os.path.join(tmp, "claude")   # rule 7: a live session's scratchpad and an idle one
        live = os.path.join(scratch, "p1", "live", "scratchpad")
        os.makedirs(os.path.join(live, "frames"))
        open(os.path.join(live, "frames", "f.ppm"), "w").write("p")
        age(os.path.join(live, "frames"), 5)
        open(os.path.join(live, "notes.txt"), "w").write("today")   # a fresh file: the whole scratchpad stays
        idle = os.path.join(scratch, "p1", "idle", "scratchpad")
        for d in ("frames", "caps", "recent"):
            os.makedirs(os.path.join(idle, d))
            open(os.path.join(idle, d, "x.ppm"), "w").write("p")
        open(os.path.join(idle, "notes.py"), "w").write("print()")
        open(os.path.join(idle, "shot.png"), "w").write("p")
        age(idle, 5)
        age(os.path.join(idle, "recent"), 2)   # idle 2 days: under rule 7's 3
        pr = Pruner(home, cache, cap=2500, in_use=[], scratch=scratch)
        pr.find()
        expect(not pr.errors, f"no errors ({pr.errors})")
        go = {os.path.relpath(p[1], home).replace("\\", "/") for p in pr.plan if not under(p[1], scratch)}
        sgo = {os.path.relpath(p[1], scratch).replace("\\", "/") for p in pr.plan if under(p[1], scratch)}
        expect(sgo == {"p1/idle/scratchpad/frames", "p1/idle/scratchpad/shot.png"},
               f"rule 7: the idle scratchpad's old frames folder and image go, nothing else ({sorted(sgo)})")
        want = {"old-wt", "gt4-lane-Z/build/packages/p0", "gt4-lane-Z/build/packages/p1", "gt4-lane-Z/build/game_a",
                "gt4-lane-Z/build/game_b", "gt4-lane-Z/build/game_f", "gt4-lane-Z/build/sessions/s_old",
                "gt4-lane-Z/build/shot.ppm",
                "gt4-lane-Z/build/rel-old"}
        expect(go == want, f"the plan: extra {sorted(go - want)}, missing {sorted(want - go)}")
        expect(len(pr.objcache_drop) == 2, f"objcache: the 2 oldest of 4 go for the cap ({len(pr.objcache_drop)})")
        busy = Pruner(home, cache, in_use=[norm(os.path.join(build, "game_a"))], scratch=scratch)
        busy.find()
        expect(not any(p[1].endswith("game_a") for p in busy.plan), "a tree a process names stays")
        if not failed:
            pr.apply()
            expect(not os.path.exists(old_wt) and os.path.exists(new_wt) and os.path.exists(dirty_wt),
                   "only the idle clean worktree is removed (its tracked platform/saves doesn't hold it)")
            expect(os.path.exists(os.path.join(saves_wt, "build", "r5_saves", "card.ps2")),
                   "a worktree holding an ignored save stays")
            expect(os.path.exists(os.path.join(nested_wt, "build", "inner")), "a worktree holding a checkout stays")
            expect("old-wt" not in subprocess.run(["git", "-C", lane, "worktree", "list"], capture_output=True,
                                                  text=True).stdout, "git's entry for it is gone")
            expect(os.path.exists(os.path.join(target, "precious.txt")), "a junction's target is untouched")
            expect(not os.path.exists(os.path.join(build, "rel-old")), "the folder holding the junction is gone")
            expect(os.path.exists(os.path.join(build, "game_e", "saves")), "a tree holding saves stays")
            expect(os.path.exists(os.path.join(build, "kept.ppm")), "a name in prune-keep.txt stays")
            expect(os.path.exists(os.path.join(build, "tool", "icon.png")), "a program folder's image stays")
            expect(os.path.exists(os.path.join(build, "refs", "x", "old.png")), "a REF still stays")
            expect(not any(os.path.exists(os.path.join(idle, n)) for n in ("frames", "shot.png")),
                   "rule 7: an idle scratchpad's old frames go")
            expect(all(os.path.exists(os.path.join(idle, n)) for n in ("caps", "recent", "notes.py")),
                   "rule 7: its caps folder, a 2-day folder and its scripts stay")
            expect(os.path.exists(os.path.join(live, "frames", "f.ppm")), "rule 7: a live scratchpad is untouched")
            expect("removed 1 worktree or clone" in open(os.path.join(home, "prune.log")).read(), "the log")
        try:
            pr.rmtree(os.path.join(home, "gt4-native-game", "x"))
            expect(False, "rmtree refuses a protected path")
        except OSError:
            pass
    print(f"prune self-test: {'all right' if not failed else str(len(failed)) + ' failed'}")
    return 1 if failed else 0


if __name__ == "__main__":
    main()

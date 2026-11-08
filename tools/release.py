#!/usr/bin/env python3
"""One commit, one command: a release's archives, BUILD-INFO, checksums and a draft note (PLAN.md C11).

    py -I tools/release.py --version VERSION --decomp C:/path/to/gt4-decomp [--out build/release/VERSION]
                           [--jobs 4] [--min-gb 7] [--no-slots] [--keep-worktree]

It makes files only and publishes nothing. In order:
1. The checkout must be clean (no change and no untracked file outside what .gitignore keeps out): the release is
   its commit (HEAD). A fresh worktree of that commit under build/release-work/ holds every build, so each one starts
   from an empty folder.
2. The game, once: tools/release_game.py (the decomp's sources at its commit, this commit's game_src/ and runtime,
   the provisional drafts staged for them). Its FFmpeg is the checkout's build/ffmpeg-i686 unless GT4_FFMPEG says.
3. The hosts, each from an empty folder: tools/build_windows.cmd (x64, with its ctest) and tools/build_linux.sh
   aarch64 (on Windows through WSL, into WSL's own file system, then copied into the worktree).
4. The archives: tools/package_game.py --release VERSION, --arch x64 (the Windows zip) and --arch linux-arm64 (the
   tar), the same gt4_game.exe in both. package_game checks each host's architecture (and the Linux host's glibc),
   refuses any file of the game's (.iso, .elf, saves, BIOS) and any block of the original executable.
5. In OUT: SHA256SUMS (checked again after writing), BUILD-INFO.txt (the commits, every program's SHA-256, the
   tools that built them) and RELEASE-NOTES-VERSION.md, a draft from CHANGELOG.md's newest section with the
   archives' names and SHA-256s and the device line. The steps' logs go to OUT/logs/.

By hand before tagging (live checks this script can't make: they need the disc image and a save):
- tools/photo_check.py with the archive's host and game: Photo Travel's photo is a picture, not black (0.1.0 shipped
  it black: its tiles were read from GS memory, not the GPU).

On this machine every build step takes its slot through tools/slot.py (the game slot, a heavy slot) and waits for
--min-gb of available memory; --no-slots runs them at once (a machine without the slots' folder skips them too).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import slot as slots  # noqa: E402

WINDOWS = os.name == "nt"
UNTESTED = ("The linux-arm64 package is untested on the device until the owner's test of R4's points "
            "(docs/modern/spikes/R4-link-across-fex.md: the window under gamescope, sound, the controls, a race's "
            "speed, Steam). The Windows package runs on Windows x64.")


def git(*args, cwd=ROOT):
    return subprocess.run(["git", "-C", cwd] + list(args), capture_output=True, text=True, check=True).stdout.strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


class Steps:
    """Runs each step's command with its output in OUT/logs/STEP.log, inside its slot (tools/slot.py)."""

    def __init__(self, out, use_slots, min_gb, version):
        self.logs = os.path.join(out, "logs")
        os.makedirs(self.logs, exist_ok=True)
        self.use_slots = use_slots and os.path.isdir(slots.locks_dir())
        self.min_gb, self.version = min_gb, version

    def run(self, name, cmd, kinds=(), cwd=None, env=None):
        log = os.path.join(self.logs, name + ".log")
        print(f"[{time.strftime('%H:%M:%S')}] {name}: {' '.join(cmd)}", flush=True)
        started = time.time()

        def go():
            with open(log, "w", encoding="utf-8") as f:
                return subprocess.run(cmd, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT).returncode
        if self.use_slots and kinds:
            with slots.held(list(kinds), f"release {self.version}: {name}", self.min_gb):
                rc = go()
        else:
            rc = go()
        print(f"[{time.strftime('%H:%M:%S')}] {name}: exit {rc} after {time.time() - started:.0f} s (log {log})",
              flush=True)
        if rc:
            tail = open(log, encoding="utf-8", errors="replace").read()[-3000:]
            raise SystemExit(f"{name} failed (exit {rc}); the end of {log}:\n{tail}")
        return log


def clean_commit(allow_unpushed):
    dirty = git("status", "--porcelain", "--untracked-files=normal")
    if dirty:
        raise SystemExit("the checkout has changes; a release is made from a commit, without them:\n" + dirty)
    commit = git("rev-parse", "HEAD")
    # A commit that is not on origin's main cannot be rebuilt by anyone else from BUILD-INFO's commits.
    fetched = subprocess.run(["git", "-C", ROOT, "fetch", "-q", "origin", "main"], capture_output=True, text=True)
    on_main = fetched.returncode == 0 and subprocess.run(
        ["git", "-C", ROOT, "merge-base", "--is-ancestor", commit, "FETCH_HEAD"]).returncode == 0
    if not on_main:
        why = "is not on origin's main" if fetched.returncode == 0 else f"could not be checked (git fetch: " \
              f"{fetched.stderr.strip()[-200:]})"
        if not allow_unpushed:
            raise SystemExit(f"{commit} {why}: nobody else could rebuild this release from its commits. Push it "
                             "first, or pass --allow-unpushed for a test release")
        print(f"warning: {commit} {why} (--allow-unpushed: a test release)", flush=True)
    return commit


def newest_changelog(root):
    text = open(os.path.join(root, "CHANGELOG.md"), encoding="utf-8").read()
    m = re.search(r"(?ms)^## (.+?)\n(.*?)(?=^## |\Z)", text)
    if not m:
        raise SystemExit("CHANGELOG.md has no '## ' section for the draft note")
    return m.group(1).strip(), m.group(2).strip()


def compiler_of(log):
    """The C++ compiler a CMake configure named in a step's log, e.g. 'MSVC 19.44.35229.0' or 'Clang 22.1.8'."""
    m = re.search(r"The CXX compiler identification is (.+)", open(log, encoding="utf-8", errors="replace").read())
    return m.group(1).strip() if m else "(not in the log)"


def cmake_of(build):
    """CMake's version from a build folder (CMakeFiles/<version>/), e.g. '3.31.6-msvc6'."""
    found = [n for n in os.listdir(os.path.join(build, "CMakeFiles")) if re.match(r"\d+\.\d+", n)] \
        if os.path.isdir(os.path.join(build, "CMakeFiles")) else []
    return "CMake " + found[0] if found else "CMake (version not found)"


def tool_version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return (out.stdout or out.stderr).strip().splitlines()[0]
    except (OSError, IndexError, subprocess.TimeoutExpired):
        return "(not found)"


def game_in(archive):
    """The SHA-256 of the gt4_game.exe inside a release archive."""
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            name = next((n for n in z.namelist() if n.endswith("/gt4_game.exe")), None)
            if name is None:
                raise SystemExit(f"{archive} holds no gt4_game.exe")
            return hashlib.sha256(z.read(name)).hexdigest()
    with tarfile.open(archive) as t:
        name = next((n for n in t.getnames() if n.endswith("/gt4_game.exe")), None)
        if name is None:
            raise SystemExit(f"{archive} holds no gt4_game.exe")
        return hashlib.sha256(t.extractfile(name).read()).hexdigest()


def linux_host(steps, work, short):
    """tools/build_linux.sh aarch64 from an empty folder; on Windows through WSL (its own file system: much faster
    than /mnt/c), the host copied to WORK/build/host-linux-arm64-Release/gt4_host."""
    dest = os.path.join(work, "build", "host-linux-arm64-Release")
    os.makedirs(dest, exist_ok=True)
    if not WINDOWS:
        env = dict(os.environ, GT4_LINUX_BUILD=dest)
        steps.run("host-linux-arm64", ["bash", os.path.join(work, "tools", "build_linux.sh"), "aarch64"],
                  kinds=("heavy",), cwd=work, env=env)
        return os.path.join(dest, "gt4_host")
    drive, rest = os.path.splitdrive(os.path.abspath(work))
    wsl_work = "/mnt/" + drive[0].lower() + rest.replace("\\", "/")
    build = f"$HOME/gt4-release/{short}/host-linux-arm64-Release"
    # (WSL does not see Windows' environment: build_linux.sh's own variables are passed on, as WSL paths)
    names = ("GT4_GLSLANG_EXECUTABLE", "GT4_LINUX_TOOLS", "GT4_JOBS")
    quote = lambda v: "'" + v.replace("'", "'\\''") + "'"   # (POSIX single quotes, a quote inside closed and reopened)
    passed = "".join(f"export {k}={quote(os.environ[k])}; " for k in names if os.environ.get(k))
    script = (f"set -e -o pipefail; rm -rf {build}; mkdir -p {build}; {passed}"
              f"GT4_LINUX_BUILD={build} bash '{wsl_work}/tools/build_linux.sh' aarch64; "
              f"cp {build}/gt4_host '{wsl_work}/build/host-linux-arm64-Release/gt4_host'; "
              f"rm -rf $HOME/gt4-release/{short}")
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    steps.run("host-linux-arm64", ["wsl.exe", "-e", "bash", "-c", script], kinds=("heavy",), env=env)
    return os.path.join(dest, "gt4_host")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True, help="the release's version (archive names, BUILD-INFO, the note)")
    ap.add_argument("--decomp", required=True, help="the gt4-decomp checkout (release_game.py, package_game.py)")
    ap.add_argument("--out", help="the folder for the release's files (default build/release/VERSION)")
    ap.add_argument("--jobs", default="4", help="release_game.py's compile jobs (default 4: the machine's rule)")
    ap.add_argument("--min-gb", type=float, default=7.0, help="available memory each step's slot waits for")
    ap.add_argument("--no-slots", action="store_true", help="run every step at once, without tools/slot.py")
    ap.add_argument("--keep-worktree", action="store_true", help="keep build/release-work/COMMIT after the release")
    ap.add_argument("--allow-unpushed", action="store_true",
                    help="release a commit that is not on origin's main (a test release: nobody else can rebuild it)")
    a = ap.parse_args()
    if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+_-]*", a.version):
        raise SystemExit(f"--version {a.version!r}: letters, digits, '.', '+', '_' and '-' only")
    commit = clean_commit(a.allow_unpushed)
    short = commit[:12]
    out = os.path.abspath(a.out or os.path.join(ROOT, "build", "release", a.version))
    if os.path.exists(out) and os.listdir(out):
        raise SystemExit(f"{out} is not empty: a release goes into an empty folder")
    os.makedirs(out, exist_ok=True)
    steps = Steps(out, not a.no_slots, a.min_gb, a.version)
    work = os.path.join(ROOT, "build", "release-work", short)
    if os.path.exists(work):
        git("worktree", "remove", "--force", work)
    git("worktree", "add", "--detach", work, commit)
    print(f"release {a.version} of {commit}, built in {work}", flush=True)
    try:
        env = dict(os.environ)
        env.setdefault("GT4_FFMPEG", os.path.join(ROOT, "build", "ffmpeg-i686"))
        py = [sys.executable, "-I"]
        game_out = os.path.join(work, "build", "game_release")
        steps.run("game", py + [os.path.join(work, "tools", "release_game.py"), "--decomp", a.decomp, "--out",
                                game_out, "--jobs", a.jobs], kinds=("game",), cwd=work, env=env)
        if WINDOWS:
            steps.run("host-windows-x64", ["cmd", "/c", os.path.join(work, "tools", "build_windows.cmd"), "Release"],
                      kinds=("heavy",), cwd=work, env=dict(os.environ, GT4_ARCH="x64"))
            win_host = os.path.join(work, "build", "host-windows-Release", "gt4_host.exe")
        else:
            win_host = None   # (the Windows host is built on Windows: this run makes the Linux archive only)
        lin_host = linux_host(steps, work, short)
        archives = []
        for arch in (["x64"] if win_host else []) + ["linux-arm64"]:
            steps.run(f"package-{arch}", py + [os.path.join(work, "tools", "package_game.py"), "--release", a.version,
                                               "--decomp", a.decomp, "--arch", arch, "--out", out], cwd=work)
        archives = sorted(f for f in os.listdir(out) if f.endswith((".zip", ".tar.gz")))
        players = [f for f in archives if "-windows-" in f or "-linux-" in f]
        # one game in both
        stamp = json.load(open(os.path.join(game_out, "BUILD-STAMP.json"), encoding="utf-8"))
        games = {f: game_in(os.path.join(out, f)) for f in players}
        if len(set(games.values())) != 1 or stamp["game_sha256"] not in games.values():
            raise SystemExit(f"the archives do not hold the one game built: {games}, the stamp's "
                             f"{stamp['game_sha256']}")
        # checksums, checked again from the files once written
        sums = {f: sha256(os.path.join(out, f)) for f in archives}
        with open(os.path.join(out, "SHA256SUMS"), "w", newline="\n") as f:
            f.writelines(f"{h}  {n}\n" for n, h in sums.items())
        for line in open(os.path.join(out, "SHA256SUMS")):
            h, n = line.split()
            if sha256(os.path.join(out, n)) != h:
                raise SystemExit(f"SHA256SUMS: {n} does not match")
            side = os.path.join(out, n + ".sha256")
            if os.path.isfile(side) and open(side).read().split()[0] != h:
                raise SystemExit(f"{n}.sha256 does not match SHA256SUMS")
        # BUILD-INFO: the commits, every program, the tools
        zig = re.search(r'zig_dir="\$tools/zig-x86_64-linux-([0-9.]+)"',
                        open(os.path.join(work, "tools", "build_linux.sh"), encoding="utf-8").read())
        info = [f"GT4 Native {a.version}",
                f"gt4-native commit: {commit}",
                f"gt4-decomp commit (the game's sources): {stamp.get('decomp_commit', '?')}",
                f"built {time.strftime('%Y-%m-%d %H:%M:%S %z')} by tools/release.py, each program from an empty "
                "folder",
                "",
                f"gt4_game.exe SHA-256: {stamp['game_sha256']} (tools/release_game.py; the same in every archive)",
                f"  {len(stamp.get('curated', {}))} native corrections, {len(stamp.get('drafts', {}))} provisional "
                f"drafts (their list: BUILD-INFO.txt in each archive)",
                # (the drafts are staged from the decomp's draft store, which is not versioned: two releases of the
                # same commits are told apart by these)
                f"  draft set SHA-256: {stamp.get('drafts_set_sha256', '(not recorded)')}",
                f"  draft list SHA-256: "
                f"{hashlib.sha256(json.dumps(stamp.get('draft_list', []), sort_keys=True).encode()).hexdigest()}"]
        if win_host:
            info.append(f"gt4_host.exe (Windows x64) SHA-256: {sha256(win_host)}")
        info.append(f"gt4_host (Linux aarch64) SHA-256: {sha256(lin_host)}")
        windows_tools = (f"{compiler_of(os.path.join(steps.logs, 'host-windows-x64.log'))}, "
                         f"{cmake_of(os.path.join(work, 'build', 'host-windows-Release'))}") if win_host else "-"
        mingw = os.environ.get("GT4_MINGW32", r"C:/msys64/mingw32/bin")
        runtime_cc = tool_version([os.path.join(mingw, "g++.exe") if WINDOWS else "i686-w64-mingw32-g++", "--version"])
        info += ["", "Tools:",
                 f"  Windows host: {windows_tools}",
                 f"  Linux aarch64 host: {compiler_of(os.path.join(steps.logs, 'host-linux-arm64.log'))} (zig "
                 f"{zig.group(1) if zig else '?'}, tools/build_linux.sh: glibc 2.35 at most)",
                 f"  game: {runtime_cc} (the runtime) and the decomp's EE compiler profiles (tools/game_cc.py)",
                 f"  {tool_version([sys.executable, '--version'])}, {tool_version(['git', '--version'])}",
                 "", "Archives (SHA256SUMS):"] + [f"  {h}  {n}" for n, h in sums.items()]
        with open(os.path.join(out, "BUILD-INFO.txt"), "w", newline="\n", encoding="utf-8") as f:
            f.write("\n".join(info) + "\n")
        # the draft note
        heading, body = newest_changelog(work)
        stale = [] if heading.split()[0] == a.version else [
            f"**CHANGELOG.md has no section for {a.version} yet: the text below is {heading}'s. Write the "
            f"{a.version} section and make the release again.**", ""]
        note = [f"# GT4 Native {a.version} (draft)", "",
                f"From commit {commit}. Draft made by tools/release.py from CHANGELOG.md's newest section "
                f"({heading}): edit before publishing.", ""] + stale + [body, "", "## Downloads", "",
                "| File | SHA-256 |", "|---|---|"] + [f"| `{n}` | `{h}` |" for n, h in sums.items()] + \
               ["", UNTESTED, "", "No game data is included: you need your own Gran Turismo 4 disc (USA, SCUS-97328, "
                "version 1.01). See the README in each archive."]
        with open(os.path.join(out, f"RELEASE-NOTES-{a.version}.md"), "w", newline="\n", encoding="utf-8") as f:
            f.write("\n".join(note) + "\n")
        print(f"\nrelease {a.version} in {out}:")
        for n, h in sums.items():
            print(f"  {h}  {n}")
        print(f"  gt4_game.exe {stamp['game_sha256']} in {len(players)} archives")
        print("nothing was published")
    finally:
        if not a.keep_worktree:
            git("worktree", "remove", "--force", work)


if __name__ == "__main__":
    main()

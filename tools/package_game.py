#!/usr/bin/env python3
"""Package the native game as one folder to copy anywhere: another PC, or a Linux handheld (Steam Deck, an ARM
handheld under Proton with FEX). gt4_host.exe started with no arguments finds everything beside it
(platform/sdl/main.cpp, usePackagedGame).

    py tools/package_game.py --decomp C:\\path\\to\\gt4-decomp [--save CARD] [--profile desktop|handheld]
                             [--textures DIR] [--out build/package]

Writes OUT/GT4-Native.zip:
    GT4-Native/gt4_host.exe, gt4_game.exe        the host (x64) and the game (x86)
    GT4-Native/msvcp140.dll, vcruntime140*.dll   the host's C++ runtime (the Visual C++ redistributable's files)
    GT4-Native/game/CORE.GT4.elf, game/*.iso     the user's own disc image and the executable made from it
    GT4-Native/saves/                            the save folder (--save: imported from a PS2 memory card image)
    GT4-Native/settings.ini                      the profile's settings (the host completes it on first start)
    GT4-Native/README.txt
and with --textures DIR (a texture pack, as textures/SCUS-97328 holds it) OUT/GT4-Native-HD-Textures.zip, to be
extracted over the same folder. The game data stays the user's: never distribute these archives.

A public release holds no game data at all:

    py tools/release_game.py --decomp C:\\path\\to\\gt4-decomp            (the game, from an empty folder)
    py tools/package_game.py --release 0.1.1 --decomp C:\\path\\to\\gt4-decomp [--game-build build/game_release]
                             [--out build/package]

takes gt4_game.exe only from a clean build (tools/release_game.py): it refuses one whose BUILD-STAMP.json does not
match the exe, or whose inputs (either tree) have changed since. It writes OUT/GT4-Native-VERSION-game-sources.zip
(the decomp's files at the stamped commit, the native corrections and drafts as compiled, the stamp) and
OUT/GT4-Native-VERSION-windows-ARCH.zip (--arch x64, the default, or arm64: a cross build's host from
build/host-windows-arm64-Release, tools/build_windows.cmd with GT4_ARCH=arm64, and the arm64 C++ runtime; built by
CI, PLAN.md C4):
    GT4-Native-VERSION/gt4_host.exe, gt4_game.exe, msvcp140.dll, vcruntime140*.dll
    GT4-Native-VERSION/settings.ini, settings-handheld.ini   the desktop and handheld profiles
    GT4-Native-VERSION/settings-playtest.ini   docs/modern/settings-playtest.ini: the opt-in work, to try
    GT4-Native-VERSION/README.txt, LICENSE, THIRD_PARTY_NOTICES.md, CREDITS.md, BUILD-INFO.txt
    GT4-Native-VERSION/textures/SCUS-97328/HOW-TO.txt     where an HD texture pack goes
    GT4-Native-VERSION/game/PUT-YOUR-GAME-HERE.txt           the player's disc image goes here; gt4_host.exe makes
                                                             game/CORE.GT4.elf from it on the first start
    GT4-Native-VERSION/saves/                                empty
--arch linux-arm64 or linux-x64 writes OUT/GT4-Native-VERSION-linux-ARCH.tar.gz instead, for a Linux handheld
(the Odin under Armada, a Steam Deck): the host built for Linux (tools/build_linux.sh; --host gives its path, default
build/host-linux-ARCH-Release/gt4_host), run natively, and the same gt4_game.exe under Proton's Wine:
    GT4-Native-VERSION/gt4_host, gt4-native.sh, gt4_game.exe     gt4-native.sh starts the host with Proton's Wine
                                                                  as the game's launcher (GT4_GAME_LAUNCHER, WINESERVER,
                                                                  PATH; GT4_PROTON chooses the Proton)
    and the same settings, documents, game/, textures/ and saves/ as the Windows package.
The host is checked to be an ELF for that architecture needing no glibc newer than 2.35.
BUILD-INFO.txt names this repository's commit, the game build's two commits (gt4-decomp: the game's sources;
gt4-native: game_src/ and the runtime), the executables' SHA-256 and the provisional drafts. The executables are
also checked for blocks of the original executable (orig/SCUS-97328/CORE.GT4.elf): a release must not carry any.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build", "host-windows-Release", "gt4_host.exe")
GAME = os.path.join(ROOT, "build", "game", "gt4_game.exe")
NAME = "GT4-Native"

# A handheld's settings: the low preset (render below the screen's resolution, FSR 1 up), borderless (gamescope
# shows it full screen), each game frame presented once (no frame interpolation: half the work on a 120 Hz screen),
# the 16:9 game. Stated as well, whatever the preset says: no ambient occlusion, FSR 1, the game's own rear-view
# mirror (the high one draws the course and cars again), whole frames in races. Everything else at the host's defaults.
# The packaged settings files' first line: platform/sdl/settings.h's Settings::kVersion. A file from an older version
# is written back whole at the host's first interactive start (MENU_DESIGN M9f2), so they say the current one.
SETTINGS_HEADER = "version = 2\n\n"
PROFILES = {
    "desktop": "",
    "handheld": "[graphics]\npreset = low\n\n[render]\nupscaler = fsr1\n\n[effects]\nambient_occlusion = off\nmirror = original\n\n"
                "[display]\nmode = borderless\nframe_interpolation = off\nrace_frames = progressive\n\n[aspect]\nratio = 16:9\n",
}

README = """Gran Turismo 4, native
=======================

Start gt4_host.exe. It runs the game from game/ (CORE.GT4.elf and the disc image) with the settings in
settings.ini and the saves in saves/, all in this folder.

Controls: a gamepad (the PS2 pad's layout), or the keyboard. Escape, Guide or Select+Start: Settings (over
a running game the game's pages, in a race through its pause; with no game the host's own).

On Linux (a Steam Deck, Armada or another SteamOS-like system on an ARM handheld):
  1. Extract this folder anywhere (the internal storage or an SD card).
  2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse: gt4_host.exe.
  3. The new entry's Properties > Compatibility: "Force the use of a specific Steam Play compatibility tool",
     Proton Experimental (or the newest Proton listed).
  4. Start it from game mode.
The host is a 64-bit and the game a 32-bit Windows program: on an ARM handheld, Proton runs both through FEX.
game.log (the game) beside gt4_host.exe and Steam's Proton log (PROTON_LOG=1 %command% in the launch options)
say what went wrong if it does not start.

HD textures: extract GT4-Native-HD-Textures.zip into this folder (it adds textures/SCUS-97328/...);
HD textures in Settings (Graphics) switches it on and off.
"""


RELEASE_README = """GT4 Native {version}
==============

Gran Turismo 4 running as a native PC program, from your own copy of the game. This package contains no game
data. Full instructions, controls, settings and known issues: https://github.com/Ganitzsh/gt4-native

1. Put an image of your own Gran Turismo 4 disc (USA, SCUS-97328, version 1.01) as an .iso file in the game folder.
2. Start gt4_host.exe. The first start makes game\\CORE.GT4.elf from the image (about a second).
3. Settings: Escape, or Guide or Select+Start on the pad.

Handheld (Steam Deck, ARM handhelds under Proton): rename settings-handheld.ini to settings.ini first.
To try the work that is in no preset yet: settings-playtest.ini (each line says what to look for), copied over
settings.ini after keeping a copy of yours.
PS2 saves: gt4_host.exe --import-save "path\\to\\Mcd001.ps2" copies them into the saves folder.
HD texture packs go in textures\\SCUS-97328.
Problems: report them with host.log and game.log from this folder.

Not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony Digital. Gran Turismo is a trademark
of Sony Interactive Entertainment. Licence: LICENSE (GPL-3.0); third-party notices: THIRD_PARTY_NOTICES.md.
"""

LINUX_README = """GT4 Native {version} for Linux ({arch})
==============

Gran Turismo 4 running as a native program, from your own copy of the game. This package contains no game data.
Full instructions, controls, settings and known issues: https://github.com/Ganitzsh/gt4-native

gt4_host runs natively; the game itself (gt4_game.exe, a 32-bit Windows program) runs under Proton's Wine, which on
an ARM handheld runs it through FEX. Steam with a Proton installed is all it needs ({proton}).

1. Put an image of your own Gran Turismo 4 disc (USA, SCUS-97328, version 1.01) as an .iso file in the game folder.
2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse: gt4-native.sh in this folder. Do NOT set a
   compatibility tool for it: the script runs natively and starts Proton's Wine itself. Start it from game mode.
   From a terminal: ./gt4-native.sh
3. The first start makes game/CORE.GT4.elf from the image (about a second) and the Wine prefix
   (~/.local/share/gt4-native/prefix, WINEPREFIX to change it), which takes longer.
4. Settings: Escape, or Guide or Select+Start on the pad.

GT4_PROTON=/path/to/a/Proton/folder ./gt4-native.sh uses that Proton (by default the newest Steam has, then
~/.steam/root/compatibilitytools.d and /usr/share/steam/compatibilitytools.d).
Handheld: rename settings-handheld.ini to settings.ini first.
To try the work that is in no preset yet: settings-playtest.ini (each line says what to look for), copied over
settings.ini after keeping a copy of yours.
PS2 saves: ./gt4_host --import-save /path/to/Mcd001.ps2 copies them into the saves folder.
HD texture packs go in textures/SCUS-97328.
Problems: report them with host.log and game.log from this folder.

Not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony Digital. Gran Turismo is a trademark
of Sony Interactive Entertainment. Licence: LICENSE (GPL-3.0); third-party notices: THIRD_PARTY_NOTICES.md.
"""

# Starts the Linux host with Proton's Wine as the game's launcher. Proton's folder names have spaces ("Proton 11.0
# (ARM64)"), and GT4_GAME_LAUNCHER is split on spaces: so its bin folder goes first on PATH and the launcher is "wine".
# Proton's Wine also needs its own wineserver (WINESERVER), or it stops with "could not exec wineserver"
# (docs/modern/spikes/R4-link-across-fex.md, on the Odin).
LINUX_LAUNCHER = """#!/bin/sh
# GT4 Native: gt4_host natively, the game (gt4_game.exe) under Proton's Wine. GT4_PROTON=<a Proton folder> chooses
# the Proton; WINEPREFIX the Wine prefix (default ~/.local/share/gt4-native/prefix).
here=$(cd "$(dirname "$0")" && pwd)
case $(uname -m) in
    aarch64|arm64) bin=bin-arm64 ;;
    *) bin=bin ;;
esac
proton=${GT4_PROTON:-}
if [ -z "$proton" ]; then
    IFS='
'
    for p in $( { ls -1d "$HOME"/.local/share/Steam/steamapps/common/Proton* "$HOME"/.steam/steam/steamapps/common/Proton* \
                  2>/dev/null | sort -rV; ls -1d "$HOME"/.steam/root/compatibilitytools.d/* \
                  /usr/share/steam/compatibilitytools.d/* 2>/dev/null; } ); do
        if [ -x "$p/files/$bin/wine" ]; then proton=$p; break; fi
    done
    unset IFS
fi
if [ -z "$proton" ] || [ ! -x "$proton/files/$bin/wine" ]; then
    echo "gt4-native.sh: no Proton with files/$bin/wine found${proton:+ in $proton}: install one in Steam, or set GT4_PROTON" >&2
    exit 1
fi
export PATH="$proton/files/$bin:$PATH"
export WINESERVER="$proton/files/$bin/wineserver"
export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/gt4-native/prefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
export GT4_GAME_LAUNCHER=wine
mkdir -p "$WINEPREFIX"
echo "gt4-native.sh: Proton $proton, prefix $WINEPREFIX"
exec "$here/gt4_host" "$@"
"""

LINUX_ARCHES = {"linux-arm64": (183, "aarch64", "Proton 11.0 (ARM64) or newer, or proton-cachyos arm64"),
                "linux-x64": (62, "x86_64", "any Proton")}


def linux_host_check(path, arch):
    """A Linux host for `arch`: an ELF of that machine whose glibc symbol versions go no higher than 2.35."""
    import re
    import struct
    raw = open(path, "rb").read()
    machine, _, _ = LINUX_ARCHES[arch]
    if raw[:4] != b"\x7fELF" or raw[4] != 2 or struct.unpack_from("<H", raw, 18)[0] != machine:
        raise SystemExit(f"{path} is not a 64-bit {arch} Linux program")
    versions = sorted({tuple(int(x) for x in m.split(b".")) for m in re.findall(rb"GLIBC_(2\.[0-9]+(?:\.[0-9]+)?)\x00", raw)})
    if not versions or versions[-1] > (2, 35):
        raise SystemExit(f"{path} needs glibc {'.'.join(map(str, versions[-1])) if versions else '(none found)'}: "
                         "the package's host must need 2.35 at most (tools/build_linux.sh)")
    return ".".join(map(str, versions[-1]))


TEXTURES_NOTE = """Put an HD texture pack for Gran Turismo 4 (USA, SCUS-97328) here: its PNG files, in any subfolders, as PCSX2
keeps them. Switch it on and off with HD textures in Settings (Graphics).
"""

GAME_FOLDER_NOTE = """Put an image of your own Gran Turismo 4 disc here, as a .iso file:
USA release SCUS-97328, version 1.01 (other releases are not supported).

On the first start gt4_host.exe makes CORE.GT4.elf here from it.
"""

# What a release archive may contain beside the programs: anything else (a disc image, an executable taken from the
# game, a save) stops the packaging.
RELEASE_DOCS = ("LICENSE", "THIRD_PARTY_NOTICES.md", "CREDITS.md")
# Shipped beside settings-handheld.ini when the tree has it: the work in no preset yet, switched on to try, each line
# saying what to look for (not a preset; the owner's playtest, 2026-11-09).
PLAYTEST = os.path.join("docs", "modern", "settings-playtest.ini")


def playtest_text(root):
    path = os.path.join(root, PLAYTEST)
    return open(path, encoding="utf-8").read() if os.path.isfile(path) else None


def git_head(folder):
    r = subprocess.run(["git", "-C", folder, "rev-parse", "HEAD"], capture_output=True, text=True)
    dirty = subprocess.run(["git", "-C", folder, "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    return r.stdout.strip() + (" (with uncommitted changes)" if dirty else "")


def original_blocks(exe, elf, block=4096, step=65536):
    """Blocks of the original executable's segments found in exe: [(address, exe offset)]. The game's compiled
    constants are small; a whole 4 KB block of the original image means its bytes were carried over."""
    import struct
    raw = open(elf, "rb").read()
    data = open(exe, "rb").read()
    phoff, = struct.unpack_from("<I", raw, 28)
    count, = struct.unpack_from("<H", raw, 44)
    found = []
    for i in range(count):
        _, off, vaddr, _, filesz, *_ = struct.unpack_from("<8I", raw, phoff + 32 * i)
        for pos in range(0, filesz - block, step):
            chunk = raw[off + pos:off + pos + block]
            if chunk.count(chunk[:1]) == block:   # a run of one byte value (zero fill) proves nothing
                continue
            at = data.find(chunk)
            if at >= 0:
                found.append((vaddr + pos, at))
    return found


def checked_game_build(build, decomp, root):
    """The stamp of a clean game build (tools/release_game.py), after checking that it still describes the exe and
    the trees: refuses a game built incrementally, rebuilt since, or whose inputs have changed since."""
    import hashlib
    import json
    stamp_path = os.path.join(build, "BUILD-STAMP.json")
    game = os.path.join(build, "gt4_game.exe")
    if not os.path.isfile(stamp_path) or not os.path.isfile(game):
        raise SystemExit(f"{build} is not a clean game build (no BUILD-STAMP.json): run tools/release_game.py")
    stamp = json.load(open(stamp_path))
    if hashlib.sha256(open(game, "rb").read()).hexdigest() != stamp["game_sha256"]:
        raise SystemExit(f"{game} is not the exe its stamp describes: rebuild with tools/release_game.py")
    for repo, commit, paths, what in ((decomp, stamp["decomp_commit"], ["src", "include", "config", "project.toml"],
                                       "gt4-decomp"),
                                      (root, stamp["native_commit"], ["game_src", "compat", "runtime", "platform/ui",
                                       "platform/audio/spu", "include", "tools/game_cc.py", "tools/game_link.py",
                                       "tools/draft_natives.py", "tools/game_toolchain"], "gt4-native")):
        changed = subprocess.run(["git", "-C", repo, "diff", "--name-only", commit, "--"] + paths,
                                 capture_output=True, text=True, check=True).stdout.split()
        untracked = subprocess.run(["git", "-C", repo, "ls-files", "-o", "--exclude-standard", "--"] + paths,
                                   capture_output=True, text=True, check=True).stdout.split()
        if changed or untracked:
            raise SystemExit(f"the game's {what} inputs changed since the build ({commit[:10]}): "
                             f"{(changed + untracked)[:5]}...: rebuild with tools/release_game.py")
    snap = os.path.join(build, "sources")
    for kind, folder in (("curated", "native_curated"), ("drafts", "native_drafts")):
        for n, digest in stamp[kind].items():
            p = os.path.join(snap, folder, n)
            if not os.path.isfile(p) or hashlib.sha256(open(p, "rb").read()).hexdigest() != digest:
                raise SystemExit(f"{p} differs from the stamp")
    return stamp


SOURCES_NOTE = """The game sources gt4_game.exe of GT4 Native {version} was compiled from (tools/release_game.py, from an
empty build folder; BUILD-STAMP.json lists every input's SHA-256).

- src/, include/, config/, project.toml, LICENSE: gt4-decomp at commit {decomp}
  (a fork of https://github.com/gustavocanepa/gt4-decomp, MIT licence: LICENSE).
- native_curated/: gt4-native's game_src/ at commit {native}: native corrections, which replace the decomp's
  function of the same address.
- native_drafts/: provisional sources for {drafts} functions the decompilation has not matched yet, chosen by
  gt4-native's tools/draft_natives.py from the decompilation's draft store (where each came from: DRAFTS.txt).

To rebuild: check out gt4-decomp at that commit and gt4-native at {native}, and run in gt4-native, with this
folder's native_drafts/ and DRAFTS.txt (the set's SHA-256: {drafts_set}):
  py tools/release_game.py --decomp DECOMP --drafts native_drafts --draft-list DRAFTS.txt
The exe's SHA-256 must then be {sha}, in any folder.
"""


def game_sources_zip(path, stamp, build, decomp, version):
    """The game's sources as compiled: the decomp's files at the stamped commit (git archive), the curated and draft
    sources as staged, the stamp itself and a note."""
    import io
    import json
    import tarfile
    tar = subprocess.run(["git", "-C", decomp, "archive", "--format=tar", stamp["decomp_commit"], "--",
                          "src", "include", "config", "project.toml", "LICENSE"], capture_output=True, check=True).stdout
    top = "gt4-decomp"
    with zipfile.ZipFile(path + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        with tarfile.open(fileobj=io.BytesIO(tar)) as t:
            for m in t.getmembers():
                if m.isfile():
                    z.writestr(f"{top}/{m.name}", t.extractfile(m).read())
        add_tree(z, os.path.join(build, "sources", "native_curated"), f"{top}/native_curated")
        add_tree(z, os.path.join(build, "sources", "native_drafts"), f"{top}/native_drafts")
        z.writestr(f"{top}/BUILD-STAMP.json", json.dumps(stamp, indent=1) + "\n")
        z.writestr(f"{top}/DRAFTS.txt", "\n".join(stamp.get("draft_list") or ["(no list)"]) + "\n")
        z.writestr(f"{top}/SOURCES-FOR-GT4-NATIVE-{version}.txt", SOURCES_NOTE.format(
            version=version, decomp=stamp["decomp_commit"], native=stamp["native_commit"],
            drafts=len(stamp["drafts"]), sha=stamp["game_sha256"], drafts_set=stamp.get("drafts_set_sha256", "?")))
        bad = [n for n in z.namelist() if n.lower().endswith((".iso", ".elf", ".ps2", ".psu", ".bin", ".gt4"))
               or "/orig/" in n]
    if bad:
        os.remove(path + ".part")
        raise SystemExit(f"game data in the sources archive: {bad[:5]}")
    os.replace(path + ".part", path)


def release(a):
    import hashlib
    name = f"{NAME}-{a.release}"
    root = a.build_root or ROOT
    if not a.decomp:
        raise SystemExit("--release needs --decomp (the game build's sources are checked against it)")
    linux = a.arch.startswith("linux-")
    if a.host:
        host = a.host
    elif linux:
        host = os.path.join(root, "build", f"host-{a.arch}-Release", "gt4_host")
    else:
        host = os.path.join(root, os.path.relpath(HOST, ROOT)) if a.arch == "x64" else \
            os.path.join(root, "build", "host-windows-arm64-Release", "gt4_host.exe")
    build = a.game_build or os.path.join(root, "build", "game_release")
    stamp = checked_game_build(build, a.decomp, root)
    game = os.path.join(build, "gt4_game.exe")
    for f in (host, game):
        if not os.path.isfile(f):
            raise SystemExit(f"missing {f}")
    glibc = linux_host_check(host, a.arch) if linux else None
    info = [f"GT4 Native {a.release}", f"gt4-native commit: {git_head(root)}",
            f"gt4_game.exe built from an empty folder (tools/release_game.py) on {stamp['built']}:",
            f"  gt4-decomp commit (the game's sources): {stamp['decomp_commit']}",
            f"  gt4-native commit (game_src/, the runtime): {stamp['native_commit']}",
            f"  {len(stamp['curated'])} native corrections (game_src/), {len(stamp['drafts'])} provisional drafts "
            f"(listed below; sources in the game-sources zip)",
            f"  drafts staged {stamp.get('drafts_staged', 'from a folder staged before the build (not checked)')}",
            f"  draft set SHA-256: {stamp.get('drafts_set_sha256', '(not recorded)')}"]
    elf = os.path.join(a.decomp, "orig", "SCUS-97328", "CORE.GT4.elf")
    for exe in (host, game):
        hits = original_blocks(exe, elf)
        if hits:
            raise SystemExit(f"{exe} holds {len(hits)} blocks of the original executable, e.g. address "
                             f"{hits[0][0]:08X} at offset {hits[0][1]:#x}: not releasable")
    print("no block of the original executable in either program")
    for f in (host, game):
        info.append(f"{os.path.basename(f)} SHA-256: {hashlib.sha256(open(f, 'rb').read()).hexdigest()}")
    if glibc:
        info.append(f"gt4_host: {LINUX_ARCHES[a.arch][1]} Linux, needs glibc {glibc} at most")
    info += ["", "Provisional drafts (address, source in the decomp's draft store, kind, differing/total instructions):"]
    info += [l for l in (stamp.get("draft_list") or ["(no list)"]) if not l.startswith("#")]

    os.makedirs(a.out, exist_ok=True)
    sources = os.path.join(a.out, f"{name}-game-sources.zip")
    print(f"writing {sources}")
    game_sources_zip(sources, stamp, build, a.decomp, a.release)
    if linux:
        return linux_release(a, name, host, game, root, info, sources)
    archive = os.path.join(a.out, f"{name}-windows-{a.arch}.zip")
    files = [(f, os.path.basename(f)) for f in [host, game] + vc_runtime(a.arch)]
    files += [(os.path.join(root, d), d) for d in RELEASE_DOCS if os.path.isfile(os.path.join(root, d))]
    texts = {
        "settings.ini": (SETTINGS_HEADER + PROFILES["desktop"], "\n"),
        "settings-handheld.ini": (SETTINGS_HEADER + PROFILES["handheld"], "\n"),
        "README.txt": (RELEASE_README.format(version=a.release), "\r\n"),
        "BUILD-INFO.txt": ("\n".join(info) + "\n", "\r\n"),
        "game/PUT-YOUR-GAME-HERE.txt": (GAME_FOLDER_NOTE, "\r\n"),
        "textures/SCUS-97328/HOW-TO.txt": (TEXTURES_NOTE, "\r\n"),
    }
    if playtest_text(root) is not None:
        texts["settings-playtest.ini"] = (playtest_text(root), "\n")
    print(f"writing {archive}")
    with zipfile.ZipFile(archive + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path, arc in files:
            z.write(path, f"{name}/{arc}")
        for arc, (text, newline) in texts.items():
            z.writestr(f"{name}/{arc}", text.replace("\n", newline))
        z.writestr(f"{name}/saves/", "")
        names = z.namelist()
    allowed = {f"{name}/{arc}" for _, arc in files} | {f"{name}/{arc}" for arc in texts} | {f"{name}/saves/"}
    if set(names) != allowed or any(n.lower().endswith((".iso", ".elf", ".ps2", ".psu", ".bin", ".gt4")) for n in names):
        os.remove(archive + ".part")
        raise SystemExit(f"unexpected entries: {sorted(set(names) - allowed)}")
    os.replace(archive + ".part", archive)
    digest = hashlib.sha256(open(archive, "rb").read()).hexdigest()
    with open(archive + ".sha256", "w", newline="\n") as f:
        f.write(f"{digest}  {os.path.basename(archive)}\n")
    print("\n".join(info[:9]))
    print(f"{archive}: {os.path.getsize(archive) / 2**20:.1f} MiB, SHA-256 {digest}")
    sdigest = hashlib.sha256(open(sources, "rb").read()).hexdigest()
    with open(sources + ".sha256", "w", newline="\n") as f:
        f.write(f"{sdigest}  {os.path.basename(sources)}\n")
    print(f"{sources}: {os.path.getsize(sources) / 2**20:.1f} MiB, SHA-256 {sdigest}")


def linux_release(a, name, host, game, root, info, sources):
    """The Linux package: a .tar.gz (it keeps gt4_host's and gt4-native.sh's execute bits), Unix line ends."""
    import hashlib
    import io
    import time
    archive = os.path.join(a.out, f"{name}-{a.arch}.tar.gz")
    files = [(host, "gt4_host", 0o755), (game, "gt4_game.exe", 0o644)]
    files += [(os.path.join(root, d), d, 0o644) for d in RELEASE_DOCS if os.path.isfile(os.path.join(root, d))]
    texts = {
        "gt4-native.sh": (LINUX_LAUNCHER, 0o755),
        "settings.ini": (SETTINGS_HEADER + PROFILES["desktop"], 0o644),
        "settings-handheld.ini": (SETTINGS_HEADER + PROFILES["handheld"], 0o644),
        "README.txt": (LINUX_README.format(version=a.release, arch=LINUX_ARCHES[a.arch][1],
                                           proton=LINUX_ARCHES[a.arch][2]), 0o644),
        "BUILD-INFO.txt": ("\n".join(info) + "\n", 0o644),
        "game/PUT-YOUR-GAME-HERE.txt": (GAME_FOLDER_NOTE, 0o644),
        "textures/SCUS-97328/HOW-TO.txt": (TEXTURES_NOTE, 0o644),
    }
    if playtest_text(root) is not None:
        texts["settings-playtest.ini"] = (playtest_text(root), 0o644)
    now = int(time.time())
    print(f"writing {archive}")
    with tarfile.open(archive + ".part", "w:gz", compresslevel=9, format=tarfile.PAX_FORMAT) as z:
        def entry(arc, mode, size=0, kind=tarfile.REGTYPE):
            info_ = tarfile.TarInfo(f"{name}/{arc}")
            info_.mode, info_.size, info_.type, info_.mtime = mode, size, kind, now
            info_.uname = info_.gname = ""
            return info_
        for folder in ("", "game", "saves", "textures", "textures/SCUS-97328"):
            z.addfile(entry(folder, 0o755, kind=tarfile.DIRTYPE))
        for path, arc, mode in files:
            data = open(path, "rb").read()
            z.addfile(entry(arc, mode, len(data)), io.BytesIO(data))
        for arc, (text, mode) in texts.items():
            data = text.encode("utf-8")
            z.addfile(entry(arc, mode, len(data)), io.BytesIO(data))
        names = z.getnames()
    allowed = {f"{name}/{arc}" for _, arc, _ in files} | {f"{name}/{arc}" for arc in texts} | \
        {name} | {f"{name}/{d}" for d in ("game", "saves", "textures", "textures/SCUS-97328")}
    if set(n.rstrip("/") for n in names) != allowed or \
            any(n.lower().endswith((".iso", ".elf", ".ps2", ".psu", ".bin", ".gt4")) for n in names):
        os.remove(archive + ".part")
        raise SystemExit(f"unexpected entries: {sorted(set(names) - allowed)}")
    os.replace(archive + ".part", archive)
    digest = hashlib.sha256(open(archive, "rb").read()).hexdigest()
    with open(archive + ".sha256", "w", newline="\n") as f:
        f.write(f"{digest}  {os.path.basename(archive)}\n")
    print("\n".join(info[:9]))
    print(f"{archive}: {os.path.getsize(archive) / 2**20:.1f} MiB, SHA-256 {digest}")
    sdigest = hashlib.sha256(open(sources, "rb").read()).hexdigest()
    with open(sources + ".sha256", "w", newline="\n") as f:
        f.write(f"{sdigest}  {os.path.basename(sources)}\n")
    print(f"{sources}: {os.path.getsize(sources) / 2**20:.1f} MiB, SHA-256 {sdigest}")


def vc_runtime(arch="x64"):
    """The C++ runtime DLLs gt4_host.exe imports, for its architecture (x64 or arm64), from Visual Studio's
    redistributable folder."""
    roots = glob.glob(rf"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\{arch}\Microsoft.VC1*.CRT")
    if not roots:
        raise SystemExit(f"Visual Studio's VC\\Redist folder for {arch} (Microsoft.VC143.CRT) not found"
                         + (" (the 'MSVC v143 ARM64' component; Windows ARM64 packages are built by CI)" if arch != "x64" else ""))
    crt = sorted(roots)[-1]
    files = [os.path.join(crt, n) for n in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")]
    for f in files:
        if not os.path.isfile(f):
            raise SystemExit(f"missing {f}")
    return files


def import_save(card, out):
    saves = os.path.join(out, "saves")
    shutil.rmtree(saves, ignore_errors=True)
    r = subprocess.run([HOST, "--import-save", card, "--saves", saves], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        raise SystemExit(f"importing {card} failed: {r.stderr}")
    return saves


def add_tree(z, folder, prefix):
    count = 0
    for base, _, names in os.walk(folder):
        for n in sorted(names):
            path = os.path.join(base, n)
            z.write(path, prefix + "/" + os.path.relpath(path, folder).replace(os.sep, "/"))
            count += 1
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--release", metavar="VERSION", help="a public release archive, without any game data")
    ap.add_argument("--build-root", help="with --release: the checkout whose build/ holds the programs and whose "
                    "commit BUILD-INFO names (default: this one)")
    ap.add_argument("--game-build", help="with --release: the clean game build folder (tools/release_game.py; "
                    "default BUILD_ROOT/build/game_release)")
    ap.add_argument("--decomp", help="gt4-decomp checkout (orig/SCUS-97328: CORE.GT4.elf, the .iso)")
    ap.add_argument("--save", help="a PS2 memory card image (or folder card, .psu) to take the saves from")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="desktop")
    ap.add_argument("--textures", help="a texture pack folder (textures/SCUS-97328) for the HD textures archive")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "package"))
    ap.add_argument("--arch", choices=("x64", "arm64", "linux-arm64", "linux-x64"), default="x64",
                    help="with --release: the host's system and architecture (arm64: a Windows cross build, built "
                    "by CI; linux-*: tools/build_linux.sh's host, a .tar.gz)")
    ap.add_argument("--host", help="with --release: the host program to package (default: the build folder's)")
    a = ap.parse_args()
    if a.arch != "x64" and not a.release:
        ap.error("--arch arm64 only builds a release archive (--release): the other packages run the host here")
    if a.release:
        return release(a)
    if not a.decomp:
        ap.error("--decomp is required (except with --release)")

    orig =os.path.join(a.decomp, "orig", "SCUS-97328")
    elf = os.path.join(orig, "CORE.GT4.elf")
    isos = sorted(glob.glob(os.path.join(orig, "*.iso")))
    for f in (HOST, GAME, elf):
        if not os.path.isfile(f):
            raise SystemExit(f"missing {f}")
    if not isos:
        raise SystemExit(f"no disc image (.iso) in {orig}")
    os.makedirs(a.out, exist_ok=True)

    work = os.path.join(a.out, "work")
    os.makedirs(work, exist_ok=True)
    saves = import_save(a.save, work) if a.save else None
    settings = os.path.join(work, "settings.ini")
    with open(settings, "w", newline="\n") as f:
        f.write(SETTINGS_HEADER + PROFILES[a.profile])
    readme = os.path.join(work, "README.txt")
    with open(readme, "w", newline="\r\n") as f:
        f.write(README)

    archive = os.path.join(a.out, NAME + ".zip")
    print(f"writing {archive}")
    # Deflate for the programs and saves, stored for the disc image (its data is compressed already: deflate gains a
    # few percent for minutes)
    with zipfile.ZipFile(archive + ".part", "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for f in [HOST, GAME] + vc_runtime() + [settings, readme]:
            z.write(f, NAME + "/" + os.path.basename(f))
        z.write(elf, NAME + "/game/CORE.GT4.elf")
        z.write(isos[0], NAME + "/game/" + os.path.basename(isos[0]), compress_type=zipfile.ZIP_STORED)
        if saves:
            add_tree(z, saves, NAME + "/saves")
        else:
            z.writestr(NAME + "/saves/", "")
    os.replace(archive + ".part", archive)
    print(f"  {os.path.getsize(archive) / 2**30:.2f} GiB")

    if a.textures:
        textures = os.path.join(a.out, NAME + "-HD-Textures.zip")
        print(f"writing {textures}")
        with zipfile.ZipFile(textures + ".part", "w", zipfile.ZIP_STORED, allowZip64=True) as z:   # PNGs: compressed
            n = add_tree(z, a.textures, NAME + "/textures/SCUS-97328")
        os.replace(textures + ".part", textures)
        print(f"  {n} files, {os.path.getsize(textures) / 2**30:.2f} GiB")
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()

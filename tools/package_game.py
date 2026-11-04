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
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, "build", "host-windows-Release", "gt4_host.exe")
GAME = os.path.join(ROOT, "build", "game", "gt4_game.exe")
NAME = "GT4-Native"

# A handheld's settings: the low preset (render below the screen's resolution, FSR 1 up), borderless (gamescope
# shows it full screen), each game frame presented once (no frame interpolation: half the work on a 120 Hz screen),
# the 16:9 game. Everything else at the host's defaults.
PROFILES = {
    "desktop": "",
    "handheld": "[graphics]\npreset = low\n\n[display]\nmode = borderless\n\n[aspect]\nratio = 16:9\n",
}

README = """Gran Turismo 4, native
=======================

Start gt4_host.exe. It runs the game from game/ (CORE.GT4.elf and the disc image) with the settings in
settings.ini and the saves in saves/, all in this folder.

Controls: a gamepad (the PS2 pad's layout), or the keyboard. Select+Start or Escape: the settings menu.

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
the settings menu's texture options switch it on and off.
"""


def vc_runtime():
    """The x64 C++ runtime DLLs gt4_host.exe imports, from Visual Studio's redistributable folder."""
    roots = glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC1*.CRT")
    if not roots:
        raise SystemExit("Visual Studio's VC\\Redist folder (Microsoft.VC143.CRT) not found")
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
    ap.add_argument("--decomp", required=True, help="gt4-decomp checkout (orig/SCUS-97328: CORE.GT4.elf, the .iso)")
    ap.add_argument("--save", help="a PS2 memory card image (or folder card, .psu) to take the saves from")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="desktop")
    ap.add_argument("--textures", help="a texture pack folder (textures/SCUS-97328) for the HD textures archive")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "package"))
    a = ap.parse_args()

    orig = os.path.join(a.decomp, "orig", "SCUS-97328")
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
        f.write("version = 1\n\n" + PROFILES[a.profile])
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

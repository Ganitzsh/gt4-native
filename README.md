# GT4 Native

**Gran Turismo 4 running as a native PC program, from your own copy of the game.** The game's code comes from the
[gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) decompilation, compiled for the PC, with a Vulkan renderer,
native sound, native saves and a settings menu of its own. There is no PS2 emulator in it.

It runs on Windows, and on Linux through Proton: Steam Deck, or an ARM handheld such as the AYN Odin 3 (Proton with
FEX).

> **Version 0.1.0, early.** The game boots, the menus, GT mode and races work, and saves load and save. Expect bugs:
> see [Known issues](#known-issues). Nothing from the game is included: you need your own disc.

Maintained by [Julien Ganichot](https://github.com/Ganitzsh). Based on Gustavo Canepa's
[gt4-native](https://github.com/gustavocanepa/gt4-native) and [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp):
see [Credits](#credits).

## What you need

- **Gran Turismo 4, NTSC-U (USA), version 1.01**, serial SCUS-97328, dumped from your own disc to an `.iso`
  ([Redump](http://redump.org/disc/955) CRC32 `d1d25931`). Other regions and versions are not supported.
- **Windows 10 or 11, 64-bit**, or **Linux with Steam and Proton**.
- **A GPU with Vulkan 1.3.**
- **Python 3**, once, to make `CORE.GT4.elf` from the disc (below). Nothing else to install.
- A gamepad with the PS2's layout (Xbox, PlayStation, Steam Deck controls), or a keyboard.

## Install

1. Download `GT4-Native-<version>-windows-x64.zip` from [Releases](https://github.com/Ganitzsh/gt4-native/releases) and
   extract it anywhere. You get a `GT4-Native-<version>` folder with `gt4_host.exe`, `gt4_game.exe`, an empty
   `game/` and an empty `saves/`.
2. Put your disc image in `game/`, for example `game/Gran Turismo 4 (USA) (v1.01).iso`.
3. Make `game/CORE.GT4.elf` from it. The game's executable is on the disc compressed; gt4-decomp's two scripts (Python
   standard library only) unpack it:

   ```sh
   git clone https://github.com/gustavocanepa/gt4-decomp
   python gt4-decomp/tools/iso_extract.py "GT4-Native-0.1.0/game/Gran Turismo 4 (USA) (v1.01).iso" CORE.GT4 -o gt4-files
   python gt4-decomp/tools/core2elf.py gt4-files/CORE.GT4 GT4-Native-0.1.0/game/CORE.GT4.elf
   ```

   The result's SHA-1 should be `eb6e17112038bbeab63b09d445112adcd957c900`. If it differs, the disc image is not
   the USA 1.01 one.
4. Start `gt4_host.exe`. It finds the game, the disc image, `settings.ini` and `saves/` beside itself, and writes
   `game.log` and `host.log` there.

The folder can be copied anywhere: another PC, an SD card, a handheld.

### Linux, Steam Deck and ARM handhelds

The same Windows executables run under Proton:

1. Do steps 1 to 3 above (on any computer), then copy the folder to the device.
2. Steam (desktop mode) > Games > Add a Non-Steam Game > Browse: `gt4_host.exe`.
3. The new entry's Properties > Compatibility: force Proton Experimental, or the newest Proton listed.
4. Start it from game mode.

On a handheld, use the handheld settings: copy `settings-handheld.ini` over `settings.ini` before the first start.
It renders below the screen's resolution with FSR 1, borderless, at 16:9, and keeps the frame rate at the game's 60.

## Settings menu

Press **Select + Start** on the pad, or **Escape** on the keyboard (the pad's Guide button works too), to open the
settings menu over the game. The game pauses while it is open.

| Action | Pad | Keyboard |
|---|---|---|
| Choose an option | D-pad or left stick up/down | W/S or arrow keys |
| Change its value | D-pad or left stick left/right | A/D or arrow keys |
| Jump between sections | L1 / R1 | Page Up / Page Down |
| Accept | Cross (south button) | Enter |
| Close the menu | Circle or Start | Escape or Backspace |

Changes apply at once and are saved to `settings.ini`. Options marked `*` (aspect ratio, menu layout) apply at the
next start. Each option shows a description. The main ones:

- **GRAPHICS**: the preset (`original`, `low`, `medium`, `high`, `ultra`), PS2 or native resolution, render scale,
  upscaler (FSR 1, FSR 2), sharpening, anti-aliasing, texture filtering, lighting, rear-view mirror detail,
  HD textures, shadows, reflections, bloom, depth of field, motion blur, tone mapping.
- **DISPLAY**: window or fullscreen, v-sync, **Frame rate limit**, **Frame interpolation**, aspect ratio, menu and
  HUD layout.
- **GAMEPLAY**: the AI's catch-up. Every other option leaves the game itself as it was.
- **IMAGE**: film grain, vignette, chromatic aberration, dither, and a performance readout (frame rate on screen).

The `original` preset shows the game as the PS2 drew it, at its resolution. Every option and its values are listed
in [docs/RENDERING.md](docs/RENDERING.md#settings-reference), and `gt4_host.exe --list-options` prints them.

**Frame rate and battery.** The game runs at 60 frames a second. With Frame interpolation on, the host draws extra
frames between the game's to match a faster screen (120 Hz, 144 Hz): smoother, but about twice the GPU work. On a
handheld or on battery, turn it off and leave Frame rate limit at 0: the host then shows each game frame once,
60 a second.

## Importing your saves

The game keeps its saves as plain folders in `saves/`, one per save (`BASCUS-97328GAMEDATA` holds the career and the
garage). They are the game's own files, so they move between this port, a PS2 and PCSX2 unchanged.

To bring a save from PCSX2, NetherSX2, AetherSX2 or a PS2 memory card dump, run once from the GT4-Native folder:

```sh
gt4_host.exe --import-save "path/to/Mcd001.ps2" --saves saves
```

`PATH` can be:
- a memory card image: PCSX2's `.ps2`, or a raw dump (`.bin`, `.mcd`);
- a PCSX2 folder memory card;
- a single save exported as `.psu` (uLaunchELF, PCSX2's memory card editor).

Only GT4 USA saves (`BASCUS-97328...`) are copied; other games' saves are left alone. A save already in `saves/` with
the same name is kept beside the new one, renamed `NAME.before-import-<date>`.

On Linux, run the import on a Windows PC, or copy the save folders by hand: a PCSX2 folder memory card already holds
them as folders (`BASCUS-97328GAMEDATA`), so copy those into `saves/`. To back your progress up, copy `saves/`.
More detail: [docs/SAVES.md](docs/SAVES.md).

## HD texture packs

The port can draw a texture pack's high-resolution images in place of the game's textures. It reads the packs
made for PCSX2's texture replacement (PNG files for the USA release, SCUS-97328), so a pack installed for PCSX2
works as it is. Known packs: the **GT4 Retexture Mod** by Hattersby and team (GTPlanet thread "Gran Turismo 4
Retexture Mod": tracks, cars, menus and HUD) and the **HD HUD & UI** pack by Silentwarior112. They are not part of
this project: get them from their authors.

To install one, put the pack's folder in `textures/SCUS-97328` inside the GT4-Native folder:

```
GT4-Native-0.1.0/
  gt4_host.exe
  textures/SCUS-97328/   <- the pack's PNG files, in any subfolders
```

A pack that comes in versions (for example 3.0, then 3.0.1 to 3.0.4) is installed in order, each one's files over
the last. If PCSX2 is installed on the same PC with a pack, the port finds PCSX2's `textures/SCUS-97328` too.

Switch it on and off with **HD textures** in the settings menu (GRAPHICS). It reads "Not installed" when no pack was
found at start. Textures load in the background the first time they're drawn, so the original shows for a few
frames first. Other places the port looks, and how matching works: [docs/TEXTURE_PACKS.md](docs/TEXTURE_PACKS.md).

## Known issues

- Car reflections show on the road in dry weather.
- Car wheels are sometimes drawn transparent, and the car pictures on the results screen have missing pixels.
- Frame drops with far cameras: rolling starts, replays, and looking back.
- Trading cars needs a second memory card and is not available.

Report problems on the [issue tracker](https://github.com/Ganitzsh/gt4-native/issues) with `game.log` and `host.log`
from the GT4-Native folder.

## Versions

Releases follow [semantic versioning](https://semver.org) and are tagged `vMAJOR.MINOR.PATCH` (first: `v0.1.0`).
What changed in each: [CHANGELOG.md](CHANGELOG.md).

## Building from source

Requires CMake 3.21+, a C++20 compiler and the Vulkan SDK. The game executable (`gt4_game.exe`) also needs a
gt4-decomp checkout and its toolchain: see [docs/NATIVE_GAME.md](docs/NATIVE_GAME.md) and
[docs/GAME_TOOLCHAIN.md](docs/GAME_TOOLCHAIN.md).

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

How the port works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/NATIVE_GAME.md](docs/NATIVE_GAME.md),
[docs/RENDERING.md](docs/RENDERING.md), [docs/PLATFORM.md](docs/PLATFORM.md). Fixes to the decompiled game code
belong upstream in [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp); changes to the port go here.

## Credits

- **[Gustavo Canepa](https://github.com/gustavocanepa)** started [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp),
  the matching decompilation of Gran Turismo 4 this port is built on, and the original
  [gt4-native](https://github.com/gustavocanepa/gt4-native), which this repository is a fork of.
- The [gt4-decomp contributors](https://github.com/gustavocanepa/gt4-decomp/graphs/contributors) and the Gran
  Turismo modding and reverse-engineering community.
- **[Julien Ganichot](https://github.com/Ganitzsh)**: this fork's playable build (the native game runtime, renderer,
  sound, saves, settings menu, packaging and Proton support).
- Third-party libraries (SDL3, FSR, glslang and others): [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

More in [CREDITS.md](CREDITS.md).

## Legal

Independent, unofficial project, not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony
Digital. Gran Turismo is a trademark of Sony Interactive Entertainment. No game data, disc image, original executable,
BIOS or proprietary SDK file is distributed here or in the releases: you need your own legally obtained copy of the
game.

This repository's code is licensed under the [GNU General Public License, version 3](LICENSE) (GPL-3.0).
Copyright (c) 2026 Gustavo Canepa; changes in this fork copyright (c) 2026 Julien Ganichot. Anyone who distributes
this port or a work based on it must make the corresponding source available under the same license.

Sources from [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) keep their own MIT license and notices; MIT
code may be combined with GPL code. Revisions up to and including commit `5799594b1fa25218f1d1c0b87602036d508aaf53`
were published under the MIT license and remain available under it. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/RELEASE_POLICY.md](docs/RELEASE_POLICY.md).

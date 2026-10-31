# Saves

The native game keeps its progress as plain files in a folder: no memory card image, no card slots. By default
the folder is `%LOCALAPPDATA%\gt4-native\saves`; `gt4_host --saves DIR` (or the game's `--saves` / `GT4_SAVES`)
picks another. A folder from before the name changed (`gt4-native\memcard1`) is moved there on the first start.

Inside, each save the game makes is a folder named as GT4 names it, with its files:

| Folder | What |
|---|---|
| `BASCUS-97328GAMEDATA` | the game data (`BASCUS-97328GAMEDATA`: the career, settings, licences) and the garage (`garage`), with the icon files the PS2's browser showed (`icon.sys`, `icon0.ico`) |
| other `BASCUS-97328...` folders | replays, photos and the other saves GT4 makes |

The files are the game's own data, as it writes them (it encrypts and checksums them itself), so they move
between the native game, a PS2 and PCSX2 unchanged. Copy the folder to back progress up.

## How the game reaches them

GT4's save code (its memory card manager, gt4-decomp unit 005547E8) calls Sony's libmc (`sceMcOpen`,
`sceMcRead`, `sceMcGetDir`, ...). `runtime/memcard.cpp` replaces those functions at their API and works on the
folder directly: no IOP, no memory card server, no card file system. What the game still sees of a card is what
its save code asks: one card in the first slot, always present and formatted, with the free space of an 8 MB
card; the second slot is empty (trading cars needs a second card: not available).

## Bringing a save from a PS2 or PCSX2

```
gt4_host --import-save PATH [--saves DIR]
```

copies GT4's saves (`BASCUS-97328...`, the NTSC-U version's) from `PATH` into the save folder, then exits. `PATH`
is one of:

- a memory card image: PCSX2's `.ps2` (8 MiB of 512-byte pages, each with its 16 bytes of ECC) or a raw card
  dump without ECC (`.bin`, `.mcd`); the card's file system is read (superblock, indirect FAT, directories);
- a PCSX2 folder memory card (its save folders are copied; PCSX2's `_pcsx2_*` index files are left);
- one save exported as `.psu` (uLaunchELF, PCSX2's memory card editor).

Other games' saves on the card are left. A save of the same name already in the folder is kept beside the new
one, renamed `NAME.before-import-YYYYMMDD-HHMMSS`. `platform/saves/save_import.cpp`; checked by
`tests/save_import.cpp` on card images, a `.psu` and a folder card built by the test, and on NetherSX2 (Android
PCSX2) `.ps2` cards holding fourteen games' saves (2026-10-31: GT4's came out and loaded). A directory's entry
count is the length in its parent's entry for it; a folder's own "." entry holds 0 on real cards.

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

## Saves are never left half written

- A file the game writes is written to a copy beside it (`.NAME.N.tmp`), flushed to the disk, and renamed over the
  file when the game closes it. If the game, the PC or the power stops in the middle of a save, the old file is
  still whole; the copy is removed at the next start.
- Before the first change a session makes to the saves (a save, a new or deleted save folder), the folder's saves
  are copied to `.backup/YYYYMMDD-HHMMSS/` inside it. The newest 10 are kept (`GT4_SAVE_BACKUPS=N` for another
  count, `0` for none). To go back to one, quit the game and copy its folders over the ones in the save folder.
- Names starting with `.` (the copies, `.backup`) are not on the card the game sees: not listed, not counted in its
  free space.

## How the game reaches them

GT4's save code (its memory card manager, gt4-decomp unit 005547E8) calls Sony's libmc (`sceMcOpen`,
`sceMcRead`, `sceMcGetDir`, ...). `runtime/memcard.cpp` replaces those functions at their API and works on the
folder directly: no IOP, no memory card server, no card file system. What the game still sees of a card is what
its save code asks: one card in the first slot, always present and formatted, with the free space of an 8 MB
card; the second slot is empty unless `GT4_SAVES2=DIR` names a folder for a second card (trading cars between
two cards; another folder than the first). `runtime/save_folder.cpp` holds the card logic and
`tests/memcard.cpp` checks it: a cut write, the hidden copies and backups, the rotation, the second slot.

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

Other games' saves on the card are left. A save of the same name already in the folder is moved to
`.backup/imports/YYYYMMDD-HHMMSS/NAME`, where the game does not list it and the backup rotation leaves it. `platform/saves/save_import.cpp`; checked by
`tests/save_import.cpp` on card images, a `.psu` and a folder card built by the test, and on NetherSX2 (Android
PCSX2) `.ps2` cards holding fourteen games' saves (2026-10-31: GT4's came out and loaded). A directory's entry
count is the length in its parent's entry for it; a folder's own "." entry holds 0 on real cards.

## Taking a save to a PS2 or PCSX2

```
gt4_host --export-save FILE.psu [--export-name NAME] [--saves DIR] [--force]   one save (default BASCUS-97328GAMEDATA)
gt4_host --export-save FILE.ps2 [--saves DIR] [--force]   a new PCSX2 memory card with every GT4 save of the folder
```

- A name without an extension gets `.psu`, the default: a `.psu` is imported onto a card by PCSX2 or a PS2 and cannot
  damage one.
- The output is always a new file, written whole beside it and then renamed. An existing FILE is refused and never
  opened; `--force` replaces it by that rename. No export ever modifies a card image in place.

- `gt4::saves::exportSave` (`platform/saves/save_import.h`) writes one save folder as a `.psu`, which PCSX2's memory
  card editor and uLaunchELF on a PS2 import.
- `gt4::saves::exportCard` writes every GT4 save of the folder onto a new 8 MiB card in PCSX2's `.ps2` format. The
  layout is the standard one the PS2 formats:
  - 16,384 pages of 512 bytes, each followed by its 16 bytes of ECC (three bytes per 128-byte quarter, the Hamming
    code the card keeps);
  - two pages to a cluster; the superblock (`Sony PS2 Memory Card Format 1.2.0.0`) in cluster 0;
  - the indirect FAT in cluster 8, the FAT in 9-40, and 8,135 allocatable clusters from 41;
  - the root `.` and `..`, then each save's folder (mode 0x8427) with its files (0x8497), dated now on the PS2's
    clock (JST); a folder's `.` points at its parent's first cluster and its entry there;
  - the card's flags 0x2B, as mymcplus formats a card;
  - backup erase block 1022 erased (every byte 0xFF, the spare bytes too), block 1023 zero pages. The card's server
    takes a backup block that is not erased for an unfinished write and copies it back at mount: a card with zeros in
    1022 lost its superblock in PCSX2 (lane U's run).
- `tests/save_import.cpp` reads both back file for file, and checks the card's size, superblock, an empty page's
  ECC, the erased block, and that an existing file is refused (or, with force, replaced).

A save folder copied as it is into a PCSX2 folder memory card is not enough: GT4 found no data there (lane C, a copy
of a native save into a folder card). A folder card also holds `_pcsx2_superblock` at its root and per-folder
metadata, which PCSX2 writes when it formats one or saves into it. Which of them GT4 needs was not narrowed down.

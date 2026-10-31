# Texture packs

The host can draw a texture pack's high-resolution images in place of the game's own textures. Packs are
made for PCSX2's texture replacements: PNG files named after the texture they replace, for the US release
(SCUS-97328). The best known one is the GT4 Retexture Mod by Hattersby and team (GTPlanet thread
"Gran Turismo 4 Retexture Mod", version 3.0.x: tracks, cars, menus and HUD); the HD HUD & UI pack by
Silentwarior112 covers the interface. Neither is part of this repository.

## Installing a pack

Put the pack's folder (the one holding its PNG files, in any subfolders) in one of these places; the host
takes the first that has replacements:

1. `texture.pack_path` in the settings, when it is not `auto`;
2. `textures/SCUS-97328` next to `gt4_host`;
3. `textures/SCUS-97328` in the settings folder (`%APPDATA%\gt4-native\host` on Windows,
   `~/.local/share/gt4-native/host` on Linux);
4. `textures/SCUS-97328` in `%LOCALAPPDATA%\gt4-native` (Windows) or `~/.local/share/gt4-native` (Linux);
5. PCSX2's own `textures/SCUS-97328` (`Documents\PCSX2` on Windows, `~/.config/PCSX2` on Linux), so a
   pack installed for PCSX2 is found as it is.

A pack in versions (3.0, then 3.0.1 to 3.0.4) is installed in order, each one's files over the last.

The settings menu's "HD textures" row (GRAPHICS) switches the pack on and off while the game runs; it reads
"Not installed" when no pack was found at start-up. The setting is `texture.hd_pack` (on by default).

## How it works

- At start-up the pack's file names are indexed (`platform/gsnative/texture_pack.cpp`); nothing is loaded.
- When the game first draws a texture, the front end computes the texture's name the way PCSX2 does
  (`platform/gsnative/texture_name.cpp`: hashes of the texture's bytes in GS memory and of its palette, and
  the TEX0 / TEXA bits that change how they read). If the pack has that name, its image loads on a worker
  thread; the original is drawn until it is ready (usually the next few frames).
- A replacement is drawn in the original's place at its own resolution, with mipmaps made from it and the
  sampler's filtering (anisotropic with `texture.anisotropy`). CLAMP's region modes still wrap at the
  original texture's texels. A replacement named with a region stands for that rectangle of the texture
  (from the clamp's minimum, or the texture's corner), and is sampled over it (until 2026-10-31 it was spread
  over the whole texture: the arcade emblems were cut off).
- Decoded images stay in memory up to 1.5 GiB (`GT4_TEXTURE_PACK_MB` changes it), least recently used first
  out; the GPU keeps what is drawn.
- PNG alpha is the GS's own (0x80 = 1.0), unscaled, as PCSX2 dumps and loads it; an image without alpha
  is opaque (0x80).
- One replacement image is uploaded once and shared by every texture slot whose content it replaces (GT4
  streams its textures through a few slots in GS memory), until unused for two seconds. Every draw with it
  counts as a use (until 2026-10-31 only a texture's first draw did: on a screen that stayed still for two
  seconds, such as the arcade car selection, the replaced textures vanished).

## Names

`texture_name.cpp` follows PCSX2 (2026-10 master): XXH3-64, seed 0, over the texture's level 0 (and, for a
mipmapped draw, its further levels), either the 256-byte GS memory blocks it covers as stored (CT32, T8 and
T4 textures of at least one block, without a horizontal region) or its texels read out row by row (indices for
palette formats, RGBA with TEXA otherwise); the palette's hash is XXH3-64 of its 256 or 16 entries expanded
to 32 bits. A file name is `texhash-cluthash-bits.png` (or with a region, `-rWxH-` or the older `-r<hex>-`),
hex without leading zeros; `bits` holds PSM, TW, TH and, for 16 and 24-bit formats, TEXA (bit 14, once TCC,
is ignored). PCSX2 names a texture differently by how it is drawn (CLAMP's region, a region it guesses after a
partial upload, the mip chain); the front end tries the region sizes the pack has for the texture's bits,
with and without the chain.

On the race capture (`build/refcap/race.cap`) with the GT4 Retexture Mod 3.0.4, 25,591 of 29,919 texture
lookups find a replacement (2026-10-30).

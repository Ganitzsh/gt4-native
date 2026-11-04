# Changelog

Versions follow [semantic versioning](https://semver.org). Each release is tagged `vMAJOR.MINOR.PATCH`.

## 0.1.0 - 2026-11-04

First public release. The game runs natively from your own USA 1.01 disc image, on Windows and on Linux through
Proton (Steam Deck, ARM handhelds with FEX).

- The game's code from gt4-decomp, compiled for the PC, with a Vulkan renderer, native sound and native saves.
- Settings menu over the game (Select + Start, or Escape): presets from `original` to `ultra`, render scale,
  upscalers, anti-aliasing, frame rate limit, frame interpolation, aspect ratio.
- Saves as plain folders, with import from PCSX2 and PS2 memory card images (`--import-save`).
- Packaged folder that runs from anywhere, with `game.log` and `host.log` beside it.
- Fixed: crash or freeze when resuming a championship race.
- Fixed: the Polyphony Digital logo and the GT mode menus shook (interlaced fields are now blended).
- Fixed: opponents were missing from the rear-view mirror.
- Lower power use: with frame interpolation off, the host shows each game frame once instead of redrawing at the
  screen's refresh rate.

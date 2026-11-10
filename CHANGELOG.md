# Changelog

Versions follow [semantic versioning](https://semver.org). Each release is tagged `vMAJOR.MINOR.PATCH`.

## Unreleased

- Settings: Escape, Guide or Select + Start open the game's own Settings pages, over menus and, in a race, from the
  pause. Under `low` to `ultra`, GT4's Options items and Start on the GT mode map open them too, and the race pause
  is the new pause page. Existing settings files take this on the first start, and the first start logs it in
  `host.log`. The `original` preset keeps GT4's own screens and pause; Escape there now opens the pages too.
- Fixed: vertical bands across the road under headlights and street lamps at night (at every render scale except
  1:1; most visible on handhelds).
- Fixed: a stall of a few hundred milliseconds on the car screens (Arcade's car selection, wherever a car's
  colours are listed).
- Night lighting (the `medium`, `high` and `ultra` presets, and the default): the course's street lamps light the
  road, the walls and the cars near them, evenly under each lamp and the same from any lamp height; the cars'
  headlights light the road ahead, and their tail and brake lamps what is behind them. The game's own baked lamp
  light stays wherever no lamp replaces it, so no stretch of road is darker than before. `original` keeps the
  PS2's night, and `low` (handhelds) keeps it too for now. Settings files written under `medium`, `high` or
  `ultra` take this on the first start (logged in `host.log`); to go back, set `render.scene = original` and
  `lighting.night = original`, or pick `low`.

## 0.1.0 - 2026-11-04

First public release. The game runs natively from your own USA 1.01 disc image, on Windows and on Linux through
Proton (Steam Deck, ARM handhelds with FEX).

- The game's code from gt4-decomp, compiled for the PC, with a Vulkan renderer, native sound and native saves.
- Settings menu over the game (Select + Start, or Escape): presets from `original` to `ultra`, render scale,
  upscalers, anti-aliasing, frame rate limit, frame interpolation, aspect ratio.
- Saves as plain folders, with import from PCSX2 and PS2 memory card images (`--import-save`).
- Packaged folder that runs from anywhere, with `game.log` and `host.log` beside it.
- First start makes `game/CORE.GT4.elf` from your disc image and refuses other releases than USA 1.01.
- `--import-save` in a packaged folder writes to its `saves/`.
- Fixed: crash or freeze when resuming a championship race.
- Fixed: the Polyphony Digital logo and the GT mode menus shook (interlaced fields are now blended).
- Fixed: opponents were missing from the rear-view mirror.
- Lower power use: with frame interpolation off, the host shows each game frame once instead of redrawing at the
  screen's refresh rate.

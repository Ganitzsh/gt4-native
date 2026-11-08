# E4: the menu slide's field artefacts against PCSX2's software picture

The coordinator's question (2026-11-08): during a track-menu slide, are the doubled text line and the faint row at
the bottom also in PCSX2's software picture, or only in ours? That says whether `display.field_mode = clean` only
modernises the picture or also corrects it.

## Method

- **PCSX2** (lane C's portable copy, software renderer, native resolution, its default **Automatic**
  deinterlacing): Arcade > Time Trial > Original Circuits, one still at rest (El Capitan) and eight during the slide
  to High-Speed Ring, caught in PCSX2's slow motion (CAPTURES.md, "PCSX2 stills"). 640x480, `build/refs/`.
- **Ours:** `menus_boot.cap` replayed over VBlanks 13600-13640, which holds a slide, at `--preset original`
  (`field_mode = blend`) and `--preset low` (`clean`), 1280x720, interpolation and the HD pack off.
- **Per frame:** in the bottom band, the separate runs of bright text rows (two runs: a doubled copy), checked by eye
  on enlarged crops, and the mean luma of the last rows.

## Results

| | Doubled text line in the slide | Faint row at the bottom |
|---|---|---|
| PCSX2, software, Automatic deinterlacing (9 stills) | none: one clean line in every still | none: the last rows at 0.5-1.8 luma |
| Ours, original (`blend`), 41 frames | yes: 4 frames (13603-13606) show two overlapping copies | at times: 3 frames over 3 luma (up to 31) |
| Ours, low (`clean`), 41 frames | none: one line | **yes, worse:** from 13612 on, the last three rows hold the top of the next text line (up to 26 luma) |

(One PCSX2 still counted two text-row runs; enlarged, it is one line, split by a letter's descender.)

The eight slide stills cannot have missed a doubling as short as ours (4 frames, 13603-13606): at 10 % speed one
still each half second is about 3 game frames apart, the first about 3 frames after the press, so they cover the
slide's first frames.

## What it says

- The **doubled line** is not in PCSX2's picture. It comes from our field blend, which mixes the two fields of a line
  that moved between them. `clean` removes it and matches PCSX2 there: for the slide, `clean` corrects as well as
  modernises. PCSX2's Automatic deinterlacing is itself a filter: a TV shows the two fields in turn, so neither the
  blend nor PCSX2's still is the CRT's picture, but PCSX2 is the reference this project compares with.
- The **faint bottom row** is not in PCSX2's picture either. `blend` shows it at times. `clean` was meant to hide it
  (the lower field's extra line, which GT4 uses to park its next text), but at `low` it shows that parked line
  **more**: the motion test sees a line present in one field only as movement, and shows that field alone there, past
  the clamp of the lower field's last line. A defect in `clean`, **fixed**: in `clean` mode the displayed rectangle's
  last GS line is drawn black, as a TV's overscan hides it (at low, the hidden rows over 3 luma in 0 of 82 frames,
  against 73 with blend; original unchanged).
- The stutter (E4's other half) is not covered by these stills; it is a matter of frame pacing (`--profile-json`).

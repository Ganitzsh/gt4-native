# Widescreen and arbitrary aspect ratios: the plan for the game

The host already fits any content aspect ratio (`docs/RENDERING.md`, "Aspect ratio and the 2D layer"): 3D is
Hor+ (the vertical field of view is fixed), the 2D layer is laid out on a 640 x 480 virtual canvas and anchored,
centred or stretched, and other window shapes get black bars. This document is the plan for doing the same with
GT4's own rendering once the R1 renderer replaces the test scene: what the game does today, where in the code,
and what the port must hook.

Addresses are those of the NTSC-U executable (SCUS-97328) as named in gt4-decomp (`src/` paths relative to that
repository); "asm only" functions are not decompiled yet. Facts marked *guess* or *unconfirmed* still need a
runtime capture. No game data is reproduced here.

## What GT4 does: an anamorphic 16:9 mode that is already Hor+

GT4 has a widescreen option. It does not change the 640 x 448 framebuffer: the game renders a horizontally
squeezed picture that a 16:9 TV stretches back.

- **One flag.** Everything keys off the global `D_006184F0` (offset +0x20 of the display object `D_006184D0`). It
  is set by 0x004365B8 (applies the option block) to the result of **0x004390D0**: *wide = monitor width /
  height >= 1.5*.
- **The option** is stored in the option block at `*(D_00622F4C) + 0x38CB0` (scripts reach it through the MOption
  native object): +0x14 `wide_mode`, +0x18/+0x1C screen adjust, +0x24 `physical_monitor_size` {flags, scale,
  distance_mm, width_mm, height_mm, pixel_aspect, ...}, +0x16B0 race display mode (normal / 480p / 1080i).
  Readers and writers: `MOption__get_wide_mode` 0x00180108 (`src/gamedata/unit_0017FCB0/`), 0x00437110 ->
  0x004391C8 -> 0x00439108 (copy the matching monitor preset), 0x00189410 (physical monitor size),
  `MSystem::GetStaticWideMode` 0x001ACA90 (default from the console's screen setting, *guess*).
- **Monitor presets** (read-only data, copied by 0x00439B30): 4:3 = 275 x 205 mm at 238 mm, 16:9 = 425 x 240 mm
  at 276 mm. Both give a vertical half-angle of about 23.4 degrees, so **the 16:9 mode is Hor+ by design**.
- **3D projection** (race): `RaceBase::initMonitor` 0x00388A80 (asm only) builds a "monitor matrix" with
  `GranTurismo4::OptionMonitorMatrix` 0x00439288 -> `OptionMonitorMatrix_2` 0x004392E0 and a vertical FOV with
  `OptionMonitorFovy` 0x00439240 (`src/race/unit_004271E8/GranTurismo4__OptionMonitor*.cpp`). The matrix holds
  m0 = 2 * distance / width and m5 = 2 * distance / height: **the aspect ratio lives entirely in this matrix**.
  Each camera then multiplies its own perspective, the same scale on x and y from its vertical FOV (0x003FB4C0,
  `src/race/unit_003F7150/`; lens setters in `src/race/unit_00378778/`).
- **2D (race HUD)**: `RaceDisplay::initialize` 0x0039B3E8 sets the HUD orthographic projection through
  `RaceDisplay::setProjectionMatrix` 0x0039BB38: left 0, right 640 (4:3) or **left -106.67, right 746.67** (wide:
  853.33 units across, = 320 -/+ 240 x 16/9); bottom 480, top 0. Same constants in
  `RaceSplitDisplayBase::initialize` 0x003A29A0. `RaceDisplay::render` 0x0039F498 sets the font x scale to 0.75 in
  wide mode (0x0044E390 on the font manager `D_00623838`).
- **Anchoring**: `RaceDisplayUtil::wideOffset` 0x003AFDD8 (`src/race/unit_003AECB0/RaceDisplayUtil__wideOffset.cpp`)
  returns -96, +96 or 0 by the element's flags (bit 4 anchored, bit 8 right side); the x-alignment helpers
  0x003AFD18 (float, about 15 callers) and 0x003AFD78 (integer, about 22 callers) add it for the HUD panels in
  0x003A3000..0x003AE000. The minimap uses -107 / 854 (0x003A1970 `RaceDisplay::init_panel`, 0x003D8518), the race
  monitor 853.33 and a 0.75 font scale (0x003DA150).
- **Other hard-coded 16:9 factors**: 0x00393790 (a textured quad, x0.75; the mirror or an inset, *guess*),
  0x003B80E8 (aspect 1.214 -> 0.911), 0x00330AE8 (samples a centre region +-0.75 for a downsample effect),
  0x00102B30 / 0x00103018 (startup: ortho -320..320 x -224..224 and scale 0.75), and **0x00107328** (menu 3D
  models: scale x by 0.75 after the frustum; called by `mRenderContextPS2` virtuals 64/65 at 0x00217058 /
  0x002177B8 and by 0x001D5C00).
- **Menus**: `mUpdateContextPS2` virtual 64 (0x00214FB0) sets an orthographic 0..640 x 0..480 projection with no
  wide handling; 0x00215FE0 scales rectangles from a 640 x 480 virtual menu space to the buffer. Menu 2D is
  therefore stretched on a 16:9 TV, while menu 3D models get the 0.75 squeeze (*unconfirmed at run time*).

## The graphics library and the display path

The render back end ("pgl", Polyphony's GL-like library) lives in gt4-decomp `src/network/unit_00494578/` (the
unit is filed under network): MatrixMode 0x004A5348 (named `RaceEntryCar__update` in the repository, wrongly),
LoadIdentity 0x004A7454, LoadMatrix 0x004A74B4, MultMatrix 0x004A7698, Scale 0x004A7890, Frustum 0x004A59B8,
Ortho 0x004A5A90, gluPerspective 0x0049AC00 (`func_0049AC00.c`). The current matrix is kept in VU0 (vf28..31),
saved matrices in scratchpad (SPR 0x70002ED0 modelview, 0x70002F10 projection).

- **Viewport**: pglViewport 0x004A2D20 -> 0x004A2D60 computes the screen scale (SPR+0xFA0 = w/2, SPR+0xFA4 = -h/2)
  and centre (SPR+0xFB0/FB4: 2048 for a 640-wide buffer).
- **Draw buffer**: 0x004A2478 sets XYOFFSET (OFX = (4096 - w) * 8 = 0x6C00 for w = 640, plus a sub-pixel jitter
  from SPR+0x2A8..), FRAME, then the viewport; called through 0x004971A0 and the GSBuffer bind 0x001056A0.
- **VU1 constants**: **0x0049E548** (asm only) uploads the combined matrix and the viewport scale, centre and clamp
  quadwords to VU1 data memory with one VIF UNPACK V4-32 of 8 quadwords. Callers 0x00497838, 0x0049BBF0,
  0x0049C2A8, 0x0049C788, 0x0049D328, 0x0049FAC8.
- **Display**: **0x004A0C10** writes PMODE, SMODE2, DISPFB2 (and DISPFB1 for a two-circuit blend, *guess*: a
  flicker filter), DISPLAY2 and BGCOLOR; DISPLAY2 comes from 0x004A0970 (640 x 448 NTSC, 512-line PAL, 480p and
  1080i variants). Front buffers are flipped by 0x00107878 -> 0x00105608 every frame. The race display mode is
  applied by 0x00436640 (normal 640 x 448; 480p; 1080i at 576 x 960), menus by 0x00436740 (640 x 448).
- Correction for `docs/HW_ACCESS.md`: 0x004AB548 reads VIF1_STAT / VIF1_CODE, resets VIF1 and restarts GIF DMA;
  it looks like the VIF1 interrupt handler rather than a plain DMA kick.

## What the port does

### Presentation

R1 rasterises the GS primitives itself, at any resolution: GS window coordinates (12.4 fixed point around
XYOFFSET) are mapped to the host's render target, so the 640 x 448 "canvas" is only a coordinate system, not a
resolution limit. The source rectangle comes from DISPFB2 / DISPLAY2 as written by 0x004A0C10; screen adjust
(DX/DY) and the two-circuit blend are ignored. The canvas is presented into the content rectangle of the host's
aspect ratio, which is how the pipeline already treats the test scene.

### 3D: Hor+ for any aspect ratio

1. Force the wide flag for any host aspect >= 1.5 (`D_006184F0` = 1, or the result of 0x004390D0), and give the
   game a monitor whose shape is the host's: keep the preset's height and distance and set
   `width_mm = height_mm x aspect` in `physical_monitor_size` (option block +0x24) before 0x004365B8 / 0x00388A80
   run. `OptionMonitorMatrix_2` then yields m0 = 2 d / (h x aspect): the vertical FOV stays, the horizontal one
   widens. Equivalent hook: scale the x row of the matrix returned by 0x00439288 by (4/3) / aspect.
2. The game keeps rendering into its 640-unit-wide canvas; NDC x still spans the full buffer, so with the canvas
   presented at the host aspect nothing is lost and the image is not squeezed. At render resolution the
   squeeze costs no detail because R1 rasterises at the host size.
3. Patch or generalise the other fixed factors: the 0x00107328 menu-model squeeze, 0x003B80E8, 0x00393790,
   0x00330AE8, the startup ortho at 0x00102B30 / 0x00103018, and the gluPerspective users (0x0033B2A8 and others
   listed above). Each 0.75 becomes (4/3) / aspect.
4. View-frustum culling follows from the matrices; it needs checking at run time that objects at the new edges are
   not culled by a separate, fixed test (*unconfirmed*).
5. The camera-manager FOV at +0xA00 and how each camera's cot(fov/2) combines with the monitor matrix
   (0x003FB4C0) need a capture to confirm; replays and photo mode use other cameras (0x003C8BF8 applies
   Scale(m5/m0, m0/m5, 1) for the develop camera).

### 2D: anchored HUD, centred or stretched menus

- **Race HUD**: generalise the hard-coded 16:9 constants to the host aspect A: ortho left/right = 320 -/+ 240 A
  (0x0039B3E8, 0x003A29A0); the anchoring offset +-96 -> +-(240 A - 320) x 0.9 (0x003AFDD8, 0x003AFD18,
  0x003AFD78; 0.9 keeps the PS2's own inset ratio, 96 / 106.67); the minimap -107 / 854 -> 320 -/+ 240 A
  (0x003A1970, 0x003D8518); 853.33 -> 480 A (0x003DA150); the font scale 0.75 -> (4/3) / A (0x0039F498). The
  `aspect.safe_area` inset applies on top.
- **Menus** (0..640 orthographic): done the centred way. `game_src/func_00214FB0.cpp` gives the menus'
  orthographic projection 320 -/+ 240 x aspect in the wide mode, so their 640 x 480 layout keeps its shape in a
  centred 4:3 area (`--menus centered`, the game's default; gt4_host passes it only for `aspect.hud =
  centered` and `--menus stretched` otherwise: GT4's own 16:9 layout, its font squeezed to the frame, lines up
  every page, while centring misplaces pages drawn as one full-screen image under their text, such as the
  status page and Home, 2026-10-29). Full-screen backgrounds drawn another way still fill the width; the menu models
  keep the game's own 0.75 squeeze, which is right for that frame. 0x00215FE0's rectangle mapping is untouched
  and nothing looked clipped in the title, mode, course and car menus (2026-10-27). A primitive the native 2D
  emitters (0x0049A200, 0x0049A360, 0x0049A4F8, 0x0049A6A0) place exactly over the centred area is a
  background or a fade: `gt4_menu_band_widen` (runtime/sdk.cpp) widens it to the frame's edges, as the PS2's
  16:9 mode stretches it (the dealers' backdrop left stale sides that its frame fed back into itself).
- **Direct GS sprites** (0x004A6668 / 0x004A6498 -> 0x004A6538: copies and post effects in framebuffer pixels) are
  full-screen passes; they scale with the canvas.
- **Fallback for anything not patched**: at the packet level R1 can tell 2D from 3D by the matrix in the
  0x0049E548 upload (an orthographic matrix has a w row of 0, 0, 0, 1), and lay those draws out with the host's
  `Canvas2D` rules (`platform/render/overlay.h`): centred by default, stretched as an option (*heuristic, not
  tested*).

### Order of work for R1

1. Present the game's canvas at the host aspect (stretch the anamorphic 16:9 output, pillarbox 4:3).
2. Hor+ through the monitor geometry, then the remaining 0.75 factors.
3. HUD constants as functions of the aspect; menus centred.
4. Captures at 4:3, 16:9, 16:10 and 21:9 of a race start, the HUD and a menu, compared with the PS2 at 4:3 and 16:9.

Still unknown and to capture at run time: the camera-manager FOV path, the vtable slot mapping of the camera
manager, what 0x00393D20 and 0x00330AE8 draw, whether Adhoc scripts (in the VOL archive) lay out their own UI
by `wide_mode`, and the 1080i factors (1.8 / 1.35) with the 576 x 960 buffer.

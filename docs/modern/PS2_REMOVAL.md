# PS2 in the pipeline: the audit of 2026-11-17, and what removes it

The owner's direction (2026-11-17): everything PS2 is gone; the whole pipeline uses proper shaders and Vulkan and
does nothing PS2-related. This page is the audit against that, at `main` 1e7be22, and the order of work that gets
there. It replaces the note of 2026-11-08, whose goal was narrower (no PS2 concept beyond what the game logic needs,
with the PS2 renderer kept as `original`). Tickets are in [PLAN.md](PLAN.md) (P1-P20, R19, and the lanes' tickets
named here). The PS2's renderer goes; its picture does not: the owner, the same day, asked that the native renderer
reproduce the original (section 4, "The original look").

How it was audited: three independent reads of the code (what the game process sends; the host's stream and front
end; the Vulkan back end and every shader), each with `path:line` evidence, and replays of five scenes with the
release host of 1e7be22 at the `high` preset (`GT4_GSN=stats GT4_GSN_DRAWS=all`, 1280 x 720, the desktop).

## 1. The answer

**No. The pipeline is Vulkan and shaders from end to end, but what those shaders compute is the PS2.** Every pixel
of every game frame, in every preset, is drawn by one path: the game sends PS2 graphics state, the host's front end
runs a model of the PS2's GS on it, and `gsn.vert` + `gsn.frag` rasterise the result with the GS's own rules.

- `render.scene = hybrid` (medium, high, ultra) draws most 3D items from the mesh cache's copy of their vertices,
  and that is all it changes in the picture: the vertex shader is still GT4's VU1 program (`modern_parity.vert`),
  the fragment shader is still the GS's (`gsn.frag`'s parity variant), into the same GS target with the same GS
  depth, blend and tests. `gsn_models.comp` still computes every batch first, the substituted ones too
  (`gsn_scene.cpp:1889-1917`). When the night lighting reads them, hybrid also redraws the items into four side
  channels (normal, material, depth, lit colour), against the GS's depth. By day its picture is the PS2 path's by
  design.
- What is modern on game frames is screen-space work on that picture: the night relight, the ambient occlusion, the
  temporal resolve, the height fog, then exposure, sharpening and the output stage.
- `render.scene = modern` does not exist: the setting's value is named and refused
  (`platform/sdl/settings_scene.cpp:22`), and there is no modern scene target, material or post code behind it.
- The display pipeline's bloom, tone mappers, auto exposure, FSR 2 and FSR 3.1 never run on game frames
  (`display_feature.cpp:565`, `811`; `pipeline.cpp:140-145`): they run on the built-in test scene only.
- `original` and `low` (the handheld's preset) are the PS2 path with no substitution at all.

## 2. Measured

Per frame, over a window of 20 to 40 frames of each scene (`race_pe2_prog`, `lap_ssr5r_prog`, `garage_r6`), at
`high`. A batch here is a draw of the front end's list (equal-state batches merged); vertices are triangle corners;
the targets are the distinct ones drawn into over the window. Four of the five windows have the same counts in every
frame; the night race's vary, its figures are means. The night capture predates the lamp flag, so
its replay drew no side channels: the night figures are the GS path's and the substitution's only.

| Scene | Draw batches | 3D batches (vertices) | 2D batches (vertices) | Textured batches with a 4- or 8-bit palette texture | GS targets drawn into | Alpha test | GS fog | Destination alpha test |
|---|---|---|---|---|---|---|---|---|
| Day race (Pan Euro) | 738 | 706 (141,519) | 32 (1,274) | 659 of 685 | 9 | 559 | 677 | 13 |
| Night race (S.S. Route 5) | 641 | 573 (111,460) | 67 (1,846) | 549 of 594 | 16 | 449 | 543 | 11 |
| Event screen (3D behind a menu) | 489 | 420 (124,371) | 69 (2,586) | 444 of 450 | 7 | 337 | 415 | 0 |
| Garage (the car on the turntable) | 528 | 472 (157,200) | 56 (3,984) | 492 of 510 | 4 | 300 | 472 | 36 |
| GT mode Home (a 2D menu) | 80 | 0 | 80 (1,578) | 60 of 60 | 2 | 0 | 0 | 0 |

- **Share of the game's 3D batches drawn from the mesh cache** (VU1 runs, about 1,540 a frame in the day race and
  1,050 at night): about 92 % in the day race (2,246,376 of 2,439,913 over its race part) and 88 % at night
  (556,422 of 629,527); garage and Home 0 (menu views are never substituted). Of the items in the views the
  substitution takes it is 97.5 % and 98.0 %, the rest being items with no cached mesh (geometry built each frame,
  meshes still decoding). Outside those views: the views of kept roles, the cars' light maps, the road reflection
  and untagged batches.
- **Share drawn with PS2 semantics: 100 %**, in all five. The 3D batches' vertex work runs in `gsn_models.comp` (all
  3,260,921 batches of the day replay on the GPU; the CPU renderers produced 0 primitive runs) and, for the
  substituted items, again in `modern_parity.vert`: both are GT4's VU1 programs.
- **GS state still interpreted a frame:** 70 (Home) to 696 (the day race) CLUT loads; every target found by its
  address in GS memory; eight distinct GS blend equations over the five windows; 550 to 680 GIF packets a frame in
  a race, the 3D batches' state headers, still parsed by the front end (1,431,273 over the day replay).
- **Not used at all in these scenes:** reinterpretations of a target in another format (0), a texture decoded from
  memory where a target had been drawn (0), the CPU renderers (0 runs). The game's own reads of drawn pixels (38,
  372 and 25 over the day, night and event replays) were answered from GS memory: a replay has no read service, so
  they say nothing about a live game.

The counter that tracks this from now on is P7's (PLAN): the same numbers printed by every run, with 0 as the goal.

## 3. What is PS2-shaped today

### 3.1 What the game process sends

Game logic and the disc's data decide the values; the port's runtime decides the record's form. "Game" below means
matched or decompiled game code and its data, "port" the native runtime (`runtime/`, `game_src` call rewrites).

| Mechanism | Where | Who fixes the form | Removed by |
|---|---|---|---|
| Records framed as a DMA source chain: each behind a DMA tag with a VIF code, walked as cnt / next / ref / call / ret | `pgl.cpp:61-79`, `frame.cpp:369-433` | chain links: game (pgl's 004A1BB0); the framing: port | P3a |
| Draw state as GS register fields: FRAME, ZBUF, SCISSOR, XYOFFSET, ALPHA, TEST, TEX0/1/2, CLAMP, TEXA, FOGCOL, PABE, FBA, DIMX, two contexts (`StateRecord`) | `gfx.h:44-259`, `pgl.cpp:149-178`, `1520-1558` | values: game (pgl's setters write GS bit fields; texture sets on the disc hold TEX0, TEX1, CLAMP); the record: port | P14 |
| Every surface named by its address in the GS's 4 MB memory and a GS pixel format | `gfx.h:8-33`, every state, upload, read and display record | game (pgl's slot cache, GSBuffer objects) | P12, P13 |
| Textures and palettes as host-to-GS transfers: packed GS texels, 16 x 16 palettes in CSM1 order, loaded by CLD and CSA | `UploadRecord`, `TexSetRecord`, `pgl.cpp:1617-1706` | layout: the disc's texture sets; the record: port | P12 |
| 2D and immediate draws as GS vertices: PRIM bits, 12.4 window coordinates with the XYOFFSET origin, 24- or 32-bit integer Z, fog byte, STQ, the ADC bit; full-screen draws cut into 32- or 64-pixel strips for the GS's texture cache | `DrawRecord`, `gfx.h:693-719`; 20 `game_src` files draw this way (46 call the `gt4_gfx_*` API) | values: game; the record: port | P11 |
| 3D as writes into a 256-qword VU1 batch memory, with VIF UNPACK formats, an output GIF tag, a VU1 program entry and ITOP (`kBatchFrame`, `Write`, `Model`, `Run`); model files sent as their own VIF packets | `gfx.h:484-560`, `pgl.cpp:1319-1374`, `game_src/func_0049D328.cpp` | model packets: the disc; indices, formats, entry: game; the records: port | P15 |
| The screen-grid effects still build a whole DMA / VIF / GIF packet, which the runtime then interprets (a port of VU1 programs 0x3D50 and 0x3EA0, a GIF tag parser) | `game_src/func_004A81A8.cpp`, `func_004A9470.cpp`, `pgl.cpp:192-309`, `1376-1453` | game builds it; port interprets it | P8 (the packet built and thrown away), P16 |
| The colour grade's record names GS pages, a CLUT address and a raw SCISSOR value; its curve is still uploaded as a CSM1 palette | `EffectRecord`, `pgl.cpp:1455-1480` | arguments: game; the record: port | P16 (a pass of the original look; not run in the modern look) |
| Reads of drawn pixels as GS-to-EE transfers in qwords of GS packing (`kRead`, `GT4_REC_GS_READ`, `GS_IMAGE`) | `pgl.cpp:1733-1801`, `link.cpp:502-515` | callers: game (the flare samples every frame with lights in view, the luminance probe, photo tiles, depth samples) | P18 |
| The display as CRTC registers (PMODE, SMODE2, DISPFB, DISPLAY, two read circuits) and the field in the frame's end record | `gs.cpp:37-116`, `DisplayRecord` | values: game; decode: port | P19 for native views; P20 with the GS path |
| Movie and still pictures written in the IPU's output layout (16 x 16 macroblocks), then uploaded and drawn by game code | `movie.cpp:374-403`, `ipu_image.cpp` | the layout: game code that consumes it | P11 (movies as one texture) |
| Raw GIF packets still written by two particle functions and left out by the walker (logged once) | `game_src/func_004961B8.cpp`, `func_00496638.cpp` | game | P10 |

### 3.2 The host's stream and front end

| Mechanism | Where | Live at the defaults? | Removed by |
|---|---|---|---|
| VIF1 model: two 256-qword batch buffers (BASE / OFFSET double buffering), CL / WL, MODE, MASK, ROW, COL, UNPACK decode of model packets | `platform/gs/vif1.*`, `include/gt4/unpack.h` | yes, every 3D batch | P15 (the decode moves into the importer) |
| VU1 renderers: the entry table, 15 lighting bodies, 5 decompression routines, VU float rules; fills a `ModelJob` per batch | `platform/gs/renderers3d.*` | the job fill: yes; its CPU per-vertex path, clipper and guard band: never run (0 primitive runs) | P8 (the CPU path), P15, P20 |
| The GS state machine: both contexts, the vertex queue and kick rules, sprites as two triangles, flat shading, the GS's coverage rule, GT4's Z24 inversion pass | `platform/gsnative/gs_frontend.cpp:188-936` | yes | P11, P14, P20 |
| A GIF packet parser (tags, PACKED, REGLIST, IMAGE), transfers by BITBLTBUF / TRXDIR, HWREG, SIGNAL / FINISH / LABEL events | `gs_frontend.cpp:415-454`, `1964-1994`, `2366-2414` (the register writes it ends in, `write()` at `286-370`, also serve the draw records) | fed only by the batches' state headers (550 to 680 a frame in a race, empty by the code's reading, not counted yet) and a mid-run capture's restore | P8 |
| Targets keyed by GS address, width and format class; ownership of 512 8 KiB pages; at most 96 targets | `gs_frontend.cpp:998-1072` | yes | P13 |
| The GS's 4 MB memory: swizzled block and column layouts, every GS pixel format, the CLUT buffer with CSM1 order and CSA; a second private copy to decode texture sets | `platform/gsnative/gs_memory.*` | yes: every game texture is decoded through it, native texture sets included | P12, P13 |
| Reinterpretation of a target as a texture of another format, with a palette | `gs_frontend.cpp:1580-1704` | code live, 0 uses in the five scenes | P13 |
| Texture pack names computed as PCSX2 does, from GS blocks and the CLUT buffer | `platform/gsnative/texture_name.cpp` | with an HD pack installed | P12 (names from a table made at import) |
| Scene records only annotate: they tag jobs with a view and an instance; every draw stays a GS-state draw. The mesh cache decodes the VU1 batch layout and keeps the raw words for the shader | `platform/scene/*`, `gs_frontend.cpp:2029-2188` | yes | P15, P16 |
| The scene stage runs only when a target at least 512 x 384 with 64 depth-tested draws is read as a texture: field-mode races and split screen (224 lines) get no lighting, occlusion or temporal resolve | `gs_frontend.cpp:970-977` | yes | P9 (a view is lit because it is a view) |
| The PS2 pad's 18-byte libpad2 frame as the input record | `platform/gs/ps2_pad.h` | yes | kept: the game's input API (section 6) |
| PCSX2 GS dump encoder and writer, GS privileged registers | `platform/gs/gsdump_*`, `gs_privileged.h` | only with `GT4_GSDUMP` | P20 |

### 3.3 The Vulkan back end and its shaders

| Shader or pass | What it reproduces | Live at the defaults? | Removed by |
|---|---|---|---|
| `gsn.vert` | GS window coordinates, integer Z as `z / 2^32`, the GS's linear interpolation, its top-left sample rule | every draw | P16, P20 |
| `gsn.frag` (and its export, exact and parity variants) | the GS texture functions (TFX, TCC), GS fog, all alpha test functions and fail modes, FBA, PABE, the blend equation `(A - B) * C + D` by dual-source output, the destination alpha test, CLAMP's region modes, the GS's bilinear weights, TEXA | every draw | P16, P20 |
| `gsn_models.comp`, `gt4_lighting.glsl`, `gt4_transform.glsl` | GT4's VU1 programs: vertex decompression, the lighting bodies, the transform with VU float rules, culling as the ADC bit, the fog word | every 3D batch | P15, P16 |
| `modern_parity.vert` (and its side variant) | the same VU1 program per cached vertex, bit-exact with the above | hybrid | P16 |
| `gsn_stencil.frag`, the stencil export | the destination alpha test (alpha bit 7 mirrored in a stencil bit) | when a draw tests it | P14, P20 |
| `gsn_reinterpret.comp`, `gsn_swizzle.glsl` | GS memory addressing, the swizzle, CLUT lookup on the GPU | when a draw needs it (0 in the five scenes) | P13 |
| `gsn_grade.comp` | GT4's colour grade with the GS's integer steps | races | P16 |
| `gsn_display.frag` | the display circuit's rectangle (DISPFB, DISPLAY), the blend of two interlaced fields | every frame | P19 for native views (the field blend as a pass of the original look); P20 with the GS path |
| Targets | one image per GS frame buffer and per GS Z buffer; colour stored as value / 255 and alpha / 128 (the GS's 0x80 = 1); depth is GS Z; 640 x 448 and 64-pixel-page constants throughout | every frame | P13, P16 |
| `modern_side.frag`, `gsn_side_*`, `gsn_velocity.*`, `gsn_mask.frag`, `gsn_seam*.comp` | nothing of the PS2 in what they write (side channels, motion vectors, masks), but they redraw the items with GT4's vertex program and the depth test EQUAL against GS Z, once per GT4 Z partition | hybrid at night; motion vectors with TAA; the seam by a switch only | P16 (the native passes write these themselves), P20 |
| The scene stage (occlusion, night relight, fog, temporal resolve, reflections, blur, focus) | modern, but its inputs are the GS target and GS Z, with GT4 constants (`Z = 1.6e6 / distance`, the half field of view's tangent 0.433, FOGCOL) | medium to ultra | P16 (same passes on native depth and colour) |
| The display pipeline (exposure, tone map, FXAA / SMAA, FSR 1, RCAS, output), the host UI overlay | nothing of the PS2 | yes | kept |

### 3.4 Already free of the PS2

Scene records (matrices, views, instances, lights, markers, vehicle state); content identity and dedup of textures
and model packets; the frame clock, callbacks at safe points and back-pressure; presentation at any refresh rate
and aspect; the settings pages' draw list (`GT4_REC_UI`); the light list and clusters; the display pipeline; movie
decoding by FFmpeg; the host mixer's output stages.

### 3.5 Dead or dormant code the audit found (P8)

- Never used by the shipped host: `platform/gs/gs_backend.*` (the paraLLEl-GS reference, off by default, two tests),
  `include/gt4/gs/gif.hpp` (one test), `gs_frame.frag` (compiled, no user), `GsnScene::setDisplaySeam` (no caller).
- Sent and dropped: `kOriginParts` (244,340 in the day replay; the host's target has no handler yet: B9's showroom
  passes are its reader, so it is kept for P9, not deleted); the host's `GT4_REC_GS_EVENT`, which the game's link
  reader never handles.
- Built and thrown away: the screen-grid packet of 004A81A8 when the grade goes as one record (the default).
- Never run: the CPU renderers, the VU clipper group and `GsFrontend::vertices` (`GT4_GPU_MODELS=0`,
  `GT4_VU_CLIP=1` only); `link_remember` (no caller); `Command::Kind::Clear` (never produced).
- Called and empty: the VU microprogram upload stub (004A12B8, twice from 004A03C0).
- Wrong today, found on the way: with `effects.dof` or `effects.motion_blur` on, the display side's passes run on a
  game frame as well as the scene stage's own (the display side has no depth or motion there); the test scene's
  meshes, cube maps and shadow map are built in game mode too.
- Stale comments and docs: `protocol.h:9-13`, `gs_host.h:2-4`, `gs_frontend.h:2-3`, `stream.h:25`,
  `platform/render/scene.h:4-7`; ARCHITECTURE.md section 4.1 and 7 (`low` is `original`, not `hybrid`; five settings
  listed there don't exist).

## 4. The end state

"Gone" means the host knows no PS2 concept, and the game process converts at one boundary:

- **Meshes.** Vertex and index buffers with float positions, normals, texture coordinates and colours, by content
  hash. Decoded once, at import, from the disc's model data.
- **Textures.** Images (RGBA8 or block-compressed) with mips, by content hash, their palettes applied at import.
  An animated palette is a new image or a palette parameter of the material, not a CLUT load.
- **Materials.** A blend mode, an alpha cutoff, a depth mode, a cull mode, a shading class (R8's classes: prelit,
  diffuse, car paint, glossy, projected) and its textures.
- **Draws.** A mesh, a material and a transform from the scene records, in a view; 2D as quads in canvas units.
- **Targets.** Named images the renderer owns (the scene with a float reversed depth, in HDR for the modern look;
  the mirror, the environment faces); none found by an address.
- **Shaders.** The renderer's own vertex stage (matrices) and material shaders, in two looks (below): GT4's own
  lighting and effects, or lit per pixel with its own sky, particles, reflections and post chain (the display
  pipeline that exists, at last on game frames); a 2D layer, at the output's resolution in the modern look.
- **Display.** The renderer presents its own image. No read circuits; fields only as the original look's blend.
- **Reads.** Each of the game's reads of its picture answered from the renderer's images in the units the game
  expects.

What cannot go, and is not hidden: the disc is a PS2 disc, so **one importer** reads its formats (model files as
VIF-packed vertex data, texture sets as GS texels with TEX0 records, the movies' PSS). That importer is the only
place left that knows them. And the game's own code keeps calling its graphics library with GS-flavoured arguments:
the runtime translates them into materials at that call, and nothing behind it sees them.

### The original look

The owner, 2026-11-17, on whether `original` is deleted with the GS path: "original goes but not really: we must
manage to reproduce it with the modern rendering". So the PS2 renderer goes and its picture stays. The native
renderer has two looks, and the `original` preset selects the first:

- **Original.** GT4's picture as the game made it, drawn from the same meshes, images and materials by the
  renderer's own shaders: GT4's lighting (the colours baked in its vertices and its per-object lights, by GT4's own
  formulas, in floats), its fog, its blend modes in GT4's meaning, its order of layers, its sky and particles, and
  its post effects (the glare, the flares, the grade and the depth of field among them; P10 lists them all), each
  a named pass with GT4's parameters. No light, shadow, occlusion or tone map GT4 didn't have. The PS2's
  resolution (640 x 448, today's `render.gs = original`) and its fields (a race's 640 x 224 fields and the
  interlaced screens, blended for display) are presentation options of this look: targets of those sizes and a
  blend of two frames, with no GS and no CRTC behind them.
- **Modern.** The same scene lit per pixel, with the renderer's own lights, shadows, sky, reflections and post
  chain.

What separates the original look from the PS2 path: GT4's look is the game's (its lighting model, its colours, its
order, its effects) and is kept; the arithmetic is the console's (12.4 coordinates, integer Z, the 0x80 alpha
scale, palettes, swizzled memory, VU1 programs, the CRTC) and goes. So the original look is not bit-identical to
the GS path's picture. It is held to it by a measured tolerance: on the REF captures, at the GS path's own target
size and before display (640 x 448 for a whole frame, 640 x 224 for a field), the share of pixels within a number
of levels, and the rest listed by class (rounding, edge coverage, texture filtering, dither where GT4 sets it).
R19 measures what a native path reaches and proposes the number; P16 fixes it before its first view role, with the
owner's agreement, since that number is what "reproduce" means.

The original look comes first for each view role, because it is the look with a reference to be measured against;
the modern look is built on a role that has passed it. That replaces a loose gate (stills reviewed) by a measured
one, and it adds work to P16: GT4's lighting as a shading model of the renderer, and its post effects as passes.

### Three decisions, put to the owner on 2026-11-17

1. **The GS path goes at the end (P20); the `original` preset stays, as the native renderer's original look.**
   Answered by the owner (above; the first draft of this page deleted the preset too). Until P20 the GS path is
   the reference the original look is measured against, and the fallback; keeping it for good would mean keeping
   everything in 3.2 and 3.3. After P20 the reference is the GS path's last frames of the REF captures, rendered
   before the deletion and kept local per device class, with PCSX2's stills beside them (P20).
2. **Release 0.1.1 waits for this**, as it waited for the modernisation. The alternative is a release from today's
   main with the PS2 path inside and this work after it. Not addressed in the owner's answer: still assumed, the
   owner's to overrule.
3. **The scope is the picture path.** Sound, input, saves, the kernel and the game's memory are section 6: kept at
   the game's API by the earlier spikes, not re-audited here. Not addressed in the owner's answer: still assumed,
   the owner's to overrule.

## 5. The order

Each step lands on main behind `render.scene` and its own switch (rule 10), and is judged against the GS path's
picture on the REF captures until P20. P7's counters say how much is left. Phases 0 to 2 are meant to leave the
picture as it is; the few places where a ticket changes one are named in the ticket (PLAN).

| Phase | Tickets | What it reaches |
|---|---|---|
| 0. Measure and clear | **P7** (the PS2-path counters), **P8** (dead code out), **R19** (spike: one race view drawn natively end to end) | a number to drive to 0; the unknowns of phase 2 and 3 named before their records are designed |
| 1. Every draw described | **P9** (every view role substituted: B9 widened), **P10** (untagged draws classified: sky, particles, clears, glows; A8), **P11** (2D as 2D records; menus progressive) | no draw the host can't name; still the PS2's picture |
| 2. Native resources and state | **P12** (textures as images; P2b inside), **P13** (targets as named images; P2c inside), **P14** (materials, not GS state; P3b), **P15** (meshes, not VU1 batches), P3a | the wire carries no GS or VU term |
| 3. The native renderer | **P16** (`render.scene = modern`, per view role: the original look first, measured against the GS path, then the modern look: B15 with B10's lighting, B14 sky, B18 and B22 particles and ordered layers, B13 reflections, F3 and F4), **P17** (the 2D layer: E6, E7, E9), **P18** (reads answered natively: R9), **P19** (display without a CRTC) | every pixel by the renderer's own shaders; the presets move to `modern` role by role, `original` as the original look |
| 4. Delete | **P20** (the GS path, `render.scene`, `render.gs`, the PS2-term records, old captures, the GS dump tools; the `original` preset stays, on the native renderer) | section 3 is empty |

By PLAN's own sizes this is the largest block in the plan: fifteen tickets, three of them L, three M-L and six M
by their headings, and P16 and P17 each hold several of PLAN's older tickets (B10's step 2, itself L; B13, B14,
B15, B18, B22; E6, E7, E9). Most of it is serial on `gs_frontend`, `gsn_scene` and `pgl`. R19 is what turns that
into an estimate worth trusting.

## 6. Outside the picture path

Carried from the note of 2026-11-08 with each ticket's result since; the code behind these rows was not re-read on
2026-11-17. They are the game logic's own interfaces: removing one means rewriting game code, not the port.

| Mechanism | Where | Verdict | Since 2026-11-08 |
|---|---|---|---|
| Guest RAM at PS2 addresses; PS2 pointers as host pointers; the 32-bit game process; gcc 2.9's ABI | `runtime/guest.*`, the game build | kept | The game's code and data hold 32-bit absolute addresses. |
| RAM size 32 MB, heap end 0x01E00000; the uncached mirrors | `kernel.cpp`, `guest.cpp`, `frame.cpp` | kept | P5 (R17) answered: the race fills the heap by design, nothing would use more. The mirrors' audit was not run. |
| Scratchpad at 0x70000000; hardware register blocks reserved without memory | `guest.h`, `hw_audit.cpp` | kept; lifted already | - |
| The original ELF in guest RAM; trampolines at original function addresses | `image.cpp`, `missing.cpp` | kept | The "missing" stops end with the drafts. |
| The 60 Hz game frame | `kernel.cpp`, `runtime/gs.cpp` | kept as the game's time base | - |
| Preemptive delivery fallback (a thread frozen after 50 ms without a safe point) | `kernel.cpp` | replaced | P4: the diagnostic is in; 0 late deliveries on the desktop over every route run. The freeze goes when an Odin play log has none. |
| The EE kernel API (threads, semaphores, alarms, syscalls); COP0 reads | `kernel.cpp`, `sdk.cpp`, `eeops.cpp` | kept at its API | - |
| PS2 float semantics | `runtime/main.cpp`, `runtime/ps2float/` | kept | Replays, ghosts and saves depend on it. |
| SIF RPC and the IOP services | `runtime/iop.cpp` | kept at the RPC API | - |
| SPU2 voice model (ADPCM, ADSR, register image), on the host since D5 | `runtime/audio.cpp`, `platform/audio/spu/` | kept | P6 (R18) answered: no, the sound driver needs the register image. |
| CD/DVD, memory card files, the pad's libpad2 frames | `iop.cpp`, `memcard.cpp`, `save_folder.cpp`, `ps2_pad.h` | kept at their APIs | - |
| Legacy GIF / VIF records on the link | `protocol.h` | gone | P1 done (53b7455). |

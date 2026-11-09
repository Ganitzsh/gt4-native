# Open questions and spikes

Status: design for review. Each spike has a question, a method and a time box. Spike code is local
instrumentation in the spiker's own clone, never committed; its answer goes into
`docs/modern/spikes/R<n>.md` (committed after review) with the numbers it found. A spike that runs out of time
writes down what it knows and which ticket waits.

References at `main` a80cfca.

---

## R1: The matrix semantics at 0049E480 (ticket A0; blocks A2, A3, B7)

**Status: answered** (lane A, [spikes/R1.md](spikes/R1.md), 2026-11-07): set 0 is object-to-view; set 1 is
view-to-clip with the viewport separate and changes inside a view; the camera is set 0 at the view's starting stack
depth; 1 unit = 1 m. CONTRACTS.md (a) follows it.

**What is known.** `func_0049E480.cpp` computes `m[r] = Σ_j B[r][j] · A[j]` (`v · B · A` with GT4's row vectors),
rows 0-2 scaled by `s->scale` when byte +0xE is set. By `s->current`: 0 -> A = `matrix[1]` (+0xF10), B = shared
vectors 28..31; 1 -> A = vectors 28..31, B = `matrix[0]` (+0xED0); 2 -> A = `matrix[1]`, B = `matrix[0]`. 004A5348
saves set k at +0xED0 + 0x40 k, so `matrix[0]` is set 0 and `matrix[1]` set 1, and B is always set 0, A always
set 1. 004ABEC0 sends light positions "through camera matrix set 0"; 0049F7D8 takes the camera position from set
0's inverse; 003B8EA0 reads a light's view position from set 0 after translating it to the light. 0049E548 then
writes rows 0-2 times the Cmd's shape scale into the batch. NATIVE_ONLY.md:68-70: vf20..27 set by the camera code
(003Exxxx).

**Questions.**
1. Is set 0 object-to-view and set 1 view-to-window (projection with viewport) in every 3D view: main, mirror,
   env-map faces, showroom, Photo Travel?
2. Where does each view load its camera into set 0, and at what stack depth (004A53F8 / 004A5400,
   `gt4_matrix_stack_top`)? Is set 0 at that point the world-to-view matrix of the course?
3. Does set 1 change inside a view (different projections for sky, mirror inset)?
4. Course instances: is their object space the world (set 0 equal to the camera), or do they carry their own
   transform?
5. Units and axes: GT4 units per metre (against `Z = 1.6e6 / view distance in metres`, LIGHTING.md:27-28),
   handedness, which axis is up, -z forward.
6. Does `D · set0 · set1` reproduce `ModelJob::matrix` for every batch (D = diag(axisScale · shapeScale, 1))?

**Method.** On a live race and the event screen (the game folder's build is not used; a release build of the
spiker's clone): log at 0049D328 `s->current`, both sets as used, the scales and `ModelJob::matrix`'s source
(vectors 24..27 after 0049E548); log 004A5348 switches and 004A53F8 / 004A5400 with stack depth, with the caller's
return address; log at `render_world` and `render_back_mirror` entry. Offline: recompose and compare (question
6), find the depth-0 set 0 per view (2), check course instances (4), fit units from GT4's Z against view-space z
(5). Cross-check the camera position from set 0's inverse against the car's position in a chase view.

**Time box.** 3 days.

**Fallback.** If the camera load cannot be pinned, the host estimates world-to-view per view as the course
instances' `objectToView` (if R1.4 says course objects are in world space), flagged without `kViewCameraKnown`.

---

## R2: Drafts in the hook paths (blocks A4, A7, A8; informs A5)

**What is known.** These functions on the extraction path run from provisional drafts: 00455708 (model display
program), 004A5348 (matrix set switch), 003B8EA0 (car lamp glows, A7 step 2, or 003B8530 which fills
their list; not the course's, which come from the matched 003B7B70), 004ABEC0 (light parameters), 00340950
(`render_cars`), 003E7A58 and 003E7F08 (smoke), 003C1430 (sparks), 0049CC48 (per-packet texture events), 003924D0
(`RaceCarSound::playStart`). docs/DRAFT_AUDIT.md: 15 of 427 drafts behaved wrongly when audited; nearly is not
the same behaviour. A draft bug in a hook path looks like an extraction bug.

**Question.** Does each of these drafts behave like the original on the inputs the extraction will see, and
which hooks can be placed in a matched or native neighbour instead?

**Method.** For each: `tools/match.py asm` / `check` against the decomp; a differential test in `tools/eeref`
(the `difftest_*.cpp` pattern, as 0049E480 has, `difftest_c.cpp:238`) on scratchpad and argument states logged
from a race; if it differs, fix the draft in `game_src/` first (its own ticket and commit). List alternative hook
points: for 00455708, its caller 00455xxx or a matched callee; for 003B8EA0, 0049C0D8 (matched) which receives
the quad.

**Time box.** 3 days for the four on the critical path (00455708, 004A5348, 003B8EA0, 0049CC48); the others before
their tickets start.

---

## R3: Coplanar and alpha ordering in the deferred pass (blocks B10, B22)

**Status: measured for the day race; B7's part answered** (lane B, [spikes/R3.md](spikes/R3.md), 2026-11-08).
On `race_boot.cap`, pixels that pass only at equal Z: opaque with Z write 3.0 %, blended with Z write 21 %, blended
without 2.6 %, opaque without Z write 2.5 %, second passes at equal Z 100 %. No class is free of order. So B7 draws
forward, in GT4's order, with GT4's own depth (PLAN B7), and needs no class rule. The split between a G-buffer and
an ordered forward pass is needed only where the view's own reverse-Z depth is used (B10, B22), and the two-order
prototype below moves there. Open before B10: night and the showroom, the destination-alpha passes, and which
views t5 and t59 are.

**Follow-up: the VU's rounding at near ties** (lane B, the coordinator, 2026-11-08). The shared shaders are precise
(CONTRACTS b.10), but not the VU's truncating rounding, so where GT4's depth tests are near-equal neither they nor
the fused code they replaced decide as the PS2 does. Baseline, PE race 2's countdown against PCSX2's software
renderer: of 109,473 pixels where fused and precise differ, precise is closer on 52,898, fused on 54,532, and
about two thirds match neither (CAPTURES.md's re-baselines). A transform that rounds as the VU does (truncation
after each operation) could beat both; it would be its own `ORIG` re-baseline, judged on the same stills.

**Question.** Which of GT4's scene-target draws rely on draw order rather than depth: coplanar layers (road
markings, kerbs, decals, shadows drawn on the road with equal Z under GEQUAL), alpha-blended surfaces (glass,
fences, glows, smoke), the car paint's destination-alpha pass (`Cs * Ad` with DATE, NATIVE_RENDERER.md:115-117),
Z-only clears (NATIVE_RENDERER.md:179-180), sky drawn without depth? Which go into the G-buffer and which into an
ordered forward pass that keeps GT4's order?

**Method.** On the race, flyover, night and showroom captures, classify every draw into the scene target by
(depth test and function, depth write, blend, alpha test, DATE) with `GT4_GSN_DRAWS=all`; for depth-tested pairs,
measure overlap with equal depth (a debug pass that counts pixels where a later draw passes GEQUAL at equal Z). Then
prototype two orders in a throwaway parity renderer: (a) opaque non-blended draws deferred, everything blended or
equal-depth-after-another in GT4's order on top; (b) everything in GT4's order (forward). Compare both against the
GS path. Decide the class rules for B7 and B22.

**Time box.** 4 days.

---

## R4: ARM64 toolchains and a device (blocks C5, C6)

**Device (owner, 2026-11-07).** The test device is the **AYN Odin 3**: aarch64 Linux (Armada Linux, Steam and
gamescope), Snapdragon 8 Elite, Adreno 830 through Mesa Turnip, reachable from the owner's Mac. There is no
Windows on ARM device, so **aarch64 Linux comes first**, and Windows ARM64 is a CI build only (built, not run).

**Question.** Which toolchain builds the host for Linux aarch64 from this x64 machine (and Windows ARM64 for
CI), and does the aarch64 host run on the Odin 3 with the x86-32 game under FEX (or the emulation Armada ships)?

**Method.**
- Linux aarch64: clang with an aarch64 sysroot matching Armada's glibc (or zig, which compiled 26/29 host files in
  the audit), SDL3 and the Vulkan loader for aarch64; C3's host glslang. Check FMA contraction in the aarch64 code
  of `renderers3d.cpp:270-272` with and without `-ffp-contract=off`.
- On the Odin 3: run the aarch64 host headless on the race capture (`--replay`, `ORIG` against x64); check Turnip
  exposes what the host needs (synchronization2, push descriptors, dynamic rendering, stencil export where used);
  then the x86-32 game under FEX with the host on the live link (needs C9's POSIX link if the game runs under
  Wine).
- Windows ARM64, CI only: install the MSVC `VC.Tools.ARM64` component (not on this machine: only
  `Hostx64/{x64,x86}`) or use clang-cl; cross build with C3; check `/fp:precise` codegen.

**Time box.** 3 days (aarch64 toolchain and the replay on the Odin 3: 2; Windows ARM64 CI build: 1).

---

## R5: What else is in the scene target (blocks B7, B8)

**Question.** Which draws into the scene target before its Light command are not model batches (the audit's
"dozen" per race frame: clears, Z-only clears, sky's flat parts, sprites, glows), and which targets read *after*
the Light command must also come from the modern renderer (env map, road reflection buffer, mirror)?

**Method.** `GT4_GSN_DRAWS=all` on the REF captures, grouped per scene target by command index relative to the
Light command; trace which targets later draws sample (`TextureSource::Kind::Target`). Output: a table of draw
classes and, per class, "the compositor keeps it on the GS path", "the modern view draws it" or "skipped".

**Time box.** 2 days.

---

## R6: Showroom passes, Photo Travel's Z24 pass, field frames (blocks A6, B9)

**Question.** How do the showroom's four jittered passes (NATIVE_RENDERER.md:171-176), Photo Travel's Z24 depth
pass (commits 6feab53, 49176b2) and the race's 224-line fields map onto views, and what does the modern path draw
for each?

**Method.** Captures of the showroom and Photo Travel; log the views A3 sends and the frame lists' targets.
Decide: one view per jitter pass, or one view and TAA instead; the Z24 pass from the modern depth; fields as
frames.

**Time box.** 2 days.

---

## R7: Captures and model bytes (blocks every capture-based acceptance)

**Question.** A capture recorded from a later VBlank (`GT4_HOST_RECORD=PATH,FROM,COUNT`) holds model packets the
game sent earlier only as content (`BatchModel::sent == 2`, "not decoded"). Can the mesh cache decode them?

**Method.** Check `GT4_REC_STATE` (protocol.h:60-66) and `stream.cpp` for what a mid-run capture carries; replay
one through B5's decoder and count meshes it cannot decode. If needed: record from game start, or add the kept
model contents to the state record (a capture format change, version 5).

**Time box.** 1 day.

**Mid-run captures (lane C, 2026-11-09; cause named, fix B25).** Ground truth for the day captures: a mid-run
sub-capture recorded while replaying a from-boot capture, replayed and compared with it over the same VBlanks:
`race_boot` at the grid and `menus_boot` at the track menus, 298 of 300 frames identical in each. Only the first two
frames after the state snapshot differ, in both tests (and `race_pe2_r`'s known dark frame 1). So the snapshot
carries what the day races and the menus draw from, and `race_grid43` and `race_pe2_r` are right past their first
frames (b.10's re-baseline and R6's pairing on `race_grid43` stand). The night laps recorded from a trigger
(`lap_ssr5r`, `lap_csr5`) don't.
- **Pinned** on `lap_ssr5r_boot` (lane F's, from game start, exact against the live game): its sub-capture from
  VBlank 9,926 differs from the from-boot replay on every sampled frame (36 over 3,600 VBlanks: 34-41 % of pixels
  identical, mean 2-4 levels, the worst 100-244), and the difference never decays. Not the frames' alignment, a
  shift, a field, a GS register or the snapshot's restore: every colour target matches at the first frame after it.
- **Cause (lane B, 2026-11-09): the snapshot holds no depth buffer.** `GsFrontend::drawnTargets` skips depth
  targets, so the snapshot reads back colour targets only (a sub-capture recorded from a lockstep replay reads back
  none), and a sub-capture starts with Z from GS memory, which the GPU-drawn Z never reaches. GT4 inverts Z in place
  each frame instead of clearing it, so the stale Z survives where the scene keeps covering it: under the car ahead.
  Found by limiting only VBlank 9,944's list, the history whole (`debug/b-r7-trace` 691bf4f): the same 577 draws in
  both replays; at draw 450 the 3D's Z buffer (bp0000) differs in 21 values, all on the car's silhouette; the 3D
  target (bp1a40) first differs within draws 465-469 (the car's body, then its env-map pass), 495 pixels by the
  frame's end. From there the difference likely spreads through GT4's feedback buffers (the glare levels, the post
  copy) to the whole picture by 10,926.
- **Ruled out on the way, still true of memory:** the 4-bit texture reads (on the nine pages first suspected, every
  read follows an upload in the same frame, so none reads a target's pixels), and blocks written by several sets (set
  textures are decoded from the set's own copy; switching to block-wise writes changed no pixel).
- **What is right:** the live game and every capture recorded from game start (lane F's 13 shots against the live
  game: mean difference 0.00). Only mid-run captures are affected, so until B25 lands, colours are judged on
  captures recorded from game start, never on a mid-run capture.
- **Fix: B25** (PLAN 96ea9ab): the snapshot reads the depth buffers back in their GS formats (Z24, Z32, Z16 through
  GsMemory's Z swizzle), counted, and a lockstep replay's recording gets its targets too. The regression test:
  `lap_ssr5r_boot`'s sub-capture pair, 36 of 36 frames identical (lane C), with `ORIG` unchanged.
- **B25 (e) parked (lane B, 2026-11-09; `parked/b25-depth-start`): the Z buffer aliases the display buffer.** Built
  and run, it changed no pixel of the pair. The 3D's Z buffer (bp0000, 640x448, pages 0-139) has its top half
  (pages 0-69) under the display buffer bp0 (640x224), which owns them in the page table, so the snapshot can't carry
  that half, and GS memory there holds the shown picture, not Z. On the PS2 the display copy overwrites that Z each
  frame and GT4's Z24 pass turns it round in place; our Z is a GPU image of its own, which the copy never touches,
  so its history isn't in GS memory for any snapshot to carry. The cause above stands. A fix needs colour and Z to
  share memory (an aliasing model, its own question for the architect), which may move from-boot `ORIG` (it can't
  see the difference while every draw covers what it tests; a measure of how often GT4 doesn't says). Until then
  references come from game start.

---

## R8: Material classes from lighting bodies (blocks B10)

**Question.** Does the mapping from GT4's 15 lighting bodies (VU1_PROGRAMS.md:553-569) to material classes hold
(0x10A8 / 0x12C8 car paint, 0x0CA8 / 0x0EA0 glossy, the N.L powers to roughness, 0x1508 prelit, 0x1608
projected, 0x16F8 fades)?

**Method.** After B6: per body, list the textures and objects that use it on the REF captures; render each class
in a debug colour; check by eye on cars, glass, chrome, course.

**Time box.** 3 days.

**Result (lane B, 2026-11-08; closed).** A spike (local, never pushed) ran
hybrid at `original` on `race_grid43` and `race_boot` (day) and `a6r_r5` (night): for each substituted item its
lighting body (`ModelJob::chain >> 16`), and for the N.L bodies the four light lanes (qwords 201..203 per lane,
colours 205..208, ambient 204) through the instance's `objectToView` into view space; a debug view coloured each
body (`GT4_R8_BODY=1` with `GT4_SIDE_VIEW=material`).
- **Classes**, from what each body does (`gt4_lighting.glsl`'s `lightVertex`), for MaterialTable's classify hook:
  - prelit: no chain (body 0, the course's bulk: 532,070 of `race_grid43`'s 977,197 items), 0x0B0 (the colour
    unchanged: the rest of the course, mostly env faces), 0x1508 (scale, offset, limit) and 0x16F8 (fade). Baked
    lighting: never relit, only shadowed and occluded;
  - car paint: 0x10A8 and 0x12C8 (a sphere map or a map by quadrant from a matrix in 201..203; no lights);
  - glossy: 0xCA8 and 0xEA0 (specular from the eye, one direction in 201..203);
  - diffuse N.L: 0x0118, 0x0290, 0x0418 and 0x0B48 by day, 0x05B0, 0x0788 and 0x0960 at night; roughness from
    the squarings (the power);
  - projected: 0x1608, kept as GT4 draws it. A body not listed keeps GT4's colour.
  The course is prelit everywhere, so B10 relights cars and objects and only darkens the course.
- **Day lights:** in the N.L bodies, lane 0 and lane 2 are each one fixed directional light: 0.00 degrees between
  every batch of a view, in all 697 lists of `race_grid43` and 1,666 of `race_boot` (lane 0 about (0.74, 0.48,
  0.47) in view space on `race_grid43`); lanes 1 and 3 are empty; plus the ambient (204). Their colours are per
  object (lane 0 from 10.6 to 49.3, the ambient from 4.5 to 17.9 between cars of one frame): GT4 scales them per
  object. So there is no one global sun: B10 relights with both lanes and the ambient, each object's own colours
  and the direction from the batches, no game record (`kSrcSun` stays reserved). How the per-instance light
  colours reach the scene stage (the side pass or the instance table) is B10's question.
- **Night:** no sun. All four lanes are per-object lights (up to 168 degrees apart within a view): the street lamps
  as a direction per object, so lamps stay F2's.
- **Showroom** (lane A's `r6_show3.cap`, the garage's turning 240SX, 1,500 VBlanks): the car's bodies are 0x0290,
  0x0418 and 0x0B48 (N.L), 0x10A8 (car paint), 0x1508 (prelit) and body 0, all in the classes above. Every view there
  is a menu view (role 8), which B8 keeps on the GS path, so substituting the showroom car needs its own role rule
  (B9). Its lights are the day's pattern: lanes 0 and 2 fixed directionals in white studio light (lane 0 at 128, the
  ambient 25), one direction per view for every instance.
- **Photo Travel** (lane A's `pt_r6.cap`: the shooting screen's preview and the shot): its batches' bodies are 0x0B0,
  0x0290, 0x0418, 0x0B48, 0x10A8, 0x1508 and 0x16F8, all in the classes above (counted in the CPU renderers). The
  capture holds no views: Photo Travel's render path sends no view records yet, so its lights per view are B9's to
  read once it does.
- **Left to B10's first step:** the untagged draws at equal Z over items (B10's veiled question), and the check that
  GT4's pixel divided by its interpolated lit vertex colour returns the texture (B10's channel). Lighting per pixel
  against GT4's per-vertex Gouraud is measured (B10's ticket: about 7 % of samples more than 4 levels apart).

---

## R9: The game's reads of its scene target (B8's read switch: a table per reader)

**Status:** no longer a blocker (the coordinator, 2026-11-08). With B8's in-place drawing, a list holding a read
whose reader is marked GS-only draws that view on the GS path, so the read sees the PS2's picture. R9 fills that
table reader by reader, GS-only by default; readers of Z alone are exact already (GT4's Z is kept). Readers that
read every frame come first, before hybrid goes into presets.

**Question.** The game reads pixels it drew (luminance probe 00330AE8, photo tiles 00333A00 / 00333E50, car
thumbnails, the flare samples 00497EF8; GRAPHICS_API_PLAN.md section H, GS_BACKEND.md:144). In `hybrid` and
`modern`, which of these read the scene target, and does it change the game's behaviour if they see the modern
image (exposure, flare intensity, which photo is saved)?

**Method.** Log each read's target and rectangle on the race, night and photo captures; for each reader, find what
the game does with the values (decomp source). Answer per reader: read the modern image, read a GS-drawn copy
(costs the GS draws on those frames), or answer from the modern image with a remap.

**Time box.** 2 days.

---

## R10: Where the Route 5 section brightness step comes from (blocks N2, A11b)

**Status: closed for the Reverse layout, 2026-11-08: no one-frame step found** (`spikes/R10.md`, coordinator's
decision for the owner). The brightness changes at fixed track positions build over 8-20 frames. Grading and fog
colour are ruled out; batch lighting values at 507 and 2514 (1049 and 2859 only partly); the glare was not checked.
Most of the largest change is the lit-road layer, which goes to N3. N2 and A11b (the section index) are parked. N2
reopens if a capture from lane F's sessions on the Normal layout or a night city course shows a one-frame step
(PLAN.md N2); the method below is kept for that.

**What is known.** Owner's playtest: driving from one section of S.S. Route 5 at night into the next, brightness
or contrast changes in one step; PCSX2 does the same, so it is GT4's. Nothing in the docs or code names course
sections; the candidates are: per-section prelit vertex colours, per-section lighting constants (the cars' light
matrices from the 0049EA80 family, `MLightSetting::read` 0x00210988), per-section fog (FOGCOL, density), per-section
colour-grading parameters (the `kGrade` effect record's curve and weights, 004A7D90), the glare's threshold.

**Question.** Which of these changes at the boundary, by how much, and does it affect the course, the cars, or the
whole frame?

**Method.** On N0's sections capture, per frame across each boundary:
1. log the `kGrade` record's curve and weights (`FrameList::Effect`), the fog colour of the 3D draws
   (`FrameList::Lighting::fogColour`), the glare's bright-pass state (the draws between the post markers or by
   their ALPHA/FIX), and the car batches' light matrices (qwords 201..216 in the model data);
2. compare the prelit vertex colours of the same road surface on both sides of the boundary (the CPU 3D renderers'
   output for those batches, `renderers3d.cpp`);
3. ablation: replay with each candidate held at its pre-boundary value (grading curve, fog, light constants) and
   measure the step with N1; the candidate whose hold removes the step is the source.
Check `CourseData::render_course` 0x00396E10 / `render_model` 0x00397188 for a section index (needed by A11b).

**Time box.** 3 days.

---

## R11: Which draws make the lamp bands (blocks N3, A5b)

**What is known.** Some street lamps on Route 5 at night throw wide vertical bands of light on the asphalt, also
on PCSX2. Candidates: the road reflection pass (`render_reflection` 003411D0, `render_reflection_car_lights`
0x00341A30) drawing lamp reflections as stretched sprites; tall glow quads from 003B8EA0; the glare's taps.

**Question.** Which draws, from which function, produce the bands, and can they be bracketed by markers without
also removing something wanted?

**Method.** On N0's lamps capture: `GT4_GSN_DRAWS=BP` for the scene target, then `GT4_GSN_LIMIT=K` bisection to the
first draw that adds a band; record its state (blend, texture, sprite shape, which target it samples); find its
caller (a temporary log of the pgl caller's return address in `runtime/native/pgl.cpp`). Check the same frame on
PCSX2's software renderer to confirm it is the same thing.

**Time box.** 2 days.

---

## R12: Where the full-frame yellow tint from warning lights comes from (blocks N4, A5b)

**What is known.** Blinking yellow warning lights tint the whole frame yellow, also on PCSX2. GT4's flare pass
draws a sample per headlight, lamp or sun into a 64 x 8 target every frame a light is in view and reads it back
(003B88C8, 00497EF8, 00497F48; GS_BACKEND.md:144), so the game knows each light's visible intensity; its glare
(003BADF8) bright-passes the frame and adds it back.

**Question.** Is the tint the flare's full-screen draw, the glare's add-back, or the grading, and which draws
carry it?

**Method.** On N0's warning capture: correlate the frame's mean Oklab a/b (N1) with the flare samples each frame
(`GT4_GS_READ_DUMP=DIR`) and with the lamp's blink; ablate in a replay: skip the draws between the flare call's
start and end (temporary), skip the glare, hold the grading; the ablation that removes the blink-synchronous tint
names the source. List its draws for A5b.

**Time box.** 2 days.

---

## R13: Upscaler SDKs (blocks B17)

Lane C (2026-11-08), before any SDK enters the repo.

**Question.** Can FSR 3.1 (Vulkan), DLSS (Streamline or the NGX Vulkan SDK) and XeSS (Vulkan) be built and
redistributed under this project's licence and release policy (docs/RELEASE_POLICY.md), what may go in the zip
and the tar, and on which architectures and systems (DLSS: x64 only)?

**Method.** Read each SDK's licence and redistribution terms; check against the release policy; a build test of
each on x64.

**Time box.** 1 day of reading, 1 of build tests.

**Result (lane C, 2026-11-08; the coordinator, the architect and lane B agreed).** FSR 3.1 is cleared; DLSS and
XeSS are not. The project is GPLv3 (`LICENSE`), and RELEASE_POLICY.md says never to package proprietary SDK
artifacts.

- **FSR 3.1: cleared.** FidelityFX SDK v1.1.4 (tag c6efa6b, upscaler 3.1.4) is MIT (`LICENSE.txt`,
  `sdk/LICENSE.txt`). It has the upscaler's whole source: the host side (`sdk/src/components/fsr3upscaler/`) and
  the ten Vulkan GLSL passes (`sdk/src/backends/vk/shaders/fsr3upscaler/`), with their headers under
  `sdk/include/FidelityFX/gpu/` (`fsr3upscaler/`, `spd/`, `fsr1/`). It is vendored unmodified, as FSR 2.2.1 is,
  with a THIRD_PARTY_NOTICES row. Its GLSL is compiled with our glslang, and our own Vulkan glue replaces AMD's
  `ffx_vk.cpp` and shader compiler. It builds into `gt4_host`, so the zip and the tar carry no extra file.
  Build test (x64, MSVC and the host's glslang): all ten passes compile to SPIR-V (`--target-env vulkan1.1`,
  FSR 2's option set), and `ffx_fsr3upscaler.cpp`, `ffx_assert.cpp` and `ffx_object_management.cpp` compile
  (`cl /std:c++20 /W3`) without errors. Linux and arm64 use the same sources as FSR 2's path, and B17's CI builds
  prove them. SDK 2.x (2.0 to 2.4) ships the upscaler as AMD-signed DLLs under a licence that grants
  redistribution "in binary form only" and forbids reverse engineering. So a later FSR means re-reading its
  licence first; it is not a pin bump.
- **DLSS: not cleared.** NVIDIA RTX SDKs licence (NVIDIA/DLSS `LICENSE.txt`). The SDK is binary only
  (`nvngx_dlss.dll`, `libnvidia-ngx-dlss.so`). Its 4.e says "You may not use the SDK in any manner that would
  cause it to become subject to an open source software license". Its supplement licenses it "only for their
  use in systems with NVIDIA GPUs" and requires the NVIDIA marks on splash screens, the about box and the credits.
  A GPLv3 program that ships a proprietary library which is not a system library needs a GPLv3 section 7
  exception from every copyright holder.
- **XeSS: not cleared.** Intel Simplified Software License (intel/xess `LICENSE.txt`). The SDK is "provided in
  binary form only", redistributed "without modification", with "No reverse engineering, decompilation, or
  disassembly". It is `libxess.dll` plus the MSVC runtime DLLs, for Windows x64 only (no Linux, no arm64). It has
  the same GPLv3 and policy conflict.

Allowing DLSS or XeSS would take a GPLv3 exception and a change to RELEASE_POLICY.md. That is the owner's decision,
not a lane's, and it is on the owner's list. B17 is FSR 3.1 only.

---

## R14: Vehicle state (blocks D6, A9, D8, B19, D9)

**Question.** Where are the per-wheel load, slip and contact state, and the steering rack's torque, in the car
object? The physics core has no symbol names; the nearest are `AutomobileControlRecord::Recorder::steer_update`
0x00345C38, `GtbSetupTireTwistParameters` 0x0035F808, `TireWearParameters` 0x00367D20-40.

**Method.** `tools/gt4dbg.c --watch-write` on the car object while steering, braking and sliding (docs/
NATIVE_GAME.md:77-80); diff memory snapshots between straight running and a slide; follow writers into the
decomp.

**Time box.** 5 days; if nothing is found, FFB and tyre marks ship from the body transform and inputs only.

---

## R15: LGDEV effect structures (blocks D7)

**Status: answered** (lane D, `spikes/R15-lgdev-effects.md`, 2026-11-09): GT4 drives three effects, each a 60-byte
block in one layout: A, a constant force (type 0, direction 90, endless) whose magnitude (-255..255) it updates every
frame of a race from its physics; B, a damper (type 0x10), and C, a centring spring (type 7), set when the race
starts. Confirmed live with a fake Driving Force Pro (`GT4_FAKE_DFP=1`): the downloads matched the code byte for byte,
A was updated 1,216 times in a race. Three of IOP_SERVICES 5.8's labels were wrong and are corrected. D7 can start.

**Question.** What are the parameter layouts of the effects GT4 sends through LGDEV fno 8-16 (IOP_SERVICES.md:
595-611; 005552B8, 00556298, 00561288...)?

**Method.** Log the RPC buffers with a device reported present; match against Logitech DFP behaviour.

**Time box.** 3 days.

---

## R16: Audio reply timing if the mixer moves to the host (blocks D5)

**Status: answered** (lane D, `spikes/R16-audio-reply-timing.md`, 2026-11-08): the replies can't come even one
frame late. With them delayed by one frame a race has 86-88 % more key-ons and 4-5x key-ons onto releasing voices
(2.4-2.5x at two and four frames): the driver keys voices again from a status that still shows them stopped, and its
re-key gap moves by exactly the delay. No voice ran short, but what is heard changes. So D5 answers from a game-side
voice model in the same call (PLAN D5).

**Question.** GT4's sound driver frees voices by ENVX/ENDX replies. With the mix on the host, how late can those
replies arrive before GT4 runs out of voices or cuts sounds?

**Method.** Delay the replies in the current game-side mixer by 1, 2, 4 frames and count voice starvation and
cut sounds in a race.

**Time box.** 2 days.

---

## R17: Guest memory beyond 32 MB (PLAN P5)

**What is known.** The runtime gives the game the PS2's 32 MB: `kRamSize`, `GetMemorySize` returns 32 MB and
`EndOfHeap` 0x01E00000 (`runtime/kernel.cpp:1179-1189`). Uncached mirrors of the RAM sit at 0x20000000 and 0x30000000
(`guest.cpp:99`), and the scratchpad at 0x70000000.

**Question.** What sizes the game's heaps: newlib's heap from `SetupHeap` / `EndOfHeap`, the game's own pools (such
as 008744D0, `GT4_HEAP_LOG`), or constants in its code? Would any of them use more memory, and for what (more cars
on track, longer replays, fewer loading stalls)? Does any game code still use the uncached mirrors?

**Method.** Follow the heap setup in the decomp; a session with `GT4_HEAP_LOG` to see each pool's high-water mark; an
audit run that leaves the mirrors unmapped and logs the faulting functions. The runtime masks the 0x2 / 0x3 addresses
the game hands it (`frame.cpp`'s `ptr()`, the flare buffer, read destinations): those are values, not mirror
accesses, so the audit counts only game code that dereferences a mirror, which is the question. A debug runtime
build; nothing goes to main.

**Time box.** 2 days. The answer is "lift it, to N MB, for this" or "nothing would use it".

---

## R18: The sound driver without the SPU2 register image (PLAN P6)

**Status: answered, no** (lane D, `spikes/R18-sound-driver-without-image.md`, 2026-11-09): the register image stays.
The driver reads back only each voice's ENVX (in `func_0055D890`, to free a voice at 0; ENDX is never read, the
whole ELF checked), and R16 showed it can't come late, so a native API would still need the game-side `Spu2` model
D5 already runs. The gain is too small: the image is 928 bytes a frame (with D9's tags, about 0.4 % of a race's
stream) and 4.2 us to apply at most, and what a native API was to enable (positional audio, per-voice effects,
higher quality, sample replacement) is done or doable from the image (D9, `audio.interpolation`).

**What is known.** GT4's sound driver (game logic) writes an SPU2 register image every frame (SPUP fno 4). It frees
voices by the ENVX / ENDX replies. `runtime/audio.cpp` (and D5's host mixer) mixes voices the way SPU2 does: ADPCM,
ADSR, pitch, reverb.

**Question.** Can a native voice API, called where the driver decides to play, pitch, set volume and release, replace
the register image, with the same timing and voice release? Or does the driver depend on the register image's
semantics too deeply to gain anything?

**Method.** Map the driver's call sites from the decomp (where it builds the image), and count the register fields
it reads back.
- **Prototype order:** a one-shot class first (a menu sound: key-on, volume, release, no modulation), then the
  engine, the hardest (per-frame pitch and volume, loops, voice stealing), so the engine's failures don't hide
  simpler ones.
- **Comparison from one capture:** two live sessions' audio isn't sample-repeatable, so they can't be compared. One
  capture records both the SPUP register image, as today, and the prototype's native voice calls beside it (a new
  record type that today's host ignores). Each path is mixed offline from that same capture and compared at D5's
  -60 dB bar. Timing: each key-on and release lands in the same frame as on the image path, or in the same SPU tick
  if the driver writes mid-frame.
- **The samples:** a native voice API still plays the driver's ADPCM banks from SPU RAM. So the spike says whether
  the SPU RAM image stays (likely, as the upload path) and only the per-frame register image goes.

**Decision rule.** Go only if both hold:
- the gain is measurable: the per-frame image transfer bytes and the host's parse cost, measured, plus what a
  native API enables (positional audio, per-voice effects, higher rates);
- every read-back the driver needs (ENVX / ENDX for voice release and stealing, and any others the count finds) can
  be answered from the native voices with the same semantics.

Otherwise the answer is no, and the register image stays.

**Time box.** 3 days, after D5.

---

## Other risks, no spike needed now

- **Motion vectors for earlier depth partitions.** The scene stage's motion-vector pass redraws the 3D with depth
  EQUAL against the frame's Z at the Light, but GT4 clears Z between its depth partitions (R5), so surfaces drawn
  in an earlier partition (the far course) get no motion vectors today. TAA and motion blur treat them as static.
  B8's per-partition side-channel passes give the substituted items theirs; the GS path's own pass would need the
  same per-partition split, measured when TAA ghosting on distant scenery is reviewed (B16).

- **Draw count of the modern path.** 1,000 instances and 1,702 batches a race frame: welded meshes and GPU culling
  (B7, B10) keep it low; measured by `PERF` from B7.
- **`original` regression.** Mitigated by `ORIG` on every ticket and by the option that keeps the game from
  sending records at all (ARCHITECTURE.md section 6).
- **Hook ABI.** Game code compiled for the EE data model calling the runtime: `tools/hook_lint.py` in every A
  ticket.
- **Determinism and replays across architectures.** x86 and ARM builds of the host give the same pictures only
  with `-ffp-contract=off` (C5); the game stays x86-32.
- **Native ARM64 game.** A research track (audit section 5.2), not in this plan.

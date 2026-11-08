# Implementation plan

Status: design for review. Decision (owner, 2026-11-07): modernisation comes first; the 0.1.1 release waits.
Architecture: [ARCHITECTURE.md](ARCHITECTURE.md). Contracts: [CONTRACTS.md](CONTRACTS.md). Spikes: [RISKS.md](RISKS.md).

Sizes: **S** = 1-3 days, **M** = 1-3 weeks, **L** = 1-3 months, for one agent. References at `main` a80cfca.

---

## 1. Rules

1. **main only.** No branches. Rebase on `origin/main` before every push; never force-push.
2. **Small commits.** One ticket is one to five commits. Each commit builds, passes ctest and does one thing.
3. **Review before push, by someone other than the author.** The author commits locally, runs the ticket's
   acceptance, and sends the reviewer the commit hashes and the clone path over arc. The reviewer fetches from the
   author's clone (`git fetch <path> HEAD`, read-only), reads the diff, reruns the acceptance, and answers OK or
   the changes needed. Only then does the author push. The reviewer never edits the author's clone.
4. **Clean release builds only.** Acceptance runs on `tools/release_game.py` game builds (it refuses uncommitted
   inputs, so commit locally first) and on hosts built from an empty build folder by `tools/build_windows.cmd
   Release`. No acceptance on incremental `build/game` objects.
5. **No Sony or Polyphony data in the repo**: no captures, frames, textures, frame hashes or reference images.
   They stay in each clone's `build/` (ignored). Tests that need them skip with exit code 77.
6. **Every visual change is checked** against the GS path (`frame_diff --baseline`) or, for `original` and
   parity, against PCSX2's software renderer (CAPTURES.md procedure, ticket C10).
7. **Settings.** Each modern feature is a setting with presets. `original` is the PS2 path and gets none of them.
8. **Lane ownership of files** (section 3). A ticket that has to touch another lane's file says so and goes in
   the order section 3 gives.

---

## 2. Acceptance vocabulary

Every ticket's acceptance uses these names.

| Name | What runs | Pass |
|---|---|---|
| `CTEST` | full ctest, Release | all pass (77 = skipped is a pass only where the ticket says) |
| `ORIG` | `tools/frame_diff.py --baseline OLD NEW --preset original` on the REF captures, every frame; plus `host_replay` | identical frames |
| `PRESETS` | the same at `original`, `low`, `medium`, `high`, `ultra`, and the test scene's `tests/scenarios/features.json` captures | identical frames |
| `PARITY` | `render.scene=parity` against the GS path at 1x on the REF captures, per frame | >= 97.5 % of pixels identical, >= 99.97 % within 4 per channel (the GPU-models step's bar, docs/NATIVE_V2.md:90) |
| `VAL` | REF captures with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, synchronization validation on | zero messages |
| `PERF(x)` | `tools/perf_report.py` on `--profile-json` of the REF captures against the baseline build | the named stages within x |
| `REL` | game from `tools/release_game.py`; `tools/hook_lint.py` | builds, lint clean |
| `SCENE` | scene-decoder counters (CONTRACTS.md a.7) on REF captures recorded with scene records | the gates in that table |
| `STREAM` | the same scripted live session (boot from the approved save, 1,800 host frames) twice with the base build and once with the new one; the host summary's `GS:` line | with scene records off, the summary's `scene` count is 0 (exact: nothing new on the wire); stream counts per VSync within 2 % of the base runs' mean; 0 bad records, 0 unknown codes (raw counts differ run to run: the game runs on its own clock beside the host, about 1.7 % between two base runs) |
| `R5` | the Route 5 night metrics (`tools/frame_metrics.py`, ticket N1) on the N0 captures | as each ticket states |

**REF captures** (local, `build/caps/REF.txt` lists them): `race7.cap` (race, 100 frames); from C10, recorded
from boot with display records: a race, the event screen and menus, a night race, a wet course, the showroom,
Photo Travel, a post-race replay, a menu with a movie background, the track menu; from N0: Route 5 at night.
Until C1 lands, single-frame checks use `--frames N --capture`.

**Every pixel gate reports a non-zero-pixel count per capture**, and a capture that shows an all-black frame is
not a REF capture. `refcap/race.cap` and `refcap/event.cap` (gt4-native-game/build) have no display records: they
replay black at every preset (`display 0/0` in `GT4_GSN=stats`; 0 of 286,720 pixels non-zero on race.cap), on
a80cfca too. Their frame lists are still built, so they serve draw statistics (the 93.5 % model share,
ARCHITECTURE.md section 1) and the CPU-side tests (B5's mesh check, A10's decoder), never `ORIG`, `PRESETS`,
`PARITY` or `VAL`. C10 replaces them first.

---

## 3. Lanes and file ownership

| Lane | Owns | Shared files it may touch, and in which order |
|---|---|---|
| A. Scene extraction | `game_src/` hooks, `runtime/native/pgl.cpp`, `runtime/frame.cpp`, `runtime/runtime.h`, `runtime/kernel.cpp` (from P4), `compat/game/gt4_scene.h`, `include/gt4/gfx.h` scene section, `platform/scene/scene_decoder.*`, `scene_frame.h` | `stream.cpp`, `stream.h`, `gs_host.cpp` (QueuedTarget): A1, A10. `gs_frontend.*`: A10 **after E2**. `protocol.h`: A1 **before D2**. `runtime/gs.cpp` `on_frame` (ticks deferred during a walk, CONTRACTS a.5.1): A3 |
| B. Renderer | `platform/render/graph/`, `platform/render/modern/`, `platform/scene/mesh_cache.*`, `material_table.*`, `pipeline.cpp`, `gsn_scene.cpp`, `vk.*`, shaders; for P2, the target, page and palette parts of `gs_frontend.*` and `gs_memory.*` (lane A reviews) | `renderer.cpp`, `main.cpp`: B2 **first** (day one) |
| C. Platform, ARM64, tooling | `cmake/`, `tools/build_*`, `tools/package_game.py`, `tools/frame_diff.py`, `tools/perf_report.py`, `platform/core/`, `link_host.cpp`, `docs/modern/CAPTURES.md`, `platform/gsnative/texture_pack.*`, `platform/sdl/session.*` and `tools/host_session.py` (the test harness), `runtime/link.cpp` (lane A reviews every change: its async reads and tick deferral) | `main.cpp`, `renderer.cpp`: C1 **after B2** |
| D. Systems | `platform/input/`, `platform/audio/`, `runtime/memcard.cpp`, `platform/saves/`, `runtime/iop.cpp`, `runtime/audio.cpp` | `main.cpp`: D1 **after C1**. `protocol.h`: D2 **after A1**. `stream.cpp`: D5 **after A10** |
| E. UI | `platform/render/overlay.*`, `platform/sdl/settings*.{h,cpp}`, `settings_menu.*`, `platform/render/modern/layer2d.*` | `gs_frontend.cpp` `textureFor` (E2), `gsn_display.frag` (E3): day one, **before A10** |
| F. Night lighting | `include/gt4/render/light_list.h`, `shaders/light_list.glsl`, `platform/render/modern/lights_*`, `tools/frame_metrics.py`, the Route 5 tickets | `gsn_scene.cpp` scene stage: F2 **after B4** |

`settings.cpp` / `settings.h`: E1 splits the option table into per-module files on day one; after that each lane
adds its options in its own file (`settings_scene.cpp`, `settings_input.cpp`, `settings_audio.cpp`,
`settings_lighting.cpp`) and the shared table only lists them.

---

## 4. Tickets

Format: **ID: title (size)**. Files and functions. Depends. Acceptance.

### Lane A: scene extraction

**A0: Spike R1, the matrix semantics (S, 3 days).** RISKS.md R1. Local instrumentation only, not committed.
Depends: none. Output: `docs/modern/spikes/R1.md` with the answers and numbers. **Day one.**

**A1: Scene record contract in code (S).** `include/gt4/gfx.h` (`kScene = 11`, namespace `gfx::scene`, every
struct and static_assert of CONTRACTS.md a.4); `compat/game/gt4_scene.h` (C copies); `compat/game/gt4_gfx.h`
(`gt4_gfx_scene`, `gt4_scene_on`); `runtime/native/pgl.cpp` (`gt4_gfx_scene` through `append`, fills
`FrameBegin::frame/vblank`); `runtime/frame.cpp:266` (bound to `kScene`); `runtime/runtime.h:26`
(`kOptionSceneRecords = 5`); `include/gt4/link/protocol.h:22` (version 3); `platform/gs/stream.cpp` (`case
gfx::kScene`), `stream.h` (`GsTarget::scene`, no-op default), `gs_host.cpp` (QueuedTarget forwards);
`tests/scene_records.cpp` (layouts: `sizeof`/`offsetof` of both headers). Depends: CONTRACTS.md reviewed.
Acceptance: `CTEST` (new `scene_records`); `ORIG`; `REL`; `STREAM` with the option off. **Day one
(after the contract review).**

**A10: Scene decoder and draw linkage on the host (M).** `platform/scene/scene_decoder.{h,cpp}`,
`platform/scene/scene_frame.h` (CONTRACTS.md a.7, a.8); `stream.cpp` (decoder at `kScene`, `ModelJob::pad[0]` =
instance serial at each `kBatchRun` of an instance, `stream.cpp:254`); `gs_frontend.{h,cpp}` (`FrameList::scene`,
`Draw::view`, `setSceneSplit(bool)`, the merge at `gs_frontend.cpp:1792` stops at a view change only when the
split is on, view-to-target binding); counters in `GT4_GSN=stats` (`gs_host.cpp:174`). Depends: A1, E2 landed
(same file). Acceptance: `CTEST` (decoder tests on synthetic streams: each counter moves in its own test, a
well-formed stream leaves every error counter at 0); `ORIG` and `PRESETS` with the split off; with the split on and
a capture with records, frames identical to the split off (merging does not change pixels); draws added by the
split reported; on a race capture, `viewSplitTargets == 0` for main views, with `viewAuxiliaryDraws` and
`viewAuxiliaryReads` reported by view role (CONTRACTS a.5.7).

**A2: Instance records at 0049D328 (M).** `game_src/func_0049D328.cpp`: after 0049E480 / 0049E548, before the item
loop, one `Instance` from the scratchpad (`current`, set 0 as used, `scale` when +0xE, `cmd->value`, `count`,
`flag`, `bits`); `section` 0xFFFF until A11b. Depends: A1, A0's answer. Acceptance: `REL`; `ORIG`; a new race
capture with records: `SCENE` (`matrixMismatch == 0`, `itemsMismatch == 0`); game CPU per frame up by less than
0.2 ms (host summary "CPU a frame: game").

**A3: Frame and view records (M).** `game_src/func_0033DEA8.cpp` (`FrameBegin`, the main view); `FrameEnd` from
the runtime at the kick (`runtime/frame.cpp`, `runtime/native/pgl.cpp`'s per-frame counts; CONTRACTS a.5.1 and its checks);
the HUD widget's view (003BE3C8, `kViewHud`); the tick deferred during a walk (`runtime/gs.cpp` `on_frame`);
`game_src/func_00341E50.cpp` (mirror, `kViewInset`); the camera as A0 found it (set 0 at its stack base);
menus' 3D entry for the showroom and event screen (A0 names it). Depends: A1, A0. Acceptance: `REL`; `ORIG`;
`SCENE` (`viewsNested == 0`, `countsMismatch == 0`, `instancesWithoutView == 0` in race frames, `cameraMismatch == 0`
on course instances, which checks something from A11a on); with A10: `viewsUnbound == 0` for main views. Tick
deferral: a live race run, before and after, reports the ticks that landed mid-walk; after it, `framesUnclosed ==
0` with that count above 0, and frame pacing (the host's frame-time stats) unchanged within noise; with scene
records off, the same run's link stream and timing unchanged from before (the deferral is off there).

**A4: Object identity from 00455708 (S-M).** `game_src/func_00455708.cpp` (draft) sets a runtime-global model
context (set, model index, level) that A2 reads (`kInstObjectKnown`). Depends: A2, RISKS R2 verdict on 00455708.
Acceptance: `REL`; `ORIG`; on two race captures, the same static object keeps the same (set, model) over 100
frames; LOD changes reported with distance.

**A5a: Post, HUD and 2D markers (S).** Around the post-effect and panel calls in `render_world`
(`game_src/func_0033DEA8.cpp`; if they are reached through vtables not visible there, `game_src` copies of
0x0033F890 and 0x0033FA40 from their matched sources); menus' 2D-over-3D. Depends: A3. Acceptance: `REL`; `ORIG`;
on race and event captures, every frame with a Light command has `PostBegin` < `PostEnd` < `HudBegin` < `HudEnd`
in command order after it; the draws between `HudBegin/End` are the HUD (`GT4_GSN_DRAWS=all` listing).

**A5b: Flare, glare and lamp-band markers (S).** `kMarkFlare*` around 003B8848's flare pass (003B88C8 samples and
its draws); `kMarkGlare*` around 003BADF8 (from 0033F1C8); `kMarkLampBands*` around the draws RISKS R11 finds.
Depends: A3, R11, R12. Acceptance: `REL`; `ORIG`; on the N0 captures, the bracketed draws are exactly the ones R11
and R12 listed.

**A6: Further views (M).** Env-map passes 0044E660 / 0044EB10 (`kViewEnvFace`), the road reflection
(`render_reflection` 003411D0, `kViewReflection`), showroom jitter passes, Photo Travel. `Instance` records for
the draw paths that run after a Cmd (CONTRACTS a.5.3): 00497838's pre-built shapes, placed by position (what
their `objectToView` is), and 0049FAC8's Cmd-like models, checking that their 0049E480 / 0049E548 composition
follows R1's rule. Depends: A3, R6. Acceptance: `REL`; `ORIG`; `SCENE` on the showroom and photo captures; each
env face's views bound to the 128 x 256 env target; `runsAfterInstance == 0` in race frames once both paths send
`Instance`s.

**A7: Light records (M).** Sources, corrected by lane A from the code (2026-11-08, a scan of the ELF for every
store to the lists' counts). The light-glow pass 003B88C8 draws from two per-frame lists in its object:
- **Step 1, course lamps and warning lamps: `entries2[100]`** at `+0x184C` (count at `+0x1848`; 24-byte hits:
  view-space x, y, z, distance, a visibility flag, the source pointer, which gives the params index). Its only
  writer is 003B7B70 (matched source, `src/race/unit_003B7470`, so R2 does not block it), reached from
  `RacePS2Base` virtual 126 (method 0x3F0) -> 00397288 -> 00397AE8 -> 00397BA0. Each source has `params[index]`
  with a blink value: above 0 it is a warning lamp (`kSrcWarning`, its smooth blink factor 0..1 in `intensity`),
  otherwise a lamp (`kSrcCourseGlow`). One `Light` per hit: point, range 0, colour = the light's rgb x the
  pass's rgb / 255, intensity without the view-dependent term (fields as agreed with lane F).
  - When: 0033EAB8 empties both lists (003B80E8, which also saves the current matrix at `+0x21B0`) after the main
    `ViewEnd` and the mirror view, then 003B7B70 fills `entries2[]` in method 0x3F0, with this frame's camera
    (positions through the matrix current at fill time, vf28-31), before the flare read 003B8848 and the glow draws
    (003B88C8 from 0033F2E8). The `Light`s are sent right after the fill, tagged with the main view's id: a `Light`
    may follow its view's `ViewEnd` within the frame (CONTRACTS a.5 rule 4). F2 reads them from the frame's
    `SceneFrame` by view id, wherever the Light command falls.
  - Checked on a night capture before relying on it: the counts per frame, that the positions are in the main
    camera's space, and where the Light command falls against the fill.
- **Step 2, car lamps** (headlights, tail and brake lamps): `entries[100]` at `+0xA38` (count at `+0xA34`; view-space
  x, y, z with -z as depth, a `Light*` with width `+0x40`, height `+0x44`, rgb `+0x54`, a flag, dx / dy). Its only
  writer is 003B8530, called only from `RaceCarModel` virtual 35 (003911D0); 003B8EA0, from virtual 36 (00391470),
  draws the same lamps' glows. The step reads the lamps from `entries[]` (if 003B8530 is not a draft; it is not in
  the decomp's `src/`, lane A checks) or from 003B8EA0 (a draft: R2's verdict). It also needs A6 / D6 for the car.
  The car lights' reflections (0x00341A30) are not sent as lights: they would light the scene twice, and N3 handles
  their sprites.
Depends: A3; step 2 also A6, D6, and R2 on 003B8EA0 or 003B8530. Acceptance: `REL`; `ORIG`; on a night capture,
each glow quad the GS path draws has a `Light` whose position projected through the view's projection and viewport
lands inside that quad (count of misses = 0); step 1 with the course and warning glows, step 2 with the car
lamps; `SCENE`: `lightViewUnknown == 0`, `lightsAfterViewEnd` reported.

**A8: Emitter records (M).** `RaceSmoke::update` 0x003E7A58, `SparkParticle::Update` 0x003C1430,
`ParticleManager::Update` 0x003C2780 (drafts). Depends: A3, R2. Acceptance: `REL`; `ORIG`; on a race capture with
tyre smoke and sparks: emitters per frame reported, positions within the projected bounds of the sprites the GS
path draws for them.

**A9: Vehicle record (S).** From the per-car state D6 found, sent at the kick in `FrameEnd`'s own block, before
`FrameEnd` (CONTRACTS a.5 rule 4; `runtime/frame.cpp`, lane A reviews). Depends: D6. Acceptance: `REL`; `ORIG`;
the record's transform matches the car body instance's `objectToWorld` within 1e-3; speed matches the HUD's speed.

**A11a: Course instance flag (S).** While the course is drawn, the runtime marks the instances A2 sends with
`kInstCourse`. The course is drawn in three places (lane A, from the code): 00396D40 (the race's main view, near and
far passes, called directly from 0033EAB8; it never calls `render_course`); `CourseData::render_course` 0x00396E10
(the mirror, split screen, 003403B8, 00397048); and the env faces' course list in 0044E528 (its `ModelSet2` calls
on `scene->f14`, not the sky's `f5C`). That makes A3's camera check real (`cameraChecked` counts course instances;
until A11a it checks nothing), and B7 relies on a validated `worldToView`. The sky (00396B80) stays unflagged;
`kInstSky` can be set the same way later. Depends: A2. Acceptance: `REL`; `ORIG`; on a race capture with records,
`cameraChecked > 0` in every race frame, in the main view and the env faces, and `cameraMismatch == 0`; car, wheel
and sky instances never flagged.

**A11b: Course section index (S).** `render_model` 0x00397188 sets the runtime's current section, which A2 writes
into `Instance::section`. **Parked with N2** (only N2's parts b and c need it); it is built if N2 reopens. Depends:
A11a, N2 reopened. Acceptance: `REL`; `ORIG`; on N2's capture every course instance has a section, and the section
under the camera changes at the crossing N2 reopened on.

### Lane B: renderer

**B2: Persistent pipeline cache (S).** Land the uncommitted work in `C:/Users/julie/gt4-native-game`
(`platform/render/vk.{h,cpp}`: `Device::pipelineCache`, `open/save/closePipelineCache`, every
`createGraphics`/`createCompute` through it; `platform/sdl/renderer.{h,cpp}`: open after `vkCreateDevice`, save and
close before `vkDestroyDevice`; `platform/sdl/main.cpp`: `pipeline_cache.bin` beside `settings.ini`,
`GT4_PIPELINE_CACHE=FILE|0`). Fix before landing: replace `remove` + `rename` with `MoveFileExW(...,
MOVEFILE_REPLACE_EXISTING)` / POSIX `rename`. Depends: none. Acceptance: `CTEST`; `PRESETS`; second run of the
race capture: "Pipeline cache: ... KB" logged and the hitch log's pipeline stage at the first race frame under
100 ms (840 ms measured without); a cache file with another `pipelineCacheUUID` is not used (unit test of the header
check); `GT4_PIPELINE_CACHE=0` runs without a file. **Day one.**

**B1: Render graph core (M).** `platform/render/graph/render_graph.{h,cpp}` (CONTRACTS.md b.2-b.7);
`tests/render_graph.cpp`: compile on the CPU only (ordering, culling, version errors, barrier lists, aliasing
placement, async edges) with a fake device; a GPU smoke test on the test scene. Depends: CONTRACTS.md reviewed.
Acceptance: `CTEST` (new `render_graph`, no GPU needed for the compile tests); `VAL` on the test scene. **Day one.**

**B5: Mesh cache (M).** `platform/scene/mesh_cache.{h,cpp}`: decode model packets (the VIF state, lane mask and
renderer entry of `renderers3d.cpp`) into object-space positions, texture coordinates, colours or normals, strip
and ADC flags; weld the items of a Cmd; key `MeshKey`; decode on a worker thread (std::thread pool until C7).
Before A2 lands it groups consecutive jobs with the same matrix. Depends: none. Acceptance: `CTEST` (new
`mesh_cache`, 77 without a capture): for every batch of the race and event captures, cache vertices times
`ModelJob::matrix` equal the CPU renderers' output (`renderers3d.cpp`) within 1/16 pixel; triangle counts equal;
decode time and memory reported. **Day one.**

**B3: Display pipeline on the graph (M-L).** `pipeline.cpp` stages as `DisplayFeature` passes
(`platform/render/display_feature.cpp`), CONTRACTS.md b.9. Depends: B1. Acceptance: `PRESETS`; `VAL`; `PERF(GPU
+-3 %)`; transient memory with aliasing reported, lower than today's.

**B4: GS back end on the graph (L).** `gsn_scene.cpp`: per list, uploads, 3D batch dispatch and GS segments split
at Light commands and markers as kept passes with their own barriers (GS targets imported from B8, CONTRACTS b.6),
the scene stage as `SceneStageFeature` passes; timestamps move into the graph. Depends: B3. Acceptance: `PRESETS`;
`VAL`; `PERF(GPU and render thread +-3 %)`.

**B2b: PipelineCache class (M).** `platform/render/pipeline_cache.{h,cpp}`: description map, `request()` on
workers, key list `pipeline_cache.keys`, `prewarm()`, save every 64 new pipelines. Depends: B2, C7. Acceptance:
`PRESETS`; with an empty cache, a full run then a second run: zero pipelines created on the render thread in the
second run after prewarm; hitch log shows no pipeline stage.

**B6: Material table (S).** `platform/scene/material_table.{h,cpp}`: `MaterialKey` from each job and its draw
state, ids per frame. Depends: B5, A10. Acceptance: `CTEST`; materials per race frame reported (expect a few
hundred); same key for the same object across frames.

**B7: Parity view renderer (L).** `platform/render/modern/parity_view.cpp`, `shaders/modern_parity.*`: draws a
view's instances from the mesh cache with GT4's lighting per vertex (the bodies of `gsn_models.comp`), at 1x, with
the ordered forward pass RISKS R3 decides for coplanar and blended draws. Depends: A2, A3, A10, B4, B5, B6, R3, R5.
Acceptance: `PARITY` on the race, flyover and event captures; `VAL`; `viewFallbacks` reasons listed.

**B8: Substitution at the Light command, hybrid (M).** `platform/render/modern/compositor.cpp`: colour and GS Z
into the GS target, the view's tagged draws skipped, untagged ones kept; read policy from R9; `render.scene =
hybrid`. Depends: B7, R9, E1. Acceptance: `ORIG`; hybrid on REF at 1x within `PARITY`; differences confined to the
scene rectangle (post effects and HUD unchanged where the scene is equal); `VAL`.

**B9: Mirror, env-map, reflection, showroom and photo views (M).** Depends: A6, B8. Acceptance: `PARITY` per view
role on its captures.

**B10: G-buffer, deferred lighting, sun, material classes (L).** `platform/render/modern/gbuffer.*`,
`lighting.*`; sun from the batches' light matrices (qwords 201..216) through `objectToWorld`; classes from lighting
bodies (`lighting.materials = pbr`); prelit course colour as baked lighting (through N2b's section normalisation
only if N2 reopens). Depends: B8, R8. Acceptance: `VAL`; no NaN or Inf (debug pass counts them); `PERF`
budget 4 ms GPU at 1440 x 1080 on the RX 7900 XTX; the owner's review against PCSX2 stills of the REF scenes.

**B11: Cascaded shadows and contact shadows (M).** Course casts onto cars only. Depends: B10. Acceptance: `VAL`;
`PERF(<= 1.5 ms)`; no shadow on the course where its baked shadow already is (review images).

**B12: GTAO from depth and normals (S).** Replaces the horizon AO on modern views. Depends: B10. Acceptance:
`VAL`; `PERF(<= 0.5 ms)`; `GT4_LIGHT_VIEW`-style debug view.

**B13: SSR, car cube probe, sector probes (M).** Depends: B10. Acceptance: `VAL`; `PERF(<= 1.5 ms)`; paint on the
showroom car reflects the probe (review).

**B14: Sky, aerial perspective, froxel fog (M-L).** Shares the froxel grid with F1. Depends: B10, F1. Acceptance:
`VAL`; `PERF`; GT4's painted skies kept where they have clouds (review).

**B15: Modern mode (M).** GT4's post effects skipped between the markers; pipeline bloom, exposure and tone map on
linear light; HDR10 metadata. Depends: A5a, B8. Acceptance: `ORIG`; `VAL`; HUD drawn after the tone map (until
E6, at render scale); HDR output measured on the HDR test pattern.

**B16: Free render scale and jitter for modern views; upscaler interface (M).** Depends: B8. Acceptance: `ORIG`
(integer scales kept on the GS path); `VAL`; render scale 67 % at 4K upscaled by FSR 2 without TAA ghosting on the
flyover (review), motion vectors exact (velocity debug view).

**B17: FSR 3.1, DLSS, XeSS (M).** Depends: B16, R13. Acceptance: each where available; `VAL`; `PERF`.

**B18: GPU particles from emitters (M).** Depends: A8, B22. Acceptance: `VAL`; smoke and sparks at the emitters
(review against the GS path's sprites); in the reactive mask.

**B19: Decals and tyre marks (M).** Depends: A9, D6, B10. Acceptance: `VAL`; marks laid at wheel contacts under slip.

**B20: Motion blur and DOF from exact velocities (S).** Depends: B10. Acceptance: `VAL`; velocity buffer equals the
analytic motion of static course instances under camera motion within 0.1 px.

**B21: Async compute on (S-M).** GTAO, cluster build, particle simulation on the compute queue. Depends: B12, F1,
B18. Acceptance: frames identical with `GT4_RG_ASYNC=0` and `1`; `PERF` gain reported.

**B22: Ordered and clustered forward pass (M).** Transparents and coplanar layers in GT4's order, lit by the
clustered lights. Depends: B7 (its first form), F1. Acceptance: `PARITY` kept; `VAL`.

**B23: Ray-traced reflections on cars (L, optional).** BLAS per mesh, TLAS per frame. Depends: B13. Acceptance:
`VAL`; off without ray query; `PERF` budget.

### Lane C: platform, ARM64, tooling

**C1: Capture, diff and profile harness (S-M).** `platform/sdl/main.cpp`: `--capture-frames LIST|every:N
--capture-dir DIR`, `--profile-json PATH`; `platform/sdl/renderer.cpp`: GPU timestamps to the JSON;
`tools/frame_diff.py` (identical %, within-4 %, max error, PSNR, heat-map PNG, `--baseline OLD NEW` over captures
and presets); `tools/perf_report.py`. Depends: B2 landed (same files). Acceptance: `CTEST` (new
`frame_diff_selftest` on synthetic PPMs: identical, one pixel off, tolerance edges); the same build twice gives
identical frames on the race capture; two presets differ. **Day one.**

**C2: Linux build fix (S).** `platform/gs/link_host.cpp:243-246` under `#ifdef _WIN32`, a POSIX branch
(`/proc/<pid>/stat` or 0). Depends: none. Acceptance: zig/clang compile test of the 29 host files 29/29 for
x86_64-linux and aarch64-linux (the audit had 26/29); Windows `CTEST`. **Day one.**

**C3: Build-machine glslang (S).** `cmake/Host.cmake:26-31`: `GT4_GLSLANG_EXECUTABLE` (found in the Vulkan SDK or
given) when cross-compiling, the built one otherwise. Depends: none. Acceptance: native build's SPIR-V byte
identical; a Windows ARM64 cross configure uses the host tool. **Day one.**

**C4: Architecture in scripts (S).** `tools/build_windows.cmd:8, 10` (`-arch`), `tools/package_game.py:28, 264,
300-301` (arm64 package name and CRT). Depends: C3. Acceptance (the coordinator, 2026-11-08: no ARM64 toolset on the
owner's machine): locally, the x64 package layout and the host build unchanged, and the arm64 path checked as far
as configure (the missing ARM64 component and CRT reported by name); the arm64 package is a CI acceptance, built by
the CI job once it is set up (Windows ARM64 is CI-build only).

**C5: CI and FP flags (M).** `-ffp-contract=off` (clang), MSVC ARM64 `/fp:precise` codegen check; CI for Windows
x64 (build, ctest, replay smoke), Linux x64 (build, ctest), Linux aarch64 (cross build; the replay smoke runs on the
Odin 3 by C6) and Windows ARM64 (cross build only: no device). Depends: C2, C3, C4, R4. Acceptance: all four green
on a80cfca + these tickets; CPU renderers' output identical x64 vs aarch64 on the CPU-only tests.

**C6: aarch64 Linux runs on the AYN Odin 3 (M).** The aarch64 host on Armada Linux (Adreno 830, Mesa Turnip),
under gamescope, with the x86-32 game under FEX. Windows ARM64 is not run (no device). Depends: C5, R4, C9 (live
link with the game under Wine). Acceptance: race capture replay `ORIG` identical to x64; a live race session runs 5
minutes at the Odin settings (1280 x 800); timings reported.

**C7: Host job system (M).** `platform/core/jobs.{h,cpp}`: work-stealing pool, fibre-free. Depends: none.
Acceptance: `CTEST` (stress test, TSan build on Linux); used by B5 and B2b with `PRESETS` unchanged.

**C9: POSIX game link (M).** Shared memory between a Linux host and the game under Wine. Depends: C2.
Acceptance: live session on Linux x64 under Wine reaches a race.

**C10: Reference captures and the PCSX2 procedure (S).** `docs/modern/CAPTURES.md`: how to record each REF
capture (`GT4_HOST_RECORD=PATH,FROM,COUNT`, with `GT4_HOST_RECORD_SCENE=1` once A3 lands; from game start so model
bytes are in, R7), how to take PCSX2 software-renderer stills at the same scene points (menus, showroom, paused
race, Photo Travel), where they live (`build/caps`, `build/refs`, never committed). Depends: none (scene re-records
after A3). Acceptance: REF.txt complete on the main dev machine; the procedure followed once by a second agent.

### Lane D: systems

**D1: Input action map (M).** `platform/input/input_map.{h,cpp}` replaces `readPad` (`main.cpp:394-443`); both
ports sent (`main.cpp:907`); `scePad2GetState` reports unplugged ports (`iop.cpp:509`); analogue throttle and brake
outside `original`. Depends: C1 landed (main.cpp order). Acceptance: `CTEST` (new `input_map`: the `original`
mapping gives the same 18 bytes as today's `readPad` for every input); session test with the virtual pad; the game
sees pad 2 in the split-screen menu. **Day one** (in `platform/input/`; the `main.cpp` change last).

**D3: Saves (S-M).** `runtime/memcard.cpp:198, 206-212, 235-239`: write to `.tmp`, flush, rename at close;
session backups in `saves/.backup/<time>/`, N kept; slot 2; export; displaced imports out of the listing pattern
(`save_import.cpp:282`). Depends: none. Acceptance: `CTEST` (`save_import` and a new `memcard` test: a write cut
halfway leaves the old save whole; backups rotate); a session saves and loads in GT4. **Day one.**

**D4: Narration (S).** `runtime/iop.cpp:253-257` (`voic()` plays the Speex PCM). Depends: none. Acceptance: a
session's `audio.wav` has the narration where the game speaks; no audio drop in the host summary. **Day one.**
**Not applicable to SCUS-97328** (2026-11-07): `narration.inf` sets the enable flag to 0 (`D_0084688C`), so the
game never opens `VOIC`. `BGM__narrationEnable()` returns `D_0084688C`, word `+0xC` of the 16-byte header that
`func_00460580` loads at boot from `/narration/<lang>/narration.inf` into `D_00846880`. Every narration path checks it
first (`RaceFreeRun::setDescriptionVoicePlay`, `::soundToggle`, `::initialize`, and the BGM narration play and pause).
A live boot on an empty save showed the game's file loader writing the 16 bytes once at start-up (`GT4_WATCH_WRITE`),
and nothing writing them after. A guest-RAM snapshot at VBlank 2400 (`GT4_SNAPSHOT`) read `NARR`, version 1, `0`,
`0`. `voic()` stays the stub; the code for a disc with narration is parked as a patch in lane D's clone and is not
on main.

**D2: Rumble (S).** `iop.cpp:572-576` profile and motor bytes, `GT4_REC_RUMBLE` in `protocol.h`, SDL rumble.
Depends: A1 (protocol.h). Acceptance: session test reads the rumble record values; a pad rumbles in a race (manual).

**D5: Audio mixer on the host (L).** SPU2 image and RAM records (`stream.cpp:766-768`), `platform/audio/`,
ENVX/ENDX reply path. Depends: A10 (stream.cpp), R16. Acceptance: the host mix within -60 dB of the game-side mix
on a session's audio over 60 s (`original` interpolation); no voice frees early (GT4's voice counts in its log).

**D6: Vehicle state discovery (L).** RISKS R14, then the fields of `scene::Vehicle`. Depends: R14. Acceptance: per
wheel load and slip plotted over a lap, consistent with the car's motion; documented addresses.

**D7: Wheels through LGDEV (L).** `iop.cpp:278-290` DFP emulation to `SDL_Haptic`. Depends: R15. Acceptance: a
wheel works in GT4's menus and races with its force feedback.

**D8: Physics force feedback (M).** Depends: D6, D7. **D9: Spatial audio (L).** Depends: D5, A7, A9.
**D10: Loading (M).** Measure 004B0E28 and 0x00104E30, then mapped reads. **D11: Latency (S-M).**
Acceptance for each as the audit's section 4 states, with session tests.

**Input as in current racing games (D12-D16; the owner, 2026-11-08).** The goal is the control quality of GT7, Forza
and ACC on top of the PS2 mapping, never instead of it. The constraints:
- Each feature is an `input.*` setting (`settings_input.cpp`). The `original` preset keeps today's behaviour byte for
  byte (`tests/input_map.cpp`). The other presets take the defaults each ticket gives.
- Everything happens on the host, before GT4 sees the pad. GT4's controller layer has no dead zone and a linear
  curve (`spikes/D1-bindings.md`), but the car's steering does not turn the wheels until the steering byte is
  past about 160, a quarter of a side's travel (lane D measured it at a standstill, 2026-11-08). So the host
  shapes its response knowing what lies after it, and compensates that band outside `original` (D12).
- GT4's functions stay assigned in GT4's own controller screen (D1's decision). The host maps devices to the PS2
  pad; it never moves GT4's functions.
- Every per-frame filter is counted in D11's latency budget.
In order of value:

**D12: Steering and pedal response (S-M).** `platform/input/response.{h,cpp}`. Today the sticks cut at 8 % and
don't rescale (`kStickDeadZone`, `input_map.cpp:62`), so steering jumps from 0 to 8 % of lock.
- `input.response = original | modern`. `original` (the `original` preset) bypasses all of this: today's map, the
  8 % cut with no rescale and nothing else, whatever the keys below say. `modern` (every other preset) turns the
  binding's own 8 % cut off, so the response works on the summed value (keys summed with the stick pass through it
  too).
- **A stateful stage after `apply()`**, which stays stateless: `input::Response::step(PadInput&, const Snapshot&,
  float dt)`, one per pad, held in `main.cpp`. It does smoothing and the keyboard ramp, which need state and the
  frame's dt. The shaping (dead zones, linearity, sensitivity, saturation) is pure functions it calls, which the
  CTEST covers.
- Steering (left stick X), per side. Its dead zones are **per axis, on X only**, which is right for steering: they
  must not be made radial.
  - `input.steer.deadzone` (inner, 0-30 %, default 0 % until the band compensation lands, the coordinator's
    interim fix, so `modern` is never worse than `original` and GT4's band swallows stick noise; 5 % with it):
    below it 0; above it rescaled, so the output starts at 0;
  - `input.steer.outer_deadzone` (0-30 %, default 2 %): full output that far before the end of travel;
  - `input.steer.linearity` (exponent 1.0-3.0, default 1.0): finer control near the centre above 1;
  - `input.steer.sensitivity` (0.5-2.0, default 1.0): a gain before the limit, so above 1 full lock comes early;
  - `input.steer.saturation` (50-100 %, default 100 %): the most lock the stick gives;
  - `input.steer.smoothing` (0-100 ms, default 0): a one-pole filter.
- Pedals (each trigger's travel, and the right stick's halves when GT4's analog config uses them): inner and outer
  dead zone, linearity and smoothing, the same way (`input.pedal.*`). Then GT4's own curve, and
  `input.trigger_travel` if it is `full`, apply on top as today.
- **GT4's steering band.** Between the steer command and the wheels, GT4 keeps the wheels straight up to a
  steering byte of about 160 and turns them roughly linearly from about 165 to 0.68-0.69 rad at 255 (D12's
  measurement, at a standstill, `original` and `modern` alike). With the host's dead zone on top, `modern`'s
  defaults left the first third of the stick doing nothing. `modern` compensates it: the shaped output o (0..1)
  becomes the byte `128 + (g + (1 - g) * o) * 127` on the right and `128 - (g + (1 - g) * o) * 128` on the left
  (`padAxis`'s scales, so both ends reach 255 and 0), so the first step past the host's dead zone moves the wheels.
  The band is measured on both sides, with g per side if GT4's edge is not symmetric. `original` keeps the band,
  as the PS2 had it.
  - **Where the band is** (lane D, 2026-11-08). It is in the controller layer's analogue path, between the linear
    steer table (`D_00622F58`) and the steer command, not in the car. The command, copied each tick to
    `D + 0x454`, is exactly 0 for bytes 90-165 and rises linearly past them, about 45.6 a byte, to +-4096 at the
    ends. Both edges sit at the same table value, about 29 % of the channel's range, in a GT-mode run and an Arcade
    run with different cars. The rack (`func_003485D0`: spring, damper, a rate limit that varies with
    `D + 0x53C`) and the target (`func_00348850`: command x lock / 4096, plus two assists only with an aid flag at
    speed) have no band. The D-pad writes its command directly and has none.
  - So g is one constant: `input.steer.game_band`, percent of a side, 0-40, default 29 in `modern`; `original`
    never applies it. On the right, 128 + 0.29 x 127 = 165, the last zero byte (no overshoot). On the left,
    128 - 0.29 x 128 = 91, one or two bytes of residual band (90, maybe 89; the allowed undershoot).
  - Still to confirm before the default is final: the band is the same at speed (expected, since it comes before
    the car), and whether a Driving Force Pro's input passes through the same analogue path (for D15's `dfp`
    mode).
  - It applies to everything that goes through the response: the stick, the keyboard ramp, and in D15 a wheel in
    `native` mode.
- Keyboard steering: a digital key ramps the axis at `input.steer.key_rate` (full lock in 0-500 ms, default 150 ms;
  `original` 0, today's step) and returns at twice that rate.
- **Speed-sensitive steering**, `input.steer.speed_reduction` (0-80 % less lock at 200 km/h, default 0), comes
  after D6 (speed, `D + 0x5C4`). It is set only after measuring how GT4 already reduces lock with speed (the
  steering angle at the wheels, `D + 0x468`, at full stick from 0 to 200 km/h), so the host never doubles it.
- Depends: D1. Acceptance:
  - `CTEST` (a new `input_response` test): the output is continuous at the inner dead zone's edge (0 at the edge,
    one steering byte step just above it), monotonic, and full travel gives full lock at saturation 100 %; the
    `original` map's bytes are unchanged for every input; smoothing's step response has the configured time
    constant.
  - A session test with the virtual pad sweeps the stick over 256 steps. With the defaults, every steering byte from
    the compensated start to full lock is reached, with no gap over one byte. With `original`, the jump at 8 % is
    still there.
  - Live: the steering angle at the wheels against the stick, at a standstill and at 100 km/h, linearity 1 and 2,
    plotted (D6's field). With `modern`'s defaults the wheels move from the first stick step past the host's dead
    zone, with no step larger than one byte's worth of angle. The plot also records GT4's own reduction with
    speed, for the speed-sensitive part.

**D13: Devices: profiles, hot-plug, keyboard bindings (M-L).** `platform/input/devices.{h,cpp}`.
- **Profiles.** Each device keyed by its SDL GUID and name: its bindings to the PS2 pad, D12's response, D14's
  haptics and D16's gyro settings, kept in `input_profiles.ini` beside `settings.ini`. A new device gets the
  defaults for its type (gamepad, keyboard, wheel).
- **Hot-plug.** `SDL_EVENT_GAMEPAD_ADDED` / `REMOVED`. A device keeps its port across a reconnect (by GUID, then
  by order). A port whose device leaves reads unplugged (`scePad2GetState`, D1), as on the PS2.
- **Keyboard bindings** are rebindable keys for the PS2 pad's inputs.
- **Remapping** maps a device's inputs to the PS2 pad's. It never moves GT4's functions, so the default stays the
  PS2 places and GT4's controller screen still assigns accelerate, brake and the rest. Refused, since the menus
  need them: a remap that leaves any menu input unbound (Cross, Circle, Triangle (back in the Options screens),
  Start, the D-pad), and an analogue input on Cross or Circle (D1's `input.pedals` trap: a trigger pull confirms).
  An analogue input on another button GT4 reads in menus gets a warning.
- Depends: D1. Acceptance:
  - `CTEST`: profiles round-trip; each refused remap (a menu input unbound, an analogue input on Cross or Circle) is
    refused; the `original` map is unchanged with no profile file.
  - A session test plugs and unplugs two virtual pads during a race and a menu: ports stay, unplugged reads as on
    the PS2, and the game goes on.
  - Live: one Xbox pad, one DualSense and the keyboard, each with its own profile after a restart.

**D14: Gamepad haptics from the physics (M).** `platform/input/haptics.{h,cpp}`, from D6's per-wheel fields
(R14: slip ratio `W + 0x38`, slip angle `W + 0x34`, load `W + 0x48`, surface `W + 0x89` (a surface id: 2 seen at
the track edge; the id table is open), stroke speed `W + 0x98`), read each frame through the vehicle record (A9).
The kerb signal waits for the id table (or comes from stroke speed alone until then).
- **Motors:** the low-frequency motor from front slip past the grip peak (understeer), lock-up and wheelspin; the
  high-frequency motor from kerbs and road texture (surface and stroke speed).
- **Impulse triggers** (Xbox, `SDL_RumbleGamepadTriggers`): brake lock-up on the left, wheelspin on the right.
  On Windows they come only through SDL's Windows.Gaming.Input or GameInput backends (not XInput), over USB or
  the Xbox Wireless Adapter, not Bluetooth. The call returns false otherwise, and the host checks it and shows
  "not supported on this connection" instead of assuming.
- **DualSense adaptive triggers** (`SDL_SendGamepadEffect`, through SDL's HIDAPI PS5 driver, USB and Bluetooth):
  brake resistance rising as the front wheels near lock-up, and a pulse on the throttle when the rear wheels spin.
  SDL defines no trigger-effect API: the host builds the output report's effects block itself (the layout SDL's
  `testcontroller` uses). Over Bluetooth it needs SDL's enhanced reports, which SDL3 turns on when effects or
  sensors are used.
- **HD haptics** only where SDL3 exposes them. If none, that's reported, not worked around.
- GT4's own rumble (D2) stays and mixes in (crashes), taking the larger value per motor.
- Settings: `input.haptics` (`original`: D2 only, the PS2 behaviour; `physics`), `input.haptics.strength` (0-100 %),
  and `input.trigger_effects` (`off`, `on`; default `on` outside `original`).
- Depends: D2, D6, A9, D13 (per-device). Acceptance:
  - `CTEST`: the mapping from recorded wheel fields to motor and trigger values, on a recorded lap's fields
    (one-frame steps, no output when stopped).
  - A session test reads the haptic records over a lap with a kerb, a lock-up and a spin, and each shows where
    D6's fields show it.
  - Live, manual: an Xbox pad (USB or the wireless adapter) and a DualSense (USB and Bluetooth), with Steam Input
    and DS4Windows closed: either can hold the device and swallow the effects (checked and said in the result).
  - D11: the effect is at most one frame behind the physics frame it comes from.

**D15: Wheels beyond the Driving Force Pro (L).** Two paths, a setting per device (D13):
- `input.wheel.mode`:
  - `dfp`: GT4 sees a Driving Force Pro through D7's LGDEV emulation and drives its own effects (the PS2
    behaviour, and the default for a wheel in `original`);
  - `native`: the wheel's axes go through D12 as a steering axis with its own rotation (`input.wheel.rotation`,
    degrees for full lock), with separate pedal axes, and the force comes from D8's physics.
- **Force feedback** through `SDL_Haptic`: constant force from D8's self-aligning torque (R14: built from the front
  wheels' lateral forces, `func_00353EE8`), plus damper and spring for low speed and a kerb effect from D14's
  signal.
- `input.wheel.ffb_strength` (0-100 %) and `input.wheel.ffb_min` (small-force boost, 0-20 %).
- The supported list is what SDL3 exposes: Logitech G series, Fanatec and Moza through DirectInput on Windows,
  checked on the devices the owner has.
- Depends: D7, D8, D12, D13, R15. Acceptance:
  - `CTEST` (effect translation, on recorded fields);
  - session test (the effect stream over a lap);
  - live on each available wheel: centring, the loss of weight at understeer, kerbs; `dfp` behaves as D7.

**D16: Gyro steering (S-M).** `input.gyro = off | steer` (default `off` in every preset). The controller's roll,
fused from gyro and accelerometer (`SDL_SetGamepadSensorEnabled`), steers. Full lock at `input.gyro.range` (30-180
degrees, default 90), recentred by a button chosen in D13's profile. It adds to the stick before D12's response.
Depends: D12, D13. Acceptance:
- `CTEST` (fusion on synthetic sensor data: half the range gives half lock; drift under 1 degree a minute at rest
  after the bias calibration);
- live, manual: a DualSense, a DualShock 4 or a Switch Pro.

### Lane E: UI, and the previous agents' unfinished work

**E2: Menu movie background as one texture updated in place (S).** Land the uncommitted work in
`C:/Users/julie/gt4-native-sprite`: `GsFrontend::textureFor` (`gs_frontend.cpp`) decodes a stream (new content at
the same address and size on three or more consecutive frames) into the previous content's texture id instead of
a new 1024-wide texture each frame, and decodes only the buffer's width of a 640-wide frame declared 1024 wide;
`stats.streamed` and its line in `GsHost::summary` (`gs_host.cpp:1048`). Check in review: the back end updates the
existing image for a same-id upload and creates none (`GsnScene::Counters::textures` flat), and the "part" decode
does not change sampled pixels. Depends: none. Acceptance: `PRESETS` on the REF captures **and** on the menu
movie capture (C10; until then a fresh `GT4_HOST_RECORD` of the main menu); over 600 menu frames, images created
per frame 0 after the first, texture memory flat, `streamed` > 0. **Day one.**

**E3: Display clamp for the faint text row at the bottom of the menus (S).** Land the uncommitted
`gsn_display.frag` change in `C:/Users/julie/gt4-native-sprite`: samples kept half a texel inside the displayed
rectangle, so the bilinear filter does not pull in the row GT4's menus park outgoing text in. Depends: none.
Acceptance: `ORIG` (at 1x the clamp never moves a sample); at `high` on the menu capture the bottom row no longer
shows text, and the only differing pixels are in the outer row and column of the picture; PCSX2 still of the same
menu for comparison. **Day one.**

**E4: Track-menu band and stutter (S).** The owner's words: "browsing tracks in the various menus like original
tracks or city tracks is stuttery, the black band at the bottom is moving a bit up and down vertically and it's
overall not smooth". The previous agent (4200b0a6) found three candidates and the owner has not said which one they
mean: (1) the picture edge, steady except 1 row in 11 of 360 frames; (2) the Course length / Elevation line
sliding up over about 18 frames with field-blend doubling; (3) the faint text row at rest (E3). No code for it was
found. Reproduce on the Odin settings at 1280 x 800 (the original-tracks and city-tracks menus), record a capture,
and check all three: per frame the black band's top row (1), the text line's position and the field blend
(`display.race_frames`, `gsn_display.frag`'s `fieldBlend`) (2), the bottom row after E3 (3); frame pacing of the
menu (presented frame times in `--profile-json`) for the stutter. Compare with PCSX2 software stills. Fix in the
front end or back end. Depends: E3 (for 3), C1 helps (frame times). Acceptance: on the track-menu capture at
1280 x 800, the band's top row constant over 360 frames, the text line moves without doubled copies, no text row
at rest, presented frame times within 1 ms of 16.7 ms; `ORIG` unless `original` is also wrong against PCSX2 (then
`original` must match PCSX2 after the fix); the other REF captures `PRESETS`. **Day one.**

**E1: Settings per module, and `render.scene` (S).** `platform/sdl/settings.{h,cpp}`: the option table split
into per-module files; the key `render.scene` (only `original` accepted until B8); presets as ARCHITECTURE.md
section 7. Depends: none. Acceptance: `CTEST` (`host_settings`); a settings file round-trips identically; every
preset's values unchanged. **Day one.**

**E5: Host UI font: SDF, Unicode (M).** `overlay.cpp:11-80`. Depends: none. Acceptance: menus legible at 720p to
4K (review captures); the stale TAA / FSR 2 text (`settings_menu.cpp:93-101`) fixed.

**E6: GT4's 2D in an output-resolution layer (M).** From `kMarkHudBegin` (A5a) the draws go into the 2D layer
after upscaling and tone mapping, at paper white in HDR. Depends: A5a, B4. Acceptance: `ORIG`; HUD sharp at 4K with
render scale 50 %; HDR HUD at paper white (measured).

**E7: SDF text for GT4's font pages (M).** Depends: E6.

**E8a: Input pages (M).** `settings_menu.*`: the devices list (D13), each device's bindings, response (D12) with a
live plot of stick against output, haptics (D14), gyro (D16) and wheel (D15) pages. Each page is shown when its
ticket has landed. Saved per device in D13's profile. It shows GT4's own controller screen as where accelerate and
brake are set (D1). Depends: E5, D12, D13. Acceptance: every D12 setting can be set and reset from the pad alone;
a profile set in the menu matches the file; the menu works with the keyboard only.

**E8b: Audio page (S).** Depends: D5.
**E9: HUD scale, safe area, HUD off, photo export (M).** Depends: E6.

### Lane F: night lighting, and the Route 5 night artefacts

**F0: Light list in code (S).** `include/gt4/render/light_list.h`, `platform/render/shaders/light_list.glsl`
(CONTRACTS.md c.1), with the falloff and cone functions. Depends: CONTRACTS.md reviewed. Acceptance: `CTEST` (a
layout test: the GLSL struct's std430 offsets, read back from a compute shader, equal the C++ ones). **Day one.**

**F1: Cluster build (M).** `platform/render/modern/lights_cluster.cpp`, `shaders/lights_cluster.comp`: froxel
assignment from the light list; first standalone with a CPU reference, then a graph feature after B1. Depends: F0
(B1 for the graph form). Acceptance: `CTEST` (GPU result equals the CPU reference for random light sets; 77 without
a GPU); 256 lights at 1440 x 1080 under 0.2 ms; `VAL`. **Day one.**

**N0: Route 5 night captures (S).** Three captures on S.S. Route 5 at night with `GT4_HOST_RECORD`, from game
start (R7): **sections** (driving across at least three section boundaries where the step shows), **lamps** (the
lamps that throw bands), **warning** (the blinking yellow warning lights in view, several blink cycles). 300-600
frames each. Re-recorded with `GT4_HOST_RECORD_SCENE=1` after A3. Notes beside them: the frames of each boundary
crossing and blink. Depends: none. Acceptance: the three artefacts visible in replays of the captures on the GS
path at `original` and `high`, and in PCSX2 software stills at the same places. **Day one.**

**N1: Night artefact metrics (S).** `tools/frame_metrics.py` over `--capture-frames every:1` output:
- *step*: per frame the mean log-luminance (Rec.709 Y) of the scene rectangle with the HUD masked; a step is a
  frame-to-frame change above 4 % that is not a camera cut;
- *bands*: in the road region, the ratio of a band's luminance to the road beside it, at the rectangles where the
  band draws write (from R11);
- *tint*: per frame the mean Oklab a and b of the picture outside a disc of 8 % of the height around each warning
  lamp; the tint is the amplitude of the part of that series in step with the lamp's blink.
Baseline numbers on the GS path go in `docs/modern/spikes/R5-night.md`. Depends: N0 (C1 for the capture option;
before it, a loop of `--frames N --capture`). Acceptance: `CTEST` (`frame_metrics_selftest` on synthetic frames
with a known step, band and tint); on N0 the GS path's numbers show all three artefacts. **Day one.**

**N2: Section step (parked; reopens on evidence).** R10 found no one-frame step on S.S. Route 5 Reverse
(`spikes/R10.md`; coordinator's decision for the owner, 2026-11-08). The four brightness changes at fixed track
positions build over 8-20 frames (the largest one-frame change of the scene is 3.0-4.6 %). No grading, fog colour or
batch lighting value switches there at 507 and 2514. About two thirds of the largest (replay 2514) is the lit-road
layer the car drives out of (draws 305-308): probably the lamps' light baked into the course, which N3 takes. So
N2a (blending grading and fog parameters) is dropped and N2 is not designed.
- **Reopening.** Lane F's live sessions on the Normal layout and one more night city course (for R11 and R12, up to
  three, recorded by lane F) are checked for a one-frame step. That is: N1 (`frame_metrics.py step`) reports a level
  shift of at least 5 %, one frame carries at least 70 % of it, it is not a camera cut (structure correlation above
  0.5), and it comes at the same track position on a second lap. Reverse's four changes fail the 70 % test (3.0-4.6 %
  of 9.8-31 %). N2's 1 % bar is its acceptance after a fix, not this detection test. If one shows it, N2 reopens
  with that capture: its source is found by RISKS R10's method, then this ticket is designed from what
  it finds. Otherwise N2 stays parked.
- Candidates kept for a reopening: *b, prelit colour* (a per-section gain from the course instances' prelit
  luminance, normalised to the neighbouring sections and blended near the boundary, applied where prelit colour
  enters as baked lighting in B10; needs A11b), *c, lighting constants* (the per-section light matrices blended over
  distance by each instance's position; needs A2).
- Acceptance when it reopens: `R5` on that capture, in `hybrid` and `modern`: no frame-to-frame step above 1 % at the
  crossing (the GS path's step, measured by N1, for comparison), no new step above 4 % anywhere except camera cuts;
  `ORIG` unchanged (the step stays in `original`).

**N3: Lamp bands suppressed (S-M).** The draws R11 identified are skipped in `hybrid` and `modern` between
`kMarkLampBands*` (A5b), and the lamps light the road through their `Light` records and the clustered lights (F2 in
`hybrid`, F3 in `modern`). R10 and R11 found two things on the road (`spikes/R10.md`, `spikes/R11.md`; draw
numbers are those of replay 2514, and the draws are found by their source, as R11 does):
- **the lit-road layer** (draws 305-308): world-fixed and without a lighting chain (measured), so probably the lamps'
  light baked into the course (PCSX2 to confirm). It is skipped exactly when the renderer draws the lamps' own
  light on the road (F2 in `hybrid`, F3 in `modern`), so the road gets the lamps' light once: never twice, and
  never dark. With F2 off (a `hybrid` preset without the night relight), the layer stays. `original` keeps it.
- **the light-image glow** (draws 325-326: a blurred image of the lights in target 71, from target 37, spread as 60
  sprites weighted by the scene's alpha): R11's first band candidate, skipped with the bands if R11 confirms it.
Depends: R11, A5b, A7, B4, F2. Acceptance `R5` on the lamps capture: band ratio at most 1.3 in every frame (the
GS path's value for comparison); suppression counter > 0 on the frames where the GS path draws them; `ORIG`
unchanged.

**N4: Warning-light tint suppressed (S-M).** The flare (and, in `modern`, glare) draws R12 identified are skipped
between their markers (A5b); each light keeps its own bloom (pipeline bloom on linear light, F4). Depends: R12,
A5b, B4, F4 for `modern`. Acceptance `R5` on the warning capture: blink-synchronous tint amplitude below 0.002 in
Oklab b and a, outside the lamp discs (the GS path's amplitude for comparison); the lamps still visibly blink and
bloom; `ORIG` unchanged.

**F2: Night relight on the GS path (M).** At a Light command in `hybrid`: lights gathered from the view's `Light`
records into the light list, clusters over the scene rectangle, view-space positions and normals from GT4's Z,
the lights added in the scene stage with the albedo estimate of CONTRACTS.md c.3. While F2 lights the road, N3
skips the lit-road layer (draws 305-308), so the lamps' light reaches the road once. Until N3 lands,
`lighting.night = enhanced` is set by `--set` only, in no preset, so a release never lights the road twice. Three
other lanes' files change, each reviewed by its lane: `gsn_light.comp` (lane B's scene-stage shader) takes the
added light; `sceneRecordsWanted` (`settings_scene.cpp`, lane E) is also true with `lighting.night = enhanced`,
since the game sends no lights without records; and `cmake/Host.cmake` (lane C) links the light sources and
compiles `lights_cluster.comp` for the host. Depends: A7, F1, B4.
Acceptance: `ORIG`; `VAL`; `PERF(<= 1 ms)` at 1440 x 1080; on a night capture lamps light the road and cars near
them (review against PCSX2 stills: the scene otherwise unchanged); on the lamps capture with the car moving, N1
shows no new step above 4 % (N2's bar) from the added light.

**F3: Clustered lighting in the deferred and forward passes (M).** Depends: F1, B10, B22. Acceptance: `VAL`;
`PERF`; the same lights as F2 in `modern`, with G-buffer normals.

**F4: Bloom and eye adaptation on linear light (S).** The pipeline's bloom and histogram exposure on the modern
scene. Depends: B15. Acceptance: `VAL`; exposure follows tunnels and night sections without a step (N1 step metric
on the sections capture); the warning capture's tint stays below N4's bar.

**F5: Wet roads (M).** G-buffer modifier, puddle masks, spray from emitters. Depends: B10, B13, B18. Acceptance:
`VAL`; wet variants only; review against the wet capture.

---

### Cross-lane: PS2 mechanisms removed (P1-P6, PS2_REMOVAL.md)

The owner's direction (2026-11-08): no PS2 concept beyond what the game logic needs. [PS2_REMOVAL.md](PS2_REMOVAL.md)
lists what is kept and why. These tickets remove or lift the rest. Each is checked with `ORIG` and `PRESETS`, since
`original` keeps the PS2 picture.

**P1: Retire the GIF and VIF records (M, lane C; lane B reviews the stream changes).** The live game sends none
(`GT4_REC_GIF`, `VIF1`, `VIF0`, and kind 4, the old GS_PRIV; protocol.h). Lane C's scan of `build/caps`: only
`race.cap` and `event.cap` have them, and CAPTURES.md already lists both as replaced by `race_boot.cap`. So no
capture is converted: those two leave the list.
- **What goes:** the record kinds (their numbers stay reserved, so an old capture is recognised and refused), their
  cases in `GraphicsStream::record`, `vif1.cpp`'s stream and DIRECT path, `CountingSink`, MSKPATH3 holding and the
  PATH3 counters.
- **What stays:** the GS back end's packet input (`gs.gif`, `gs_host`'s `Op::Gif`, the front end's `gif()`). The
  3D renderers pass the game's own packets on as PATH1 (`RendererSink::packet`), and state restore replays the
  saved GS registers as one A+D packet. `vif1.cpp`'s model-packet decoder stays too.
- **The STATE record:** `race7.cap`, the `ORIG` reference, starts with one, and its version 2 / 3 layout carries
  vif1's program, batch buffers, renderer constants and the VIF registers. The reader keeps reading that layout
  (it skips the VIF registers).
- **Tests:** the ones that write the old records are rewritten onto GFX records, or lose the stream cases they
  cover: `gs_host` (`tests/gs_host.cpp:46-48`, the synthetic capture `host_replay` replays, which keeps a fixture),
  `ps2gfx` (`tests/ps2gfx.cpp:404-498`) and `gs_backend` (`tests/gs_backend.cpp:178`, reference builds only).
- Depends: none. Acceptance: `CTEST`; `ORIG` (`race7.cap` identical before and after); `PRESETS`; every listed
  capture replays; `race.cap` (kept out of the repo, as always) is refused with a message, not misdrawn.

**P2: Native targets and palettes (L in total, lane B; three pieces, each S-M with its own identity-gated
commits).** What is left of docs/NATIVE_V2.md (step 1's textures are done):
- **P2a: targets keyed by surface**, instead of GS page ownership. Today a record names a surface only by its
  FRAME / ZBUF address, width and format, so P2a's key is derived on the host from those. P3b later carries the same
  key as an explicit handle; P2a defines surface identity, P3b does not define it again. P2a lands before B8's
  import code, or B8 imports by P2a's key, so B8's imports are not written twice.
- **P2b: native palettes.** CLUTs come from the set's content, and the game's palette writes become palette data,
  so set pages stop being materialised for CLUT loads.
- **P2c: reinterpretations as explicit copies** from the named source target, on the GPU (as `gsn_scene`'s
  reinterpret pass already is). The fallback (004A9470, never seen) stays, and is counted.
- A GS view of memory stays for every reader until R9 decides it: P2 needs only R9's list of readers, not its
  decisions.
- Files: P2 owns the target, page and palette parts of `gs_frontend.*` and `gs_memory.*`, lane A reviewing
  (section 3).
- Depends: B4; R9's reader list. Order: P2a, then B8's imports, then P2b and P2c.
- Acceptance, per piece:
  - `ORIG` (`original` runs the same GS front end, so P2 changes its path too); `PRESETS` identical; `VAL`;
  - `PERF`: the GPU and the front-end thread no worse;
  - a new metric in `GT4_GSN=stats`: pages materialised and bytes written into `gs_memory` per frame (uploads,
    CLUT loads, transfers), with the baseline measured at the parent commit on `race7.cap` and `race_boot.cap`
    (NATIVE_V2 has about 130 pages a frame). It must come out lower;
  - reinterpretation fallbacks counted: 0 on `race7`, `race_boot` and `menus_boot.cap` (the menus);
  - the R9 readers' bytes unchanged (`GT4_READ_HASH` on a live session to a race: CarIconMaker, the luminance
    probe).

**P3a: Block links as offsets (S-M, lane A).** The frame list's DMA-tag block links become plain offsets
(`runtime/frame.cpp`'s walk, pgl's packet). Depends: none. It goes with P1's format bump (both phase 1); P3b bumps
the format again later, with its own conversion.

**P3b: Draw state in renderer terms (L; lane A the runtime side, lane B the host side).** `include/gt4/gfx.h`
records carry blend, depth, texture and target handles instead of GS register images. The target handle is P2a's
surface key. Depends: P2a, A10.
- **The round trip.** pgl's API stays GS-shaped in places (the pglu calls, the mirror's own TEX0s, the effects'
  register values, 0049CC48). So the decode moves into `pgl.cpp`, and the GS back end re-encodes for the GS path,
  which `original` still uses. The renderer terms must carry the GS-only states: alpha test fail modes (FB_ONLY,
  ZB_ONLY, RGB_ONLY), destination alpha test (DATE / DATM), PABE, FBA, COLCLAMP, DTHE, TFX / TCC, fog, STQ against
  UV, AA1, CLAMP's region modes, and MMAG / MMIN / LCM / K. A state with no mapping is counted and kept raw, never
  approximated.
- `STREAM` no larger may need packing: a handle plus explicit fields can outgrow a 64-bit register image.

Acceptance for P3a and P3b:
- `CTEST`, including for P3b: decoding then encoding gives back every draw state of `race7`, `race_boot` and
  `menus_boot.cap` (the menus);
- `ORIG` and `PRESETS` identical;
- `STREAM` no larger, measured on the same route as today's baseline;
- `hook_lint` 0.
- **Captures.** The new records change the link format, so every capture CAPTURES.md lists is converted (old
  records to new) or refused with a message, as in P1. The converter is also the identity oracle: the converted
  capture on the new host against the old capture on the old host, identical. Each ticket states this before its
  code changes. P1 and P3a share one format bump (phase 1); P3b's is its own, later.

**P4: No preemptive frame delivery (S-M, lane A, which owns `runtime/kernel.cpp` for it).** Find the threads that
make no syscall for 50 ms (17 of 8,911 deliveries in a session to a race used the fallback), give each a safe point,
then delete the thread freeze, `GT4_FRAME_DELIVERY=preemptive` (`runtime/kernel.cpp:258-273`) and `GT4_WATCH`'s
guard that exists only for the freeze (`g_guard_frozen`, `g_frozen_in_runtime`). The risk: a thread that spins on a
flag a callback sets has no syscall at all, so deleting the freeze without a safe point in that exact loop deadlocks
it. Depends: none. Acceptance:
- `REL`; `ORIG`; `gt4_game --self-test` passes without the freeze.
- 0 preemptive deliveries before the code goes, over a GT mode session, a race, the menus, a replay, a licence
  test, the movies and loading screens, and Photo Travel (where decoder and loader loops live).
- The commit names each thread and loop found (PC, function).
- A diagnostic stays instead of the freeze: a delivery waiting more than 50 ms logs the running thread's PC once, so
  a missed loop shows instead of stalling silently.

**P5: Spike R17, guest memory beyond 32 MB (S, lane A).** RISKS.md R17: a debug runtime build, nothing to main.
Acceptance: the answer and its measurements in R17. If it finds a use, a ticket follows.

**P6: Spike R18, the sound driver without the SPU2 register image (M, lane D).** RISKS.md R18. Depends: D5.

## 5. Day one, per lane

| Lane | Day-one tickets | Notes |
|---|---|---|
| A | **A0** (spike R1), **A1** after the contract review | A10 follows A1 (and E2, same file) |
| B | **B2** (land the pipeline cache, first in `main.cpp`/`renderer.cpp`), **B1** (graph core), **B5** (mesh cache from `ModelJob` words) | B1 and B5 touch no shared file |
| C | **C1** (harness, after B2), **C2** (Linux fix), **C3** (glslang) | C10 can start too |
| D | **D3** (saves), **D4** (narration), **D1** (input map, in its own folder first) | D2 waits for A1 |
| E | **E2** (movie texture), **E3** (display clamp), **E4** (track-menu band), **E1** (settings split) | E2 must land before A10 starts on `gs_frontend.cpp` |
| F | **F0** (light list), **F1** (cluster build, standalone), **N0** (Route 5 captures), **N1** (metrics); spikes **R10-R12** after N0 | |

### Agent assignments

- **3 agents.** 1: A0, A1, A10, A2, A3 (then A5a, A11a; A11b only if N2 reopens). 2: B2, B1, B5, then B3, B4.
  3: E2, E3, E4, E1, C1, C2, C3, then N0, N1, R10-R12, F0, F1. D waits for a fourth agent.
- **4 agents.** As 3, plus 4: D3, D4, D1, then D2, D6.
- **5 agents.** As 4, with lane F (N0, N1, R10-R12, F0, F1, then F2, N2-N4) moved to agent 5, leaving agent 3 on
  E and C.

No two day-one tickets edit the same file except `main.cpp` / `renderer.cpp` (B2, then C1, then D1) and
`gs_frontend.cpp` (E2, then A10), which go in that order.

---

## 6. Phases and the critical path

| Phase | Tickets | Gate to leave it |
|---|---|---|
| 0. Foundations (weeks 1-3) | A0, A1, A10, A2, A3; B2, B1, B5, B3; C1-C4, C10; E1-E4; F0, F1, N0, N1; spikes R1-R7, R9-R12 | `SCENE` gates on race and event captures; B3 `PRESETS` |
| 1. Parity (weeks 3-8) | B4, B6, B7, A4, A5a, A6, A11a, P1, P2a (after B4, before B8's imports), P3a, P4, P5; D-track (D12, D13 first; P6 after D5) | `PARITY` on REF |
| 2. Hybrid ships (weeks 8-10) | A7 step 1, B8, B9, F2, N3, N4 (hybrid parts) | `render.scene = hybrid` in presets low-ultra; `R5` gates in hybrid |
| 3. Modern lighting (weeks 10-18) | B10-B15, F3, F4, A7 step 2, A8 (N2b/c and A11b only if N2 reopens) | `R5` gates in modern; owner review |
| 4. Resolution and effects | B16-B22, F5, E6-E9, P2b, P2c, P3b | per ticket |
| 5. Optional | B23, C6 on devices, research track | per ticket |

Critical path: A0 -> A1 -> A10 -> A2/A3 -> B7 (also needs B1 -> B3 -> B4 and B5 -> B6) -> B8 -> B10 -> B15.
Calendar estimates are guesses from the sizes: hybrid in about 10 weeks with lanes A, B and C in parallel.

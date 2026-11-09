# GT4-Native modern renderer and systems: target architecture

Status: design for review, not implemented. Written at `main` a80cfca (2026-11-07), from
`docs/MODERNISATION_AUDIT.md` (the audit, in the audit agent's clone) and a check of its key claims against the
code. Companion documents: [CONTRACTS.md](CONTRACTS.md) (the scene records, the render graph, the light list),
[PLAN.md](PLAN.md) (tickets by lane), [RISKS.md](RISKS.md) (spikes).

Conventions: `path:line` is at a80cfca. GT4 addresses are NTSC-U v1.01. **New** = code that does not exist,
**Changed** = existing code that gets edited, **Kept** = existing code that stays as it is.

---

## 1. What I checked in the audit, and what changes

| Audit claim | Checked how | Result |
|---|---|---|
| Model path share: 48.0 % of draws on the race capture, 93.5 % on the flyover | Rebuilt the host from my clone (Release, 25/25 ctest), replayed copies of `rec/race7.cap` and `refcap/race.cap` with `GT4_GSN=stats GT4_GSN_DRAWS=all`, counted the per-draw lines (model draws print untouched extents, `xy 1000000000,...`) | **Confirmed exactly**: 74,146 of 154,346 draws over 100 frames (48.0 %), 120,597 of 129,045 over 200 frames (93.5 %). (Draw counts only: `refcap/race.cap` has no display records and replays black, so it is no pixel reference; PLAN.md section 2.) The 98 % share inside the scene target needs the Light list, which the stats do not print; I checked the method, not the number. |
| `ModelJob::matrix` is the composite object-to-window matrix (`gfx.h:600`) | Read `renderers3d.cpp:1008`, `game_src/func_0049E548.cpp` | **Confirmed, with a detail the audit leaves out**: 0049E548 writes rows 0-2 of the composite times the Cmd's shape scale (`cmd->value`, `func_0049D328.cpp:24`), row 3 as is. 0049E480 also scales rows 0-2 by `s->scale` when byte +0xE is set. Any check that recomposes the matrix must apply both. |
| The game builds it at 0049E480 as A x B; [guess] B = object-to-world, A = world-to-screen | Read `func_0049E480.cpp`, `func_004A5348.cpp`, `func_004ABEC0.cpp`, `func_0049F7D8.cpp`, `func_003B8EA0.cpp`, `docs/NATIVE_ONLY.md:68-70` | **The guess is wrong; the real split is better.** pgl keeps three *matrix sets* (004A5348 makes set 0, 1 or 2 current; set k is saved at scratchpad 0xED0 + 0x40 k, so `matrix[0]` is set 0 and `matrix[1]` is set 1). Whichever set is live, 0049E480 computes `m[r] = Σ_j B[r][j] A[j]` with B = set 0 and A = set 1, which with GT4's row vectors is `v · set0 · set1`. Set 0 is the camera chain: 004ABEC0 sends light positions "through camera matrix set 0", 0049F7D8 takes the camera position from its inverse, and 003B8EA0 reads a light's *view* position from it. So **set 0 = object-to-view** (camera times every object transform pushed on the stack) and **set 1 = view-to-clip** (a GL projection; the viewport is separate, the batch's qwords 197-198; confirmed by spike R1, `spikes/R1.md`). World space needs the camera itself, which is set 0 at the base of its stack when a view starts. See RISKS.md R1. |
| Splitting the halves is "a hook in one native function" (0049E480) | `grep func_0049E480 game_src` | **Not at 0049E480.** It has 12 callers: models (0049D328), sprites, 2D projections, billboards, glows (00497838, 004998A8, 00499D00, 00499F18, 0049BBF0, 0049C2A8, 0049C788, 0049E3E0, 0049FAC8, 004A6C58, 004A70E8). The instance hook belongs in **0049D328**, after it has called 0049E480 and 0049E548, which is matched source (`func_0049D328.cpp:1`, "from its source"). |
| The scene's camera is empty (`gsn_scene.cpp:2692-2696`) | Read it | **Confirmed**: a default `SceneCamera`. |
| The Light command is the substitution point (`gs_frontend.cpp:821`, LIGHTING.md:17-22) | Read `sceneRead`, `GsnScene::Impl::execute` | **Confirmed.** Detail that matters: the command is emitted when a *later* draw reads the target, so the model draws into it are already in the frame list. The back end receives whole frame lists before it executes them (`gsn_scene.h:37-39`), so it can look ahead and skip those draws. |
| Linux build break at `link_host.cpp:243-246` | Read the `#ifdef` layout | **Confirmed**: `FILETIME` and `GetProcessTimes` sit outside every `#ifdef`; the POSIX block is lines 110-171. |
| Hooks run through drafts | Header of each hook function in `game_src/`, `draft_natives.txt` of the game folder's build (418 lines) | **Confirmed and wider than the audit lists.** Drafts on the extraction path: 00455708 (model display program), 004A5348 (matrix set switch), 003B8EA0 (light glow), 004ABEC0 (light parameters), 00340950 (`render_cars`), 003E7A58 and 003E7F08 (smoke), 003C1430 (sparks), 0049CC48 (per-packet texture events), 003924D0 (`RaceCarSound::playStart`). Matched or native: 0049D328, 0049E480, 0049E548, 0049E228, 004A53F8/004A5400 (matrix push/pop), 00341E50 (`render_back_mirror`). See RISKS.md R2. |

Four more facts from the code that shape the design:

- The frame walker accepts a record block only if its first record's type is at most `kEffect`
  (`runtime/frame.cpp:266`). A new record type must widen that test.
- The front end merges consecutive model jobs with the same state into one `Draw` (`gs_frontend.cpp:1792-1798`).
  To skip one view's draws and keep another's, that merge must stop at view boundaries.
- `ModelJob` has two spare words (`pad[2]`, `gfx.h:599`) inside its std430 layout. An instance index fits there
  without changing `gsn_models.comp`.
- GS Z lives in the D32 targets as `z / 2^32` (`gsn_common.glsl:50-53`). A substituted scene can write depth that
  GT4's later draws test against exactly.

---

## 2. Principles

1. **`original` is not touched.** With `render.scene = original` (the `original` preset, and the default until
   the hybrid mode ships), the game sends no scene records, the host runs no new code on the frame path, and
   every replay gives the same pixels as a80cfca. Section 6 says how this is enforced.
2. **Records describe, draws still happen.** The game keeps sending every batch and draw it sends today. Scene
   records are extra information. The host chooses, per view and per frame, which representation it draws.
3. **One substitution point.** The modern renderer replaces GT4's 3D scene at the Light command and writes
   colour and GS-compatible depth into the GS target. Everything GT4 draws afterwards (glows drawn late, glare,
   grading, HUD) composes over it unchanged. Later phases take those over one at a time.
4. **Fall back per view, never half a view.** If a view is missing anything (an instance not linked, a mesh not
   decoded yet, a validation failure), the GS path draws that view this frame and a counter says why.
5. **Every modern feature is a setting with presets.** `original` is the PS2 path. Presets low to ultra pick
   modern features. A setting the current renderer does not use says so (as `1b495a6` started).
6. **No Sony or Polyphony data in the repo.** Tests that need game data read captures from the user's build
   folder and skip (exit 77) without them. Reference images and frame hashes made from game data stay local.

---

## 3. Modules and boundaries

```
 gt4_game.exe (x86-32, gcc 2.9 objects)                       gt4_host.exe (x64 / ARM64)
 ------------------------------------------                   ----------------------------------------------------------
 game_src hooks (A)            runtime (A)                    GS stream thread           front-end thread
  0049D328 instances     -->   pgl append (kScene)  ==link==>  stream.cpp records -->    GsFrontend (Kept, small Changed)
  render_world views           frame walker                      SceneDecoder (New) --+    FrameList + SceneFrame (New)
  glows, car lights            (records in order                 renderers3d (Kept)   |         |
  emitters, markers             with the batches)                ModelJob.pad[0] tag  |         v
  vehicle state (S6)                                                                  |   render thread
                                                                 scene-cache jobs <---+    RenderGraph (New)
                                                                 (mesh decode/weld,         GS segments (Changed gsn_scene)
                                                                  materials) (New)          items in place (New, B8)
                                                                                            lighting at the Light
                                                                                            display pipeline nodes (Changed pipeline.cpp)
                                                                                            present
```

### 3.1 Module table

| Module | Where | Lane | New / Changed / Kept | Boundary (what it may call, what it exposes) |
|---|---|---|---|---|
| Scene record types | `include/gt4/gfx.h` (namespace `gt4::gfx::scene`) | A | **New** (section added) | Plain data, both compilers. CONTRACTS.md (a). |
| Game hooks | `game_src/func_0049D328.cpp`, `func_0033DEA8.cpp`, `func_00341E50.cpp`, more per PLAN.md | A | **Changed** | Call only `gt4_gfx_scene()`; read the scratchpad and shared vectors; never write game state. Guarded by the scene-records game option. |
| Record emitter | `runtime/native/pgl.cpp` (`gt4_gfx_scene`), `compat/game/gt4_gfx.h` | A | **New** function | Appends to the pgl context's packet like `gt4_gfx_batch_model` (`pgl.cpp:362-368`), so records keep their order with batches. |
| Frame walker | `runtime/frame.cpp:266` | A | **Changed** (one bound) | Accepts blocks that start with `kScene`. |
| Game option | `runtime/runtime.h:26` (`kOptionSceneRecords = 5`), `runtime/sdk.cpp` | A | **Changed** | Host turns records on; off by default. |
| Record decode | `platform/gs/stream.cpp` (`case gfx::kScene`), `platform/scene/scene_decoder.{h,cpp}` | A | **New** + one case **Changed** | Validates and decodes records into a `SceneFrame`; tags each `ModelJob` with its instance (`pad[0]`). |
| GsTarget interface | `platform/gs/stream.h:24` | A | **Changed** (one virtual with a no-op default) | `scene(const uint8_t*, size_t)`; `QueuedTarget` (`gs_host.cpp:306`) forwards it in order. |
| Front end | `platform/gsnative/gs_frontend.{h,cpp}` | A | **Changed** (small) | Carries the `SceneFrame` in `FrameList`; stops merging model draws at a view boundary **only** when `setSceneSplit(true)`; records each `Draw`'s view. No behaviour change when the split is off. |
| Scene cache | `platform/scene/mesh_cache.{h,cpp}`, `material_table.{h,cpp}`, `scene_cache.{h,cpp}` | B | **New** | Input: model packet bytes + VIF state + batch words, by content hash. Output: GPU meshes (object space) and material ids. Keys and ids on the front-end thread (B6); decodes on the mesh cache's own workers until lane C's job system. |
| Render graph | `platform/render/graph/` | B | **New** | CONTRACTS.md (b). Owns resources, barriers, aliasing, queues, timestamps. |
| Pipeline cache | `vk::Device::pipelineCache` + `vk::open/save/closePipelineCache` (`vk.{h,cpp}`, `renderer.cpp`, `main.cpp`: uncommitted work in gt4-native-game, ticket B2), then `platform/render/pipeline_cache.{h,cpp}` (B2b) | B | **Changed** (B2), **New** (B2b) | Persistent `VkPipelineCache` in `pipeline_cache.bin` beside `settings.ini`; later a pre-warm list and async requests. |
| Display pipeline | `platform/render/pipeline.cpp` | B | **Changed** (ported to graph nodes, same shaders) | Same output bit for bit. |
| GS back end | `platform/render/gsn_scene.cpp` | B | **Changed** (frame list split into graph segments at Light commands; scene stage passes as nodes) | Same output bit for bit. |
| Modern view renderer | `platform/render/modern/` (parity, G-buffer, lighting, shadows, probes, sky, transparents, particles, decals) | B, F | **New** | Graph features (CONTRACTS.md (b) `Feature`). Read `SceneFrame`, scene cache, light list. |
| In-place substitution | `gsn_scene.cpp` (`executeRange`), `platform/render/modern/` (the parity and hybrid stages) | B | **New** | At a modeled run of a bound, valid view: draws its items from the mesh cache with B7's vertex stage and the mode's fragment stage, in GT4's order, writing side channels (normal, material id, view-space depth, motion) beside the scene target. No compositor (B8). |
| Light list | `include/gt4/render/light_list.h`, `platform/render/shaders/light_list.glsl` | F | **New** | CONTRACTS.md (c). Shared by night lighting and clustered lights. |
| Job system | `platform/core/jobs.{h,cpp}` | C | **New** | Work-stealing pool; used by scene cache, front end (later), command recording (later). |
| Test harness | `platform/sdl/main.cpp` (capture options), `tools/frame_diff.py`, `tools/perf_report.py` | C | **Changed** / **New** | Capture N frames, diff, timings JSON. |
| Input, audio, saves, loading | `platform/input/` (New), `platform/audio/` (New), `runtime/memcard.cpp`, `runtime/iop.cpp`, `runtime/audio.cpp` | D | New / Changed | The audit's section 4. No dependency on the renderer except spatial audio and FFB (vehicle record). |
| Host UI and 2D layer | `platform/render/overlay.cpp`, `platform/sdl/settings_menu.cpp`, `platform/render/modern/layer2d.cpp` | E | Changed / New | 2D layer needs the HUD markers. |
| Settings | `platform/sdl/settings.{h,cpp}` | E owns the table | **Changed** | Each lane's options in its own file after ticket E1. |

### 3.2 What is kept as is

- The game's simulation, its threads and the kernel (`runtime/kernel.cpp`).
- The GS command path end to end: native records, VIF1, `renderers3d.cpp`, `gsn_models.comp`, GS memory model,
  texture sets, reinterpretations, readbacks, the display (`gsn_display.frag`). It is the `original` renderer,
  the fallback for every view, and the reference for parity.
- The link transport (`link_host.cpp`, `runtime/link.cpp`): scene records travel inside the existing
  `GT4_REC_GFX` blocks. Only `GT4_LINK_VERSION` moves to 3.
- Captures and replays (`GT4_REC_STATE`, `stream.cpp`). Old captures replay as before; they have no scene
  records, so modern views fall back.

---

## 4. Data flow, one frame

1. **Game thread, frame N being built.** At `render_world` (0033DEA8) the hook sends `FrameBegin` and, for the
   main view, `ViewBegin` with the camera (set 0 at its stack base) and the projection (set 1). Each Cmd drawn
   by 0049D328 sends one `Instance` (object-to-view, scales, item count, object identity from 00455708's
   context) **before** its `BatchModel` / `BatchRun` records. The mirror view (`render_back_mirror` 00341E50)
   and the environment-map passes get their own `ViewBegin` / `ViewEnd`. Lights, emitters and the vehicle
   record go in where the game computes them. `Marker` records bracket GT4's post effects and the HUD. The HUD's
   3D widget and the menus' car views, drawn after `render_world` returns, get their own views in the same frame.
   All of it goes through pgl's packet, in issue order. The runtime closes the frame at its kick (004A11F8): it
   sends `FrameEnd` right after the walked list (CONTRACTS.md a.5.1).
2. **Frame walker (game thread).** Sends the record blocks as today (`frame.cpp:183-246`); `kScene` records
   carry values, not guest pointers, so the walker sends them as they are.
3. **GS stream thread (host).** `GraphicsStream::records` hands `kScene` payloads to the `SceneDecoder`,
   which keeps the current view and instance. Each `BatchRun` of an instance's Cmd produces a `ModelJob` whose
   `pad[0]` is the instance index + 1 (0: none). New mesh keys (the Cmd's packet hashes) are queued to scene-cache
   jobs with copies of their bytes.
4. **Front-end thread.** Builds the `FrameList` as today. With the split on, model draws stop merging at view
   changes and each `Draw` records its view. The `SceneFrame` (views, instances, lights, emitters, markers,
   vehicles, counters) moves with the `FrameList`. The Light command is recorded where it is today.
5. **Render thread.** For each frame list:
   - build the graph: GS segments split at each Light command (and at each marker the 2D layer needs);
   - inside each GS segment, at a modeled run of a view the settings give to the modern renderer that passed
     validation, the run's items are drawn in place from the mesh cache by B7's vertex stage (GT4's transform
     and Z, exact) and the mode's fragment stage, writing side channels beside the scene target (B8). Everything
     else in the segment, untagged draws included, is drawn as today, in GT4's order;
   - at a Light command, the scene stage combines: today's relight, then modern lighting from the side channels
     (B10 on);
   - then the remaining GS segments (GT4's post effects, HUD) and the display pipeline nodes.
6. **Display pipeline.** As today, as graph nodes. In modern mode (later), GT4's post effects are skipped and the
   pipeline's bloom, exposure and tone map run on the modern scene's linear light; the 2D layer composes the HUD
   at output resolution.

### 4.1 Modes, per view

| `render.scene` | Main view | Mirror, env maps | GT4 post effects | HUD |
|---|---|---|---|---|
| `original` | GS path | GS path | GT4's | GS path, render scale |
| `parity` (debug, `--set`) | modern renderer with GT4's own lighting per vertex, 1x | GS path or parity | GT4's | GS path |
| `hybrid` | its items drawn in place by the modern stages, in GT4's order with GT4's Z; modern lighting at the scene stage from the side channels | the same per view role (B9), else GS | GT4's | GS path |
| `modern` | the same ordered path, with a modern scene target (HDR) and the modern lighting and effects at the scene stage | the same | skipped by their markers; pipeline bloom, exposure, tone map on linear light | 2D layer at output resolution |

**How `modern` (B15, the end state) sits on the ordered path without a rewrite** (lane B):
- The same ordered path draws the views; `modern` changes what each stage does, not where draws happen. The
  substituted views' passes draw into a modern scene target (HDR, more precision) in place of the GS one. The
  vertex stage keeps GT4's Z for the order; lighting and depth effects use the side channels' view-space depth, so
  GT4's depth partitions never constrain them.
- At the Light command, the modern lighting and post run (tone map, bloom, grading), and GT4's post chain after the
  Light is skipped by its markers (A5a's post and HUD markers; N4 already skips the flare and glare brackets). The
  HUD stays GT4's, in the 2D layer.
- The untagged draws move to the modern side one class at a time as lane A tags them (sky, particles, billboards:
  `kInstBillboard` is the first), each still drawn at its place in the order. When all are tagged, every draw is
  modern and still ordered; re-sorting transparents, if ever wanted, is then a local change in the plan.
- Nothing in B8 assumes the GS target's format, its resolution or GT4's post: those are the GS path's defaults,
  and `modern` replaces them per view.

The `original` preset sets `render.scene = original`. Presets low to ultra set `hybrid` (then `modern` once phase
B-4 passes its gates). `parity` is never in a preset.

---

## 5. Threading

| Thread | Today | Added |
|---|---|---|
| Game threads (32-bit process) | One host thread per guest thread, one running at a time (`kernel.cpp:183-203`) | The hooks' record writes, inline: about 1,000 instances x 120 bytes a race frame, no allocation. Nothing else. |
| GS stream thread | Link poll, decode, VIF, 3D batches (`gs_host.cpp:741-760`) | `SceneDecoder` (cheap: copies and checks). Hands new mesh keys to the job system. |
| Front-end thread | GS front end (`gs_host.cpp:306-329`) | Carries the `SceneFrame`; nothing heavy. |
| Job workers (New, C lane) | none | Mesh decode and weld, material classification, BC compression, later BLAS builds and parallel command recording. Results land in the scene cache under a lock-free publish (a mesh is used from the frame after it is published). |
| Render thread | `GsnScene` execute, display pipeline (`main.cpp:732-1000`) | Graph build, compile and record. Later: per-node secondary command buffers recorded on job workers. |
| Async compute queue (New, optional) | one queue family (`renderer.cpp:325`) | Graph passes marked async run there when the device has a separate compute family; otherwise in order on the graphics queue. |

Frames in flight stay at 2 (`gsn_scene.cpp:29`). Scene-cache GPU uploads go through the existing uploader
(`vk.h:218`) on the render thread.

Determinism: nothing on the host feeds back into the game except what does today (pads, GS events, image reads).
One exception needs a rule: the game reads pixels of its scene target (luminance probe 00330AE8, photo tiles,
car thumbnails; GRAPHICS_API_PLAN.md section H). In `hybrid` and `modern`, those reads would see the modern image.
B8's read switch handles them: a list holding a read whose reader RISKS.md R9 hasn't cleared draws that view on the
GS path, so the read sees the PS2's picture (cost: those frames lose the modern look). Readers of Z alone are exact
anyway, since GT4's Z is kept.

---

## 6. How `original` stays untouched

Four mechanisms, each with a test in PLAN.md:

1. **No records without the option.** The hooks are `if (gt4_game_option(kOptionSceneRecords)) ...`; the host
   sends 0 for `render.scene = original`. The link stream is then the same bytes as at a80cfca.
2. **No new host code on the frame path.** The `kScene` case is never reached; `setSceneSplit(false)` leaves the
   front end's merge as it is; the graph's GS segments are one segment per frame list when no modern view exists.
3. **The graph port is pixel-identical.** Tickets B3 and B4 (pipeline and GS back end on the graph) are gated on
   bit-identical captures of every preset on the reference captures, before and after.
4. **Golden comparison on every visual change.** `tools/frame_diff.py --baseline` replays the reference captures
   with the build before and after a change, at the `original` preset, and requires identical frames. Hashes
   and frames stay local (game data). `host_replay` (ctest) keeps running in every build.

---

## 7. Settings

New keys (the settings table is lane C's, options in per-module files after E1):

| Key | Values | `original` preset | low / medium / high / ultra |
|---|---|---|---|
| `render.scene` | `original`, `hybrid`, `modern` (`parity` by `--set` only) | `original` | `hybrid` (later `modern` from high up) |
| `render.scale_mode` | `integer` (GS path rule), `free` (modern views) | `integer` | `free` |
| `lighting.materials` | `original`, `pbr` | `original` | `pbr` |
| `lighting.night` | `original`, `enhanced` | `original` | low `original`, others `enhanced` once N3 has landed (until then `--set` only, PLAN.md F2) |
| `effects.shadows`, `effects.reflections`, `effects.ambient_occlusion`, `effects.particles`, `effects.sky_fog`, `effects.bloom`, `effects.dof`, `effects.motion_blur` (exist) | as today | `original` | as today; they now steer the modern nodes too |
| `effects.decals` | `off`, `on` | `off` | medium up `on` |
| `effects.wet` | `original`, `enhanced` | `original` | medium up `enhanced` |
| `effects.ray_tracing` | `off`, `reflections`, `all` | `off` | ultra `reflections` where the device has ray query |
| `render.upscaler` (exists) | adds `fsr3`, `dlss`, `xess` | `bilinear` | as today, plus the new ones where available |
| `input.*`, `audio.*`, `saves.*` | lane D | PS2 behaviour | per lane |
| `ui.*` | lane M (MENU_DESIGN.md section 3.8) | `ui.entry = hotkey`, `ui.pause = gt4` | `integrated`, `native` |

Each option states which renderer uses it; the settings pages grey out the ones the current `render.scene` does
not use (extends `1b495a6`).

---

## 8. GT4 night artefacts the new renderer must not inherit

From the owner's playtests on S.S. Route 5 at night, all three also on PCSX2, so they are GT4's own:

1. **Section step**: driving from one track section into the next, brightness or contrast changes in one step.
2. **Lamp bands**: some street lamps throw wide vertical bands of light on the asphalt.
3. **Warning-light tint**: blinking yellow warning lights tint the whole frame yellow.

`original` and `parity` keep them (they are the PS2 picture and its proof). `hybrid` and `modern` must not
have them. Treating prelit colour as baked lighting, as the audit proposes, would carry (1) over unchanged, so
the source of each must be found first (RISKS.md R10-R12) and removed where it enters, not hidden after:

| Artefact | Where it can come from (to be decided by the spike) | How `hybrid` / `modern` remove it |
|---|---|---|
| Section step | per-section prelit vertex colours; per-section lighting constants (the car lights' matrices, `MLightSetting::read` 0x00210988); per-section fog (FOGCOL, density); per-section colour-grading curve and weights (`kGrade` effect record, 004A7D90); the glare threshold | **Grading or fog parameters**: the host blends the parameters over time across a change (`SceneGrade` smoothing, about 1.5 s) in `hybrid`; `modern` does not run GT4's grading and its exposure is scene-referred eye adaptation. **Lighting constants**: blended over distance across the boundary, per instance by its position. **Prelit colour**: a per-section gain from the section's prelit luminance statistics, normalised to its neighbours and blended over distance near the boundary, applied when the prelit colour enters the G-buffer as baked lighting. `Instance::section` carries the section. |
| Lamp bands | the road reflection pass (`render_reflection` 003411D0, `render_reflection_car_lights` 0x00341A30) drawing lamp reflections as stretched sprites; tall glow quads from 003B8EA0; glare taps | The draws are identified by a material class (B6; the lit-road layer is course geometry with no call site to bracket) and skipped in `hybrid` and `modern` when the lamps' own light replaces them (PLAN N3). The lamps' own `Light` records light the road through the clustered lights, with SSR and the wet-road BRDF for real reflections. |
| Warning-light tint | the flare pass (003B8848 / 003B88C8 samples, read back by 00497EF8 / 00497F48) driving a full-screen flare or veil; glare 003BADF8's bright pass and add-back | The flare's draws (`kMarkFlareBegin/End`) and, in `modern`, GT4's glare are skipped. Lights get the pipeline's energy-conserving bloom and eye adaptation on linear light; a blinking light changes its own light and bloom, not the frame's mean colour. |

Each has a ticket with an acceptance test on a Route 5 night capture (PLAN.md lane F): no step in mean
luminance at section boundaries, no bands, no full-frame tint when the warning lights blink. For the section step,
R10 found no one-frame step on the Reverse layout (gradual changes, mostly a lit road overlay, which goes to the lamp
bands' ticket N3), so that row's remedies are parked until a capture shows one (PLAN.md N2).

---

## 9. Validation built into the architecture

- `SceneDecoder` counters (CONTRACTS.md (a) section 7) in `GT4_GSN=stats` and in `--profile-json`.
- Per view, a recompose check: `D · objectToView · viewToClip == ModelJob::matrix` (with the instance's own projection; the viewport checked against the batch's scale and offset) for every batch of every
  instance, counted, not asserted.
- Parity mode: the modern renderer's output against the GS path's on the same frame, by `tools/frame_diff.py`.
- Graph: Vulkan validation layers and synchronization validation in debug runs (`--set debug.validation=on`),
  zero errors required on the reference captures for every graph ticket.
- Per-node GPU timestamps and CPU scopes in `--profile-json`, compared by `tools/perf_report.py` against a
  baseline file kept in the user's build folder.

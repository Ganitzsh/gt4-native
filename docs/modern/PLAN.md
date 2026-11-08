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
| `ORIG` | `tools/frame_diff.py --baseline OLD NEW --preset original` on the REF captures, every frame; plus `host_replay` | identical frames. **Re-baselines** (a standing rule, the coordinator, 2026-11-08): `original` means matching the PS2, so a commit that fixes a correctness bug in the GS path may move `ORIG`, on these conditions: it is its own commit; the frames it changes are compared with PCSX2's software renderer and shown closer or equal; its `PERF` is checked; the new reference (captures and hashes) is recorded and announced to every lane with the commit's hash; and it is listed in CAPTURES.md's re-baselines, so a comparison never crosses one by mistake. `ORIG` gates compare against the latest reference from that commit on. Refactors and performance changes never move `ORIG`. The first: the commit that makes the shared transform and lighting shaders precise (CONTRACTS b.10, B7), its near-tie pixels compared on PE race 2's countdown (`race_grid43.cap` against PCSX2 software stills; PCSX2 can't replay a capture) |
| `PRESETS` | the same at `original`, `low`, `medium`, `high`, `ultra`, and the test scene's `tests/scenarios/features.json` captures | identical frames |
| `PARITY` | `render.scene=parity` against the GS path at 1x on the REF captures, per frame | >= 97.5 % of pixels identical, >= 99.97 % within 4 per channel (the GPU-models step's bar, docs/NATIVE_V2.md:90) |
| `VAL` | REF captures with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, synchronization validation on | zero messages |
| `PERF(x)` | `tools/perf_report.py` on `--profile-json` of the REF captures against the baseline build | the named stages within x |
| `REL` | game from `tools/release_game.py`; `tools/hook_lint.py` | builds, lint clean |
| `DIFFTEST` | a `gt4_game` linked from the release build's objects with `tools/game_link.py --decomp <decomp> --objs <release_game --out>/obj --out <dir> --eeorig build/eeref/genh` (`--objs` is searched recursively) (`tools/eeref/build.cmd` makes `genh`), run from any working directory with `--elf <decomp>/orig/SCUS-97328/CORE.GT4.elf` (or `GT4_ELF`) and `GT4_ORIG_KINDS=<checkout>/build/eeref/kinds.txt GT4_DIFFTEST=all` (`docs/DIFFERENTIAL_TESTING.md`). `known.txt` is read from beside the compiled `tools/eeref/difftest.cpp`, so from the checkout that linked the game (`GT4_DIFFTEST_KNOWN` overrides; the run logs which file it read, and a lane running another lane's build reads that clone's). Each test runs the native and the original code on the same inputs and compares what they write, including what a packet-sending native delivers (batch runs, buffers, vertices, records). The link takes a game slot (about 3 min), the run a heavy slot (about 20 s). Required, besides the ticket's own gates, for every ticket that changes `game_src` or a native that replaces game code (the coordinator, 2026-11-08), whatever the lane: among them the records in `include/gt4/gfx.h`, `platform/gs/vif1.{h,cpp}` and `tools/eeref/` itself. Its author runs it | exits 0: every test agrees, or differs only as `tools/eeref/known.txt` lists. A new line in `known.txt` is reviewed by lane U (since lane U's cc995a5 / 63eaa64 / 93ceb38: 111 tests, 003E8438 known; it would have caught 5f5aad7's PRIM bit and 838699f's record padding) |
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
| B. Renderer | `platform/render/graph/`, `platform/render/modern/`, `platform/scene/mesh_cache.*`, `material_table.*`, `scene_cache.*`, `platform/gs/vif1.*` (the mesh cache's input; lane A reviews, since A1 and A10 use its runs; P1, lane C, removes its stream path), `pipeline.cpp`, `gsn_scene.cpp`, `vk.*`, shaders; for P2, the target, page and palette parts of `gs_frontend.*` and `gs_memory.*` (lane A reviews) | `renderer.cpp`, `main.cpp`: B2 **first** (day one) |
| C. Platform, ARM64, tooling | the settings table (from lane E, 2026-11-08): `platform/sdl/settings.{h,cpp}`, `settings_table.h`, the presets and the shared parts (`settings_display.cpp`, `settings_render.cpp`, `settings_effects.cpp`, `settings_scene.cpp`). The per-module files stay with the lanes that add to them (`settings_input.cpp`, `settings_audio.cpp` lane D; `settings_lighting.cpp` lane F; a future `settings_ui.cpp` lane M), lane C reviewing each change for the table's rules (preset membership, `original` unchanged, `unusedReason`), `cmake/`, `tools/build_*`, `tools/package_game.py`, `tools/frame_diff.py`, `tools/perf_report.py`, `platform/core/`, `link_host.cpp`, `docs/modern/CAPTURES.md`, `platform/gsnative/texture_pack.*`, `platform/sdl/session.*` and `tools/host_session.py` (the test harness), `runtime/link.cpp` (lane A reviews every change: its async reads and tick deferral) | `main.cpp`, `renderer.cpp`: C1 **after B2** |
| D. Systems | `platform/input/`, `platform/audio/`, `runtime/memcard.cpp`, `platform/saves/`, `runtime/iop.cpp`, `runtime/audio.cpp` | `main.cpp`: D1 **after C1**. `protocol.h`: D2 **after A1**. `stream.cpp`: D5 **after A10** |
| E. UI | no agent; its files moved on 2026-11-08 (the coordinator): the settings table to lane C, `overlay.*`, `settings_menu.*` and `layer2d.*` to lane M. Its remaining tickets E5 (SDF font), E6 (2D layer), E7 and E9 (HUD) go to lane M with their files (the coordinator), lane C reviewing | `gs_frontend.cpp` `textureFor` (E2), `gsn_display.frag` (E3): day one, **before A10** |
| F. Night lighting | `include/gt4/render/light_list.h`, `shaders/light_list.glsl`, `platform/render/modern/lights_*`, `tools/frame_metrics.py`, the Route 5 tickets | `gsn_scene.cpp` scene stage: F2 **after B4** |
| M. Menus (docs/modern/MENU_DESIGN.md) | `platform/ui/` (page engine, UI draw list, UI renderer, SDF text), `platform/render/modern/layer2d.*`, `platform/render/overlay.*`, `settings_menu.*` (deleted by M9), `runtime/menu/`, `assets/ui/`, `tools/ui_assets.py`, `include/gt4/ui.h`, the menu-flow `game_src` copies (00109D00, 0010FE40, 0038B428, 0038B2B0, and 00109B38, the menu game object's update where the pages tick); lane C reviews the host-side UI files | `protocol.h`: M1 took link version 4 (ba5c358); a later change to what the game sends takes the next one. `runtime/link.cpp` (lane C's, lane A reviews). `runtime/iop.cpp` (lane D reviews). `main.cpp` (lane C's order) |

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

**A5b: Flare and glare markers (S).** `kMarkFlare*` around the flare read 003B8848 (in 0033EAB8) and around
003B88C8's samples and glows (in 0033F2E8); `kMarkGlare*` around 003BADF8 (in a copy of 0033F1C8). No lamp-band
markers: the lit-road layer is course geometry with no call site of its own, so N3 finds it by a B6 material class
instead (`kMarkLampBands*` stay reserved and are never sent). Depends: A3, R12. Acceptance: `REL`; `ORIG`;
`hook_lint` 0; on a night race with records, no frame ends with a bracket open, and with N4's marking the flare
bracket's draws are counted by target. Draws in the bracket not onto the picture are about 0: the flare's samples are
a read of the scene (GS to host), not draws. The glare
bracket is checked on a race whose glare settings have flag bit 0 set (0033F1C8 calls 003BADF8 only then): neither
the night Route 5 race nor a day Pan Euro race set it, so that live check stays open until a course with glare is
found, like the warning lamps at the Route 5 pit entry.

**A6: Further views (M).** Env-map passes 0044E660 / 0044EB10 (`kViewEnvFace`), the road reflection
(`render_reflection` 003411D0, `kViewReflection`), showroom jitter passes, Photo Travel. `Instance` records for the
untagged draw paths: the car light maps. The view passes come first (the architect, 2026-11-08): they run in every
race and B9 waits on them.
- **The env faces** (lane A, 2026-11-08, a replay with `GT4_SCENE_DEBUG=1`, 7,617 frames). Each face renders into
  its 128 x 256 target; 0044E660 then copied it into the shared env map inside the face's view, which split every
  face by rule 4. The face's `ViewEnd` moves before the copy, and the copy gets its own `kViewEnvCopy` view
  (CONTRACTS a.5.7). Acceptance below: each face bound to its target, unsplit.
- **The road reflection** split the main view by rule 1 (0.56 a frame): two `Instance`s of the main view (3 and 7
  items, about 7 draws a frame) into a 64 x 32 colour target with its own Z (PSMZ24), tested and written; that fits
  `render_reflection` (003411D0, buffer from +0xC24 / +0xC28, through the course renderers 00397130 / 00397098).
  It is drawn in the middle of the main view, with about 330 principal draws after it, and on Route 5 night its
  output is read 11 times in 1,200 frames, all by untagged draws in the same frame (lane A, 2026-11-08). The
  architect's call: its instances are flagged `kInstReflection` and kept out of the binding, as the light maps
  are (CONTRACTS a.5.7); no nesting, no split of the main view. The flag is set by a context around the pass (a
  `game_src` copy of its caller, which also proves the caller), never from the target. `kViewReflection` stays
  reserved: B9 decides whether it needs the reflection as a view, from a wet course's capture (C10's), where its
  readers are measured first.
- **The paths that run after a Cmd** (CONTRACTS a.5.3: 00497838's pre-built shapes, 0049FAC8's Cmd-like models) are
  situational. With the light maps tagged, `runsAfterInstance` is 0 on Route 5 B-spec night (lane A, 2026-11-08,
  7,600 frames; 43,686 with step 1b alone). Their callers in the decomp: the race's sparks (ThickLine2,
  `ParticleManager_SparkParticle`), the HUD's round meter (`RaceRoundMeterBase` virtual 14), Photo pause (003C4EC0,
  003C5AB8), and two game-data functions outside races (00194DB0, 001AB110). So they go with what draws them:
  sparks to A8 (tagged on A8's capture with contact); the round meter with the HUD view, checked here on a lap race;
  Photo pause with Photo Travel's views (R6). Where tagged, their `objectToView` is the position they are placed
  by, and a 0049FAC8 model's 0049E480 / 0049E548 composition is checked against R1's rule.
- **The car light maps** (lane A, from the code, 2026-11-08; the architect's call (A)). `render_cars` (00340950)
  calls, per car with its light context enabled, 0038E7F8 (fills the context from the course's light table near
  the car: up to six lamps) and 0038F5D0, which binds the car's light-map target (a 256 x 256 `GSBuffer` on large
  courses) under an orthographic projection and draws per lamp a 10-segment radial fan at the lamp direction's
  sphere-map point (0038F408, 0049C0D8 prim 0x15), flushed by 0049BBF0. The car then samples it: the fans are its
  reflection map of the course lamps. They are most of the untagged runs left (about 5.8 a frame on Route 5 night).
  An `Instance` per batch run inside that pass, items 1, flag `kInstLightMap`, `objectToView` = set 0 as the pass
  set it, inside the main view. 0038F5D0 doesn't know the car, so it needs no copy: the `render_cars` copy
  (`game_src/func_00340950.cpp`) opens the light-map context around each 0038F5D0 call, `entrant` = the car's
  entrant byte (`+0x544`, the byte its loop compares with the car index). Its draws
  are out of the binding (CONTRACTS a.5.7, light-map draws), so they can't split the main view or fill its four
  auxiliary targets. B6 gives them mesh 0 per frame as it does `kInstBillboard` (lane B's code, lane A reviews);
  F2b's and N3's classifiers skip their materials, since they light a texture, not the scene. Not done: a view
  per call (nesting inside the main view, which a.5 rule 2 forbids; B9's question if the maps are ever redrawn
  natively), and lights from the context's lamps (a third source of course lamps beside `entries2[]` and the
  billboards, only if F2 finds lamps missing; lane F says).
Depends: A3, R6. Acceptance: `REL`; `ORIG`; `SCENE` on the showroom and photo captures; each env face's views bound
to the 128 x 256 env target; `runsAfterInstance == 0` in race frames without contact (met on Route 5 night) and with
the round meter on a lap race; on Route 5 night, `viewSplitTargets` and `viewAuxiliaryDraws` unchanged by the
light maps, `viewSplitTargets == 0` for the main view with the reflection flagged, `viewLightMapDraws` and
`viewReflectionDraws` reported.

**A7: Light records (M).** Sources, corrected by lane A from the code (2026-11-08, a scan of the ELF for every
store to the lists' counts). The light-glow pass 003B88C8 draws from two per-frame lists in its object:
- **Some course lamps are in `entries2[]`** (lane A, corrected 2026-11-08). The first hook read the list too
  early, before the course sources were added, and saw it empty. Read right before 003B88C8 draws it, a B-spec
  S.S. Route 5 Reverse night race sends 1,928 `kSrcCourseGlow` lights over 1,200 frames (about 1.6 a frame),
  besides 33,027 car lamps. They are not `BillboardSet__render` billboards (one type, vegetation-like). How many of
  the street lamps the list holds is not measured yet: F2b compares them with its lamp heads.
- **Step 1a, warning lamps and car lamps, now.**
  - `entries2[100]` at `+0x184C` (count at `+0x1848`; 24-byte hits: view-space x, y, z, distance, a visibility flag,
    the source pointer, which gives the params index). Its only writer is 003B7B70 (matched source,
    `src/race/unit_003B7470`), reached from `RacePS2Base` virtual 126 (method 0x3F0) -> 00397288 -> 00397AE8 ->
    00397BA0. A source whose `params[index]` blink is above 0 is a warning lamp (`kSrcWarning`, its smooth blink
    factor 0..1 in `intensity`); the rare others are `kSrcCourseGlow`.
  - `entries[100]` at `+0xA38` (count at `+0xA34`; view-space x, y, z with -z as depth, a `Light*` with width
    `+0x40`, height `+0x44`, rgb `+0x54`, a flag, dx / dy): the car lamps. Its only writer is 003B8530, called only
    from `RaceCarModel` virtual 35 (003911D0); 003B8EA0 (virtual 36) draws the same lamps' glows. The entries don't
    keep the lamp group, and the element's flags say only "on with the lights" (bit 0) and "hide when not Y"
    (bit 1). So step 1a sends every car lamp as a point `kSrcCarLamp` (a source of its own, so its small, dim
    values in `light_sources.cpp` don't shrink anything else sent as `kSrcOther`), with no direction, and F2
    decides how far such lights reach. Step 2 sends `kSrcHeadlight`, `kSrcTailLight` and `kSrcBrakeLight` where
    the lamp group is known. Reading the list runs no new code: whatever fills it is what the game's own glows use.
  - One `Light` per entry: point, range 0, colour = the light's rgb x the pass's rgb / 255, intensity without the
    view-dependent term (fields as agreed with lane F).
  - When: 0033EAB8 empties both lists (003B80E8, which also saves the current matrix at `+0x21B0`) after the main
    `ViewEnd` and the mirror view, with this frame's camera. The course sources are added after method 0x3F0, so
    the lists are read right before 003B88C8 draws them (in 0033F2E8, inside the flare bracket). The `Light`s are
    sent there, tagged with the main view's id (CONTRACTS a.5 rule 4). F2 reads them from the frame's `SceneFrame`
    by view id.
  - The glow-quad check passes (lane A, the same race): 53,546 of 53,546 flare-pass glows (003B88C8's additive
    stars, mostly car lamps; not the street-lamp draw F2b reads) have a `Light` at their centre.
- **Step 1b, the street lamps: course billboards** (lane A, 2026-11-08, correcting its own earlier reading). The
  street-lamp glows are `BillboardSet__render` (004506F0) billboards, from `CourseData::render_course_2` (00396F00):
  one quad each through 0049C0D8, flushed by 0049BBF0 (one type, one texture, tall quads up to y 10.7 above the
  road). No Cmd draws them, so their runs were untagged and B6 never saw them (lane F, F2b). Two parts:
  - **An `Instance` per batch run** inside the billboard context (a `game_src` copy of the matched 004506F0 sets it
    around its 0049C0D8 / 0049BBF0 calls): each run gets its own `Instance` just before it, items = 1,
    `objectToView` = set 0 (the quads are built in world space facing the camera), flag `kInstBillboard`. A flush
    may split into several runs (a VU1 batch takes a limited number of quads), and nothing has to predict how many.
    A flush's runs share one draw state and texture, so they get one material; the runs are tagged, B6 gives them a
    material, and F2b's classifier sets the glow class. The quads are rebuilt every frame, so they are per-frame:
    `SceneCache::assign` skips the mesh request for `kInstBillboard` (no key hashed, mesh 0, counted in
    `meshesPerFrame`) and still assigns the materials. That is B6 code: lane B writes it (`scene_cache.cpp`), lane A
    reviews; without it each flush would decode and cache a new mesh every frame. B7 then draws them on the GS path,
    counted as mesh 0. A flush never mixes billboard types, by
    construction: `BillboardSet__render` flushes when the type changes and at a tab entry (0x8000).
  - **A `kSrcBillboard` light per billboard** from its entry: world position `e->f4, f8 + h/2, fC` through set 0;
    the half width and half height as `Light` defines them (item `+0` is already the half width, since the quad
    spans x +- f0; the half height is f4 / 2); colour the mean of the item's vertex colours at `+0xC` / 128 (0x80
    = 1, so a channel may exceed 1), intensity their alpha's mean, white when the item has none. The flush's
    billboards are kept in a small fixed buffer during the batch and sent after the flush returns, so after all
    of its runs' `Instance`s, with `id` = the serial of its first `Instance` and the main view as their view
    (inside the view: a.5 rule 4's plain case). The host keeps one as `kSrcCourseGlow` only when that instance's
    material has the glow class, so tree billboards light nothing (CONTRACTS a.4).
- **Step 2, headlight spots:** each headlight's direction and cone from the car body (A9's record, D6), so the
  headlights light the road ahead and not around the car. In lane A's runs the player's car showed no lamps in
  `entries[]` in a time trial once the race ran (perhaps the in-car or bumper view hides them; whether the
  player's car is there in a B-spec race isn't established). So this step's spots are the sure source for the
  player's headlights. Captures for F2's car lamps are B-spec or races with other cars. R2's verdict on 003B8EA0
  only if its terms are needed.
  The car lights' reflections (0x00341A30) are not sent as lights: they would light the scene twice, and N3 handles
  their sprites.
Depends: A3; step 2 also A6, A9, D6. Acceptance:
- step 1a's code lands with the checks it can have now: `REL`; `ORIG`; `PRESETS`; `SCENE`: `lightViewUnknown == 0`,
  `lightsAfterViewEnd` reported, lights per frame by source, every light in front of the camera and on screen
  through the main view's projection;
- right after A5b (its flare bracket around 003B88C8, with N4's marking of the draws onto the picture): on a night
  capture, each warning-lamp and car-lamp glow quad the GS path draws has a `Light` whose position projected
  through the view's projection and viewport lands inside that quad (count of misses = 0);
- step 2: the headlight cones point along the car.

**A8: Emitter records (M).** `RaceSmoke::update` 0x003E7A58, `SparkParticle::Update` 0x003C1430,
`ParticleManager::Update` 0x003C2780 (drafts). Depends: A3, R2. Acceptance: `REL`; `ORIG`; on a race capture with
tyre smoke and sparks: emitters per frame reported, positions within the projected bounds of the sprites the GS
path draws for them. The sparks' draws through 0049FAC8 / 00497838 (A6) are tagged here, as an `Instance` with a
particle flag or by the `Emitter` record, decided on that capture; `runsAfterInstance == 0` on it.

**A9: Vehicle record (S).** From the per-car state D6 found, sent at the kick in `FrameEnd`'s own block, before
`FrameEnd` (CONTRACTS a.5 rule 4; `runtime/frame.cpp`, lane A reviews). Depends: D6. Acceptance: `REL`; `ORIG`;
the record's transform matches the car body instance's `objectToWorld` within 1e-3; speed matches the HUD's speed.

**A11a: Course instance flag (S).** While the course is drawn, the runtime marks the instances A2 sends with
`kInstCourse`. The course is drawn in three places (lane A, from the code): 00396D40 (the race's main view, near and
far passes, called directly from 0033EAB8; it never calls `render_course`); `CourseData::render_course` 0x00396E10
(the mirror, split screen, 003403B8, 00397048); and the env faces' course list in 0044E528 (its `ModelSet2` calls
on `scene->f14`, not the sky's `f5C`). That makes A3's camera check real (`cameraChecked` counts course instances;
until A11a it checks nothing), and B7 relies on a validated `worldToView`. The sky (00396B80) stays unflagged;
`kInstSky` can be set the same way later.
- **Course geometry and placed course objects** (lane A, measured): about 10 % of the course's Cmds come from model
  display programs (00455708, from `render_course_2` 00396F00) that push a matrix of their own: placed objects at
  their own position, scale and rotation. All of them sit one level deeper on the matrix stack than the course
  bracket did when it opened; every course Cmd at the bracket's own depth matched the camera. So `kInstCourse` is a
  Cmd inside the bracket at the bracket's depth (the runtime notes `gt4_matrix_stack_top` when the outermost bracket
  opens), and one at any other depth is `kInstCourseObject` (CONTRACTS a.4). The flag is set from the stack depth,
  never by comparing with the camera, so the camera check stays a real check.
Depends: A2. Acceptance: `REL`; `ORIG`; on a race capture with records, `cameraChecked > 0` in every race frame, in
the main view and the env faces, and `cameraMismatch == 0` over `kInstCourse`; `kInstCourseObject` counted; car,
wheel and sky instances never flagged.

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

**B6: Material table and the instances' ids (M).** `platform/scene/material_table.{h,cpp}` (`MaterialKey` to a
stable `MaterialId`, dropped after N frames unused, like the mesh cache) and `scene_cache.{h,cpp}` (owns a
`MeshCache` and a `MaterialTable`; one call per view). Each instance with jobs gets its mesh (B5, the jobs hashed in
place, copied only on a miss) and its materials per item, into `FrameList::ViewOnList::ids` (CONTRACTS a.8).
- **When:** in the front end's `sceneView()`, when the view's package arrives at its `ViewEnd` (every job of its
  instances is in the list being built by then, a.5.3), before the view goes on the list. A cut view is skipped.
  `SceneFrame`'s instances get a copy of the ids at `sceneFrame()`. Only while the scene split is on, like the
  packages, so `ORIG` and `PRESETS` stay identical by construction.
- **Jobs by tag:** an instance's jobs are found by `ModelJob::pad[0]`, not as a range (`firstJob` was never filled,
  and an item callback's runs can land between them). `model()` keeps each open view's job indices per instance
  (in lane A's file: lane B writes it, lane A reviews).
- **Mesh 0, counted:** skinned instances (`kInstSkinned`, vertices computed each frame; `meshesSkinned`) and
  items with a key-frame blend (the mesh cache's per-frame items, whose weight changes every frame;
  `meshesPerFrame`), since their content changes every frame; B7 leaves them on the GS path. The same for
  `kInstBillboard` and `kInstLightMap` instances (rebuilt every frame; no key hashed). Also pending meshes and cut
  views.
- **Materials:** one per item, as many as the mesh has (a car has more than four), in `ViewOnList::materials` and
  `SceneFrame::materials`, each instance pointing at its range.
- **Texture identity:** the front end keeps texture id -> identity (`textureIdentity(id)`, setTexture's key) next
  to its texture map, so a material keeps its id after an eviction (lane A's file, lane A reviews).
- Depends: B5, A10. Acceptance:
  - `CTEST`: the table (same key, same id; eviction; counters); `assign` on a synthetic list with two instances
    sharing a mesh, and an instance whose jobs are interleaved with a callback's;
  - on `race_boot.cap` with the split on: materials and meshes per race frame reported (expect a few hundred
    materials); the same object (`InstanceData::object`) keeps its mesh and material ids across race frames,
    counted; new meshes per frame after the race's first 300 frames near 0 outside the per-frame ones (after
    the first lap, for a lap-long capture or a live session);
  - `PERF`: the front-end thread's added time with the split on, at most 0.5 ms a race frame at p95, measured as the
    thread's CPU time (`QueryThreadCycleTime` on Windows, `CLOCK_THREAD_CPUTIME_ID` on Linux) beside the wall clock,
    since the wall clock's p95 includes preemption (lane B measured 330 us mean, p95 538-743 us under load). Cycles
    are converted with a one-time calibration against the high-resolution clock, said in the summary line, and the
    margin under 0.5 ms is reported, not only pass or fail (headroom for the Odin under FEX). Measured (4761e96,
    `race_boot.cap` after the race's first 300 frames): CPU p95 439 us (from 700), median 360 (from 587), p99 532,
    about 12 % under the bar; accepted by the coordinator. It took three cuts inside B6 (the mesh key hashes only
    what the decode reads; one material lookup per run of items from one draw; the stability check only with the
    log), which also decode 3x fewer meshes. A per-job identity in `vif1.cpp` ("B6b") was dropped: its buffers are
    never cleared, so a running hash never repeats, and a content hash per written qword costs more than it saves.
    The next cuts are B6c;
  - `ORIG` and `PRESETS` identical.

**B6c: B6's front-end cost, for the Odin (S, lane B; lane A reviews the `gs_frontend` parts).** Before `hybrid`
ships to players (before B8's release), the coordinator's follow-up to B6: the front end's item gathering per
instance, and the mesh request's lock taken once per view instead of per instance. Depends: B6. Acceptance: B6's span
in thread CPU time on `race_boot.cap` (after the race's first 300 frames), p95 below B6's 439 us and the margin
reported; the same span measured on the Odin in lane C's device run; `ORIG` and `PRESETS` identical.

**B7: Parity view renderer (L).** `platform/render/modern/parity_view.cpp`, `shaders/modern_parity.*`: draws a
view's instances from the mesh cache with GT4's lighting per vertex (the bodies of `gsn_models.comp`), at 1x,
**forward, in GT4's order, with GT4's own depth** (RISKS R3): each instance through its own projection
(`InstanceData::projection`), GT4's Z tests, its Z clears between depth partitions kept as the view's passes. Its
depth then equals the GS path's and ties resolve the same, so coplanar layers, blended draws and second passes at
equal Z need no class rule and no G-buffer. Two conditions make the ties exact:
- GS Z is computed with the same operations as `gsn_models.comp` (shared shader code), so equal Z stays equal;
- the items of an instance are drawn in GT4's run order, one draw (or one index range) per item, so layers inside
  one instance keep their order. The mesh cache's welding across items merges only bitwise-equal vertices, which
  changes no Z.
The scene target's other draws between the view's passes (R5's clears, Z-only draws and the full-screen draws at
0.47 of the order) keep their place in that order; whether B7 or the GS path draws them is B8's question (R5).
- **The design** (lane B): B7 is the GS path with one change. Each tagged model run of a bound, valid, uncut view
  is drawn from the mesh cache by a parity vertex stage instead of from `gsn_models.comp`'s per-frame output, at its
  own place in the list's command order, through the same pipelines and `gsn.frag`. So R3 and R5 hold by
  construction: GT4's order and depth, the Z clears and full-screen draws untouched, alpha as GT4 leaves it.
  - **Per item**, `ViewOnList::items` (CONTRACTS a.8): its job (the per-frame constants: matrix, scale, offset,
    clamp, guard, origin, and the lighting rows 200..216) and its draw (state and texture in this list), filled
    in `sceneView()` beside B6's ids (lane A's file, lane A reviews). The render thread doesn't search by tag.
  - **Shaders:** the transform, the lighting chain bodies and GS Z move from `gsn_models.comp` into includes
    (`gt4_transform.glsl`, `gt4_lighting.glsl`). `gsn_models.comp` keeps its decode and calls them, and
    `modern_parity.vert` calls the same functions on the mesh's object-space vertices and the item's job: it binds
    the frame's batch words already on the GPU (`gsn_models.comp`'s Jobs and Words) and reads the job's constants,
    lighting rows and tables there, so no per-item buffer is built. Only position, ST, colour and normal come from
    the mesh. Same operations on the same values, so Z is equal (R3), and the normals' scaling is made the same
    operation in both.
  - **Quirks kept** (lane B): the dot model with the next-normal bit (0x0290) adds the next vertex's N.L terms in
    batch order, so the mesh cache keeps a next normal per vertex for such items and welds on it too. Flat shading
    takes the last vertex's colour: the provoking vertex set to last (`VK_EXT_provoking_vertex`; without it the
    triangles are rotated, below). The clipping renderers' per-triangle hiding is part of the per-triangle decision
    below.
  - **The GS path's vertex order and per-triangle decisions** (lane B): the rasterizer interpolates Z from the
    vertices in their order, so a strip's odd triangles turned to one winding gave a different Z at coplanar
    layers. Parity draws each triangle in the GS path's order (the mesh's turned triangles turned back, from
    `Mesh::turned`). The renderers' per-triangle decision is made per vertex for the triangle ending at it, as
    `gsn_models.comp` does: facing (a precise, shared `homogeneousFacing`), the hidden test, the fog word with its
    raise, and ADC. The raw vertex carries the two previous strip vertices and its strip position; `gt4FogWord`
    (`gt4_transform.glsl`) is shared with `gsn_models.comp`. The ADC bit and the flat colour reach `gsn.frag`'s
    parity variant from the provoking vertex, the strip's last.
  - **One device feature:** `VK_EXT_provoking_vertex`, logged at device creation and in the parity line. Without
    it the triangles are rotated (v, a, b): ADC stays exact, Z may differ.
  - **Measured** (`race_boot.cap`, all 4,914 frames, precise shaders, against `render.scene = original` in the same
    build, 17 frames sampled): at the `original` preset 16 identical and one 2 pixels apart by 1 level; at medium
    8 identical and the rest at most 0.11 % apart (mostly 1 or 2 levels, at most 10), from medium's depth- and
    history-based scene stage (occlusion, motion vectors, the temporal resolve), not the draws, which are equal at
    `original` (being traced). 2,916,756 of 2,978,633 items from the mesh cache; 1,264 views not substituted (cut
    or invalid); VAL clean. Raw vertices are 76 bytes (52 before the previous-vertex words): 8 MiB held at the
    capture's end against the 512 MiB budget; the arena (76 + 16 MiB host-visible) exists only with
    `render.scene = parity`.
  - **Where:** `GsnScene::executeRange`, at a Draw command holding such runs, only with `render.scene = parity`
    (`--set` only, never in a preset, named in the settings text as a parity check, not a mode). Without it
    nothing changes. A Draw can merge several jobs, of several instances (`model()` merges consecutive jobs of
    the same state, target, texture and view), so the replacement is per job: each item carries its job's own
    vertex range, and `executeRange` splits a Draw at job boundaries where one instance has a mesh and another
    falls back. Merging is not changed, since the split now runs in every night race (F2b).
  - **Fallbacks**, counted as `viewFallbacks` reasons: an instance with mesh 0 (pending, skinned, key-frame blend)
    draws that run from `gsn_models.comp` as today; a view not valid, or cut, stays wholly on the GS path. The
    mirror falls back unless it binds and validates as the main view does.
  - Not in B7: B8's substitution, jitter and TAA (parity is 1x).
- **Order of work:** B6's `items` (inert without the parity path); the shader split together with "precise"
  arithmetic (CONTRACTS b.10), which moves `ORIG`: a re-baseline under section 2's rule, after the PCSX2 near-tie
  comparison and `PERF`; then the parity path; then the acceptance. The split alone is not a pure
  refactor: moving identical expressions changed 15 of 30 `race7` frames through the driver's fusion.
Depends: A2, A3, A10, B4, B5, B6, R3, R5.
Acceptance: `PARITY` on the race, flyover and event captures at 1x; Z equal to the GS path wherever both draw (a
debug compare pass counts mismatches: 0); `VAL`; `viewFallbacks` reasons listed; `ORIG` and `PRESETS` identical (the
parity path runs only with `render.scene = parity`).

**B8: The ordered substitution, hybrid (M, lane B).** `render.scene = hybrid` draws each substituted view's tagged
items in place, in GT4's command order, by modern stages: B7's vertex stage (GT4's transform and Z, exact, so every
depth test and blend that follows still holds) and a hybrid fragment stage. The scene stage at the Light command is
where modern lighting combines, reading side channels the items wrote. Untagged draws (GT4's clears, Z partitions,
the full-screen draws at 0.47, particles, the sky) stay on the GS path, in place. There is no compositor at the
Light (the coordinator's decision, 2026-11-08): R5 showed that draws between the view's passes depend on its depth,
colour and alpha as they stand (particles Z-tested against the cars, t76 added by alpha mid-frame).
1. **Shippable mesh resources:** device-local vertex and index buffers per mesh, uploaded through staging when first
   drawn, retired after the frames in flight when the mesh cache evicts the mesh, under a budget. Each item stays
   one indexed draw; its CPU cost per item is reported into B6c's span. B7's host-visible arena stays for
   `render.scene = parity` only.
2. **The hybrid pipelines and the side-channel passes:** the parity vertex stage and a hybrid fragment stage
   (`gsn.frag`'s parity variant in B8's first form, so hybrid at 1x equals parity). The side channels (view-space
   normal, material id: B6's `MaterialId`, so B10's classes reach the scene stage; view-space depth, from the
   instance's own projection, independent of GT4's partitioned Z; motion) are not extra attachments of the GS
   passes: GT4's blended draws use dual-source blending, which allows one colour output
   (`maxFragmentDualSrcAttachments`), and every GS pipeline would need a variant per attachment count. Instead a
   side-channel pass redraws the items with depth test EQUAL, as the scene stage's motion-vector pass does, through
   the parity vertex stage (the same Z to the bit), writing no colour or depth to the GS target:
   - **one pass per depth partition:** GT4 clears Z between its partitions (R5, about 2.7 clears a frame; the
     course's near and far passes), so a single pass at the Light would find only the last partition's Z. A pass
     runs just before each Z clear of the view's depth target in the command order, and at the Light, for the
     items drawn since the previous one. The Z clears are R5's classes (sprites with Z test ALWAYS writing Z, the
     alpha-test-never Z-only fills), recognised in the plan, not by a heuristic in the draw loop;
   - **each pass** first writes material 0 ("GT4's colour as it is") and clears the other channels where this
     partition wrote Z (Z differs from the value the clear left), then redraws the partition's items with EQUAL.
     So a later partition overwrites an earlier one exactly where it drew, as GT4's colour does after the clear,
     and a pixel whose final Z an untagged opaque draw wrote (the sky, opaque billboards) keeps material 0, never
     a stale channel or the normal of the car under it;
   - **veiled:** the same pass then draws the partition's blended draws (items and untagged, from the GS vertex
     buffer as the motion-vector pass reads it) with test GEQUAL and no depth write, marking the pixels a blend
     lies in front of (a bit, or the coverage in a small unorm channel). A material id is an integer and can't
     blend, so writing 0 there would cut a hard-edged hole in the relighting under faint smoke. How a veiled pixel
     is lit is B10's;
   - the cost: the items drawn once more in total a frame (the motion-vector pass is 46 us on `race7` high), split
     over two or three passes, and no change to the GS passes or pipelines. Made only in hybrid.
3. **The scene stage reads the side channels:** in B8 only a debug view of them; B10 lights with them.
4. **The read switch** (R9 as a per-reader table): a list holding a game read of the scene target whose reader is
   marked GS-only draws that view on the GS path (its items left out), so the read sees the PS2's picture; counted
   per reader. R9 fills the table (luminance probe, photo tiles, car icons, flare samples), GS-only by default until
   a reader is checked. A reader of Z alone is exact already (GT4's Z is kept). A reader that reads every frame must
   be checked before hybrid goes into presets, or hybrid would never show on those frames.
5. **`render.scene = hybrid` accepted:** never saved while it is in check form (as parity); in presets when B10 makes
   it worth it (the coordinator's call).
Depends: B7, E1; R9's table for step 4's entries (the switch itself doesn't wait). Acceptance: `ORIG`; hybrid on REF
at 1x within `PARITY`; differences confined to the scene rectangle; `VAL`; `PERF` against `original` (GPU and the
front end); the Odin's device run (the provoking vertex, logged).

**B9: Mirror, env-map, reflection, showroom and photo views (M).** The road reflection is no view today
(`kInstReflection`, out of the binding, A6): B9 first measures its readers on a wet course and makes it a
`kViewReflection` view only if a substituted view needs it redrawn (a.5 then allows that one nesting, or the main
view splits around it). Depends: A6, B8. Acceptance: `PARITY` per view role on its captures.

**B10: G-buffer, deferred lighting, sun, material classes (L).** `platform/render/modern/gbuffer.*`,
`lighting.*`; sun from the batches' light matrices (qwords 201..216) through `objectToWorld`; classes from lighting
bodies (`lighting.materials = pbr`); prelit course colour as baked lighting (through N2b's section normalisation
only if N2 reopens). Its G-buffer is B8's side channels: B10 adds the lighting at the scene stage and the hybrid
fragment stage's material classes, not a separate geometry pass. R3's measurements decide which items write the
side channels (opaque Z-writing ones) and how blended and equal-Z items are lit (B22). Open: how a pixel veiled by
an untagged blended draw (B8 step 2) is lit: relight the surface under it, accepting that the veil's colour is
already mixed in, or skip it above a coverage. Depends: B8, R8, R3 (night,
showroom and the destination-alpha passes measured). Acceptance: `VAL`; no NaN or Inf (debug pass counts them); `PERF`
budget 4 ms GPU at 1440 x 1080 on the RX 7900 XTX; the owner's review against PCSX2 stills of the REF scenes.

**B11: Cascaded shadows and contact shadows (M).** Course casts onto cars only. Depends: B10. Acceptance: `VAL`;
`PERF(<= 1.5 ms)`; no shadow on the course where its baked shadow already is (review images).

**B12: GTAO from depth and normals (S).** Replaces the horizon AO on modern views. Depends: B10. Acceptance:
`VAL`; `PERF(<= 0.5 ms)`; `GT4_LIGHT_VIEW`-style debug view.

**B13: SSR, car cube probe, sector probes (M).** Depends: B10. Acceptance: `VAL`; `PERF(<= 1.5 ms)`; paint on the
showroom car reflects the probe (review).

**B14: Sky, aerial perspective, froxel fog (M-L).** Shares the froxel grid with F1. Depends: B10, F1. Acceptance:
`VAL`; `PERF`; GT4's painted skies kept where they have clouds (review).

**B15: Modern mode (M).** On B8's ordered path (ARCHITECTURE section 4.1): GT4's post effects skipped between the
markers; pipeline bloom, exposure and tone map on linear light; HDR10 metadata. Depends: A5a, B8. Acceptance:
`ORIG`; `VAL`; HUD drawn after the tone map (until E6, at render scale); HDR output measured on the HDR test
pattern.


**B16: Free render scale and jitter for modern views; upscaler interface (M).** With B8's in-place drawing, the
render scale is the scene target's: B16 decides a free scale for the scene target as a whole. Depends: B8.
Acceptance: `ORIG` (integer scales kept on the GS path); `VAL`; render scale 67 % at 4K upscaled by FSR 2 without
TAA ghosting on the flyover (review), motion vectors exact (velocity debug view).


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
minutes at the Odin settings (1280 x 800); timings reported; `VK_EXT_provoking_vertex` reported from the line the
renderer logs at device creation (for B7's parity path), that line named in the owner's device list. Without it
parity's triangles are rotated and its Z may differ: a B7 finding, not a C6 failure (parity is `--set` only). That
line waits for B7, which adds the log.

**C7: Host job system (M).** `platform/core/jobs.{h,cpp}`: work-stealing pool, fibre-free. Depends: none.
Acceptance: `CTEST` (stress test, TSan build on Linux); used by B5 and B2b with `PRESETS` unchanged.

**C9: POSIX game link (M).** Shared memory between a Linux host and the game under Wine. Depends: C2.
Acceptance: live session on Linux x64 under Wine reaches a race.

**C10: Reference captures and the PCSX2 procedure (S).** `docs/modern/CAPTURES.md`: how to record each REF
capture (`GT4_HOST_RECORD=PATH,FROM,COUNT`, with `GT4_HOST_RECORD_SCENE=1` once A3 lands; from game start so model
bytes are in, R7), how to take PCSX2 software-renderer stills at the same scene points (menus, showroom, paused
race, Photo Travel), where they live (`build/caps`, `build/refs`, never committed). Depends: none (scene re-records
after A3). Acceptance: REF.txt complete on the main dev machine; the procedure followed once by a second agent.

**D10: Loading (M; lane C, moved from lane D on 2026-11-08).** From the audit (section 4.5, not in the repo): every
disc read is a synchronous `ReadFile` loop under one global mutex (`iop.cpp`), on an image opened
`FILE_FLAG_RANDOM_ACCESS` (no OS read-ahead); stream reads run inline in `sceSifCallRpc` with the frame interrupt
held; GT4's volume files are inflated on the game thread; no load time is measured anywhere.
- Measure first: time `Pipe::open` 004B0E28 and `GranTurismo4::LoadingMode` 0x00104E30 for a race load and the
  main menus.
- Then: memory-map the image, or read ahead per stream on a worker (a stream's size is known at open); drop
  `FILE_FLAG_RANDOM_ACCESS`; give BGM and movies their own readers. If the loading screens turn out to be paced by
  whole frames, run the frame clock faster on loading screens outside `original`.
- **First measurement** (lane C, `GT4_LOAD_STATS`, desktop, lane A's race route): the race load takes 0.70 s, of
  which the disc is about 0 (the image is in the OS cache, 1.5-1.9 GB/s), the game's CPU 0.14 s and waiting 0.55 s:
  the game draws every other VBlank, the "paced by whole frames" case. The other loads are under 0.5 s. The I/O
  costs would show on a cold cache or the Odin (SD card, Wine's file I/O, FEX), which the desktop can't measure.
- **Agreed order and targets:**
  - (a) the I/O part now, safe everywhere: drop `FILE_FLAG_RANDOM_ACCESS`, give music and movies their own file
    handle (off the loads' mutex), and read each stream ahead on open. Target: no regression on the desktop
    numbers, and a cold-cache check on the desktop (an uncached copy of the image, before and after) showing it
    helps where it should; the device numbers come from `GT4_LOAD_STATS` in the owner's device test (R4's list).
  - (c) the faster frame clock on loading screens, outside `original`: `LoadingMode` (0x00104E30) timed first, and
    the clock runs fast only while the game is in it, and never during anything with game time the player
    watches (the countdown, replays, animated menus): if `LoadingMode` ever runs under one of those, the clock
    follows the state the player sees, not the function. Target: the race load's waiting from 0.55 s to under
    0.2 s, with a race's lap times and AI behaviour unchanged after a faster load.
- Acceptance: the load times (race and menus) reported before and after, against those targets; `ORIG` unchanged in
  its picture and the game's pacing (faster reads shorten `original`'s loads too, which is the point; the faster
  frame clock stays outside it); saves and replays unaffected (session test).

**D11: Latency (S-M; lane C, moved from lane D).** The input-to-photon budget D12-D16's filters count against. From
the audit (section 4.6): two frames in flight today, and the pad sampled every host frame. Design: one frame in
flight when the GPU has room, and the pad sampled just before the game's frame tick. Acceptance: input-to-present
latency measured (mean and p95) before and after, with the target lane C proposes from that measurement; `PERF` no
worse; `ORIG` unchanged.

**C11: One-command release pipeline (M).** From one commit, one command makes the Windows zip and the linux-arm64
tar (clean release builds through `tools/release_game.py` and the host build), a `BUILD-INFO` (the gt4-native and
decomp commits, the build machines' tool versions), checksums, and a draft release note. It makes files only and
publishes nothing. Depends: C2, C4, C6. Acceptance:
- both archives from one clean commit, their checksums verified; a dirty tree is refused;
- one game in both: `gt4_game.exe` built once by `release_game.py`, the same SHA-256 in the zip and the tar, named
  in `BUILD-INFO` with the commits;
- each host built from an empty folder (`tools/build_windows.cmd`; `tools/build_linux.sh` for linux-arm64), each
  with its glibc and architecture check, as `package_game.py` does;
- the draft note made from CHANGELOG.md's newest section, the archives' names and SHA-256s, and a line saying the
  linux-arm64 package is untested on the device until the owner's test (R4's eleven points);
- no Sony or Polyphony asset in either (docs/RELEASE_POLICY.md).

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

**D5: Audio mixer on the host (L).** SPU2 image and RAM records (`stream.cpp:766-768`), `platform/audio/`.
The ENVX/ENDX replies come from a game-side voice model, in the same SPUP call as today (R16: not even one frame
late). The model keeps key state, ADSR / ENVX, and the address through the ADPCM loop and end flags at the voice's
pitch (ENDX): `audio.cpp` without decoding, interpolation, volume, reverb and the mix. The host mixer plays the same
voices from the register image, with any latency. The envelope and address code is one source compiled into both,
so the game's replies and the host's voices agree by construction. Depends: A10 (stream.cpp), R16. Acceptance: the
host mix within -60 dB of the game-side mix on a session's audio over 60 s (`original` interpolation); the
driver's key-on counts within the run-to-run spread of today's (2-8 %; one frame late gives 1.9x), and its main
re-key gap still at 2 frames with about today's count (a stale reply shows as a peak at 3 or 4), so no voice frees
early.

**D6: Vehicle state discovery (L).** RISKS R14, then the fields of `scene::Vehicle`. Depends: R14. Acceptance: per
wheel load and slip plotted over a lap, consistent with the car's motion; documented addresses.

**D7: Wheels through LGDEV (L).** `iop.cpp:278-290` DFP emulation to `SDL_Haptic`. Depends: R15. Acceptance: a
wheel works in GT4's menus and races with its force feedback.

**D8: Physics force feedback (M).** Depends: D6, D7. Acceptance as the audit's section 4 states, with session
tests. (D10 and D11 moved to lane C.)

**D9: Spatial audio (L, lane D).** The audit (section 4.2, design 3) gives the design but no acceptance; this is it
(the architect, 2026-11-08; lane D reviewed). `audio.spatial = original | stereo | hrtf`, behind `audio.mixer =
host` (D5). `original` in every preset while `audio.mixer` defaults to `game` (spatial does nothing there and would
read as unused, as `audio.interpolation` does); the change that makes `host` the default (after the Odin run, the
coordinator's call) gives low to ultra `stereo`. `hrtf` is the player's choice (it assumes headphones).
- **Tags:** each key-on is tagged game-side with the entrant whose sound object starts it (`RaceCarSound::playStart`
  003924D0, `EngineSound::AttachCallback` 00391930, the two engine sample objects per car from 003923D0; lane D's
  design note maps a voice to its entrant in GT4's sound driver). Untagged voices (UI, BGM, crowd, narration) stay
  a stereo bed, mixed as `original` mixes them.
- **Positions:** the entrant's `Vehicle::bodyToWorld` (A9) and the main view's camera as the listener. Spatial
  audio needs no instances, so with full records off it asks for vehicles only (`kOptionSceneRecords` 2, as D14
  does), and that mode then also carries the main camera: the `ViewBegin` of the main view (with its `Projection`,
  no instances), kept by the 0033EAB8 copy whatever the mode and sent in the kick's block (CONTRACTS a.1, lane A).
  A tagged voice with no position (records off, an old capture, outside a race) plays in the bed, counted.
- **GT4's own pan:** lane D's note settles first whether GT4's driver already pans and attenuates car voices by
  position (`voll` / `volr` from the car relative to the camera). If it does, spatial takes a tagged voice's level
  without GT4's pan (the power sum of `voll` and `volr`) and applies its own pan and distance curve, so nothing is
  panned twice; the distance check below then measures spatial's curve alone.
- **Rendering:** `stereo` pans with constant power and attenuates by distance; `hrtf` renders through an HRTF
  library and dataset with a licence lane C checks (no Sony data). Doppler from the `Vehicle` velocities is lane
  D's call, limited and off in `original`; occlusion is not part of D9.
- Depends: D5, A9, A3 (the main view), D13 if the output device decides `hrtf`. Acceptance:
  - `ORIG`; `DIFFTEST` (the tags come from game code); with `audio.spatial = original`, a session's `audio.wav`
    bit-identical to D5's host mix.
  - Tags, on a race session with six cars: at least 99 % of the car key-ons tagged, a car key-on being one whose
    sample start address lies in the race's car sound banks (the regions STRP loads for the entrants, logged at
    load), so the rate doesn't depend on the tags; no BGM, UI or narration key-on tagged; counted per frame
    (`voicesTagged`, `voicesUntagged`, `voicesNoPosition`). Gaps the note names (e.g. a shared tyre-squeal voice)
    are listed with their share.
  - Direction: a session in which an AI car passes the player in the chase view; in each 100 ms window where the
    car's view-space |x| exceeds 2 m, the sign of its tagged voices' left-right level difference matches the sign
    of x in at least 95 % of windows, for `stereo` and `hrtf`.
  - Distance: as that car pulls away on a straight, its voices' level falls with distance along the attenuation
    curve within 1 dB.
  - Balance: a lap's integrated loudness (EBU R128) within 1 LU of `original`; the bed alone identical to
    `original`'s untagged voices.
  - No audio drop in the host summary; the audio thread's time per buffer reported (`PERF`); with vehicles only,
    frame pacing unchanged within noise against records off, and `STREAM`'s extra bytes reported (as D14).

**Input as in current racing games (D12-D16; the owner, 2026-11-08).** The goal is the control quality of GT7, Forza
and ACC on top of the PS2 mapping, never instead of it. The constraints:
- Each feature is an `input.*` setting (`settings_input.cpp`). The `original` preset keeps today's behaviour byte for
  byte (`tests/input_map.cpp`). The other presets take the defaults each ticket gives.
- Everything happens on the host, before GT4 sees the pad. GT4's controller layer has no dead zone and a linear
  curve (`spikes/D1-bindings.md`), but the car's steering does not turn the wheels until the steering byte is
  past about 160, a quarter of a side's travel (lane D measured it at a standstill, 2026-11-08). So the host
  shapes its response knowing what lies after it, and that band is compensated outside `original` (D12). The one
  exception to "on the host": the band's compensation is applied game-side, while a race is loaded (D12).
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
  - **Applied game-side (`kOptionSteerBand`) while a race is loaded, never in menus outside it** (lane D with lane
    C). Menus read left X too, and a menu threshold in untraced code can't be ruled out. So the host sends g as game
    option 7 (`kOptionSteerBand`, per mille; 0 in `original`), and `scePad2Read` (`runtime/iop.cpp`) remaps left
    X only while `vehicle_conductor()` finds the race's cars, next to the car's own band. Menus outside races read
    exactly what they did. The one gap: a race's own pause menu sees compensated bytes, at most 37 bytes further
    out than the stick.
  - **The same at speed** (lane D, a GT-mode run to 166 km/h): the command is 0 for bytes 92-164 at 98-160 km/h,
    and the edge values match the standstill run byte for byte. So the default of 29 % is final (68866e4). Whether
    a Driving Force Pro's input passes through the same path is checked in D15, with the same log column.
  - It applies to everything that goes through the response: the stick, the keyboard ramp, and in D15 a wheel in
    `native` mode.
- Keyboard steering: a digital key ramps the axis at `input.steer.key_rate` (full lock in 0-500 ms, default 150 ms;
  `original` 0, today's step) and returns at twice that rate.
- **Speed-sensitive steering**, `input.steer.speed_reduction` (0-80 % less lock at 200 km/h, default 0), comes
  after D6 (speed, `D + 0x5C4`). It is set only after measuring how GT4 already reduces lock with speed (the
  steering angle at the wheels, `D + 0x468`, at full stick from 0 to 200 km/h), so the host never doubles it.
  A first, indicative reading (lane D): at about 120 km/h the wheel angle per command unit is roughly an eighth of
  the standstill one, so GT4 already reduces lock a lot. The clean measurement needs full-stick pulses on a
  straight at speed; until it shows room for more, the default stays 0.
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
- **Per-device settings** (lane D's design): a device's section of `input_profiles.ini` may override any `input.*`
  setting under its own name without `input.` (`steer.deadzone = 3`, `haptics.strength = 60`, `response =
  original`); anything not overridden takes the global value. A port's response and haptics use its gamepad's
  overrides; pad 1's keyboard takes the keyboard section. The pages reach them through `pad_devices.h` (no SDL in
  the interface: the devices, get / set / clear an override validated by the option table, bindings with
  `checkProfile`'s refusals, reset), and through lane M's records as the keys `input.device.<key>.<setting>`. A
  device key holds no dot (the host splits the key there); clearing an override is a set with an empty value, so
  lane M's records need no new kind. A new binding is captured on the host, since the game sees only the PS2 pad's
  state: `beginCapture` / `cancelCapture`, and the captured input goes through `checkProfile` like any binding.
  - **`original` stays the PS2 behaviour:** while a gate is at `original` in the globals (`input.response`,
    `input.haptics`), every device override under it is ignored. A device may still choose `original` under a
    `modern` global. `detectPreset` ignores device overrides, as it does `ui.*`.
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
- **The records it needs:** with full scene records off, the host asks for vehicles only (`kOptionSceneRecords` 2,
  CONTRACTS a.1): one block at the kick with the `Vehicle`s, no tick deferral, nothing sent outside a race. The
  `original` preset stays at 0.
- Depends: D2, D6, A9, D13 (per-device). Acceptance (also: `ORIG`; with vehicles only, frame pacing unchanged
  within noise against records off, and `STREAM`'s extra bytes reported):
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
  - live on each available wheel: centring, the loss of weight at understeer, kerbs; `dfp` behaves as D7;
  - whether the D12 band applies to a wheel in `dfp` mode (the steer command at `D + 0x454` against the wheel's
    position, as D12 logged it), so `native` and `dfp` steer alike near the centre.

**D16: Gyro steering (S-M).** `input.gyro = off | steer` (default `off` in every preset). The controller's roll,
fused from gyro and accelerometer (`SDL_SetGamepadSensorEnabled`), steers. Full lock at `input.gyro.range` (30-180
degrees, default 90), recentred by a button chosen in D13's profile. It adds to the stick before D12's response.
Depends: D12, D13. Acceptance:
- `CTEST` (fusion on synthetic sensor data: half the range gives half lock; drift under 1 degree a minute at rest
  after the bias calibration);
- live, manual: a DualSense, a DualShock 4 or a Switch Pro.

### Lane E: UI, and the previous agents' unfinished work

Lane E has no agent (2026-11-08). E5, E6, E7 and E9 are lane M's, with lane C reviewing; E8a and E8b became M11's
pages (docs/modern/MENU_DESIGN.md); E1-E4 are done.

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

**E8a: Input pages (M; now M11's Controls page, lane M, drawn natively).** The devices list (D13), each device's
bindings, response (D12) with a live plot of stick against output, haptics (D14), gyro (D16) and wheel (D15) pages.
Each page is shown when its ticket has landed. Saved per device in D13's profile. It shows GT4's own controller
screen as where accelerate and brake are set (D1). Depends: E5, D12, D13. Acceptance: every D12 setting can be set
and reset from the pad alone; a profile set in the menu matches the file; the menu works with the keyboard only.

**E8b: Audio page (S; now M11's Audio page, lane M).** Depends: D5.
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

**N3: Lamp bands suppressed (S-M).** The lamps light the road through the clustered lights (F2 in `hybrid`, F3 in
`modern`), the street lamps from F2b. R10 and R11 found two things on the road (`spikes/R10.md`, `spikes/R11.md`;
draw numbers are those of replay 2514, and the draws are found by what they are, not their numbers):
- **the lit-road layer** (draws 305-308): world-fixed and without a lighting chain (measured), so probably the
  lamps' light baked into the course (PCSX2 to confirm). It is course geometry: modeled strips in course instances
  (tagged jobs, renderers 1 and 4, lighting body 0) with a state of its own ((Cs - Cd) As + Cd, z GEQUAL, no z
  write, its own textures). There is no call site to bracket (A5b), so it is found by a B6 material class, as F2b's
  glow bit is: lane F's classifier, from that signature plus a computed check of the texture (no hash list). It is
  skipped exactly when the renderer draws the lamps' own light on the road (F2 in `hybrid`, F3 in `modern`), so the
  road gets the lamps' light once: never twice, and never dark. With F2 off (a `hybrid` preset without the night
  relight), the layer stays. `original` keeps it.
- **the light-image glow** (draws 325-326: GT4's reflection view of course geometry into target 71, blurred and
  spread over the road; F2b's step 0): R11's first band candidate, skipped only if R11 finds it makes the bands.
- **Step 0:** on a night capture with records, the lit-road layer's instances (Cmd, set, model) and their
  materials. The signature alone isn't unique: other course draws share it (lane F, replay 2514: textures 18957,
  22853, 27077 / 27078 beside the layer's 20203 / 20204). So step 0 finds a property computed from the texture or
  the mesh that separates the layer from them (coverage of the road surface, the texture's grain), checked on both
  courses (`r11_csr5r.cap` the second); a false positive would skip ordinary course geometry. Then how the
  GS back end skips a modeled draw of that class, given that B6's ids are known at the view's `ViewEnd` (designed
  with lanes A and B, before the skip is written).
Depends: R11, A7, B4, B6, F2, F2b. Acceptance `R5` on the lamps capture: band ratio at most 1.3 in every frame (the
GS path's value for comparison); suppression counter > 0 on the frames where the GS path draws the layer; on the
second course, no other draw skipped; `ORIG` unchanged.

**N4: Warning-light tint suppressed (S-M).** The flare (and, in `modern`, glare) draws R12 identified are skipped
between their markers (A5b); each light keeps its own bloom (pipeline bloom on linear light, F4). Depends: R12,
A5b, B4, F4 for `modern`.
- **Only the draws onto the picture.** The game reads the flare's samples back (00497EF8 / 00497F48) to set each
  flare's strength; that read must not change. A bracketed draw is skipped only when its colour target is:
  - the main view's principal target (A10's binding, known when the flare pass runs, after the main `ViewEnd`);
  - a target the display records showed in the last 8 VSyncs: in a race, the 640 x 224 field buffer the flare's
    glows are added to, after the scene's copy and before the HUD (lane A's `a5b_r5.cap`). The size rule alone
    missed it: the field buffers are 448 tall only when the menus have drawn them, so a mid-run replay marked
    none and a live run about half;
  - or a screen-sized target by `sceneRead`'s rule (bw x 64 >= 512 and height >= 384).
  Each skip is counted by the rule that caught it. The glare is skipped the same way, in `modern` only.
- **Where.** The front end marks the draws as it builds the list. It sees each `Marker` in stream order, so a
  list cut before `FrameEnd` is marked too. A `Draw` gets the bracket it is in (flare, glare); lane F writes it in
  `gs_frontend.cpp`, lane A reviews. The GS back end skips a marked draw when the setting asks for it (one check
  in its draw loop; lane B reviews). `GT4_GSN_LIMIT` still counts every draw. A tally of skipped draws per bracket
  gives the suppression counter.
- **Setting.** Part of `lighting.night = enhanced` (the night relight replaces GT4's veil), not a key of its own.
  So it follows F2's rule: `--set` only, in no preset, until N3 lands. `render.scene = modern` adds the glare
  (B15).
- Acceptance: `CTEST` (the marking on a synthetic list: markers, draws, targets, a cut list); `ORIG` unchanged;
  the flare readback's bytes identical with the suppression on and off (`GT4_READ_HASH`), so GT4's own flare
  strengths don't change. Every other read is compared too, on a night route with the markers (A5b): GT4's
  luminance probe (00330AE8) reads the picture, and if it follows the flare's bracket it sees a frame without the
  veil, so the game's night exposure would differ from the PS2's. A read that changes goes on R9's reader list for
  a decision (lane B's point). `R5` on the warning capture with the markers (A5b): blink-synchronous tint amplitude
  below 0.002 in Oklab b and a, outside the lamp discs (the GS path's amplitude for comparison); the lamps still
  visibly blink and bloom.

**F2: Night relight on the GS path (M).** At a Light command in `hybrid`: lights gathered from the view's `Light`
records into the light list, clusters over the scene rectangle, view-space positions and normals from GT4's Z,
the lights added in the scene stage with the albedo estimate of CONTRACTS.md c.3. While F2 lights the road, N3
skips the lit-road layer (draws 305-308), so the lamps' light reaches the road once. Until N3 lands,
`lighting.night = enhanced` is set by `--set` only, in no preset, so a release never lights the road twice. Three
other lanes' files change, each reviewed by its lane: `gsn_light.comp` (lane B's scene-stage shader) takes the
added light; `sceneRecordsWanted` (`settings_scene.cpp`, lane C) is also true with `lighting.night = enhanced`,
since the game sends no lights without records; and `cmake/Host.cmake` (lane C) links the light sources and
compiles `lights_cluster.comp` for the host. Depends: A7 step 1a, F1, B4; the street lamps F2b.
Acceptance: `ORIG`; `VAL`; `PERF(<= 1 ms)` at 1440 x 1080; on a night capture lamps light the road and cars near
them (review against PCSX2 stills: the scene otherwise unchanged); on the lamps capture with the car moving, N1
shows no new step above 4 % (N2's bar) from the added light.

**F2b: Course lamps (M, lane F; lane B reviews).** The street lamps' glows are course billboards (A7 step 1b), not
mesh geometry: GT4 builds their quads every frame facing the camera. So the lamps come from the billboards' own
entries, game-side, with exact centres and sizes, and the host keeps those whose material its classifier calls glow.
- **Step 0's answer** (lane F, `r5_ss_ab.cap`; `spikes/R11.md`): the lamp glows are one draw a frame onto the scene
  target, with the signature blend (Cs - Cd) As + Cd, alpha test EQUAL 128, z test GEQUAL with z write, textured,
  renderer 3, body 0. Lane A then traced its jobs to `BillboardSet__render` (A7 step 1b).
- **The glow class** is that draw-state signature plus a computed check of the texture (B6's `MaterialKey`, never the
  host texture id). Another night course may use another texture, or the signature may match something else there:
  the acceptance checks a second course.
- **The lights:** each `kSrcBillboard` candidate whose `kInstBillboard` instance has the glow class becomes a
  `kSrcCourseGlow` light; the rest are dropped and counted. A lamp named both by `entries2[]` (A7 step 1a) and as a
  billboard gets one light: the game record's position and blink, the billboard's size. They are one lamp when they
  lie within the billboard's half-size of each other in view space.
- Extracting lamps from course meshes is not built: no glow geometry was found in them. It comes back only if a later
  course shows glow geometry the billboards don't cover.
- **How it is built** (lane F's design on B6):
  - **The scene split** is on whenever `lighting.night = enhanced`, not only with `render.scene`: B6's ids exist only
    with the split. One settings function decides it beside `sceneRecordsWanted` (`settings_scene.cpp`, lane C
    reviews), read where `GT4_GSN_SCENE_SPLIT` is read today (`gs_host.cpp`, lane A reviews). The split changes no
    pixel (A10), and `original` has night off.
  - **A texture summary at decode:** when a texture's level 0 is decoded, the front end keeps 16 bytes (mean colour
    weighted by alpha, centre-to-rim luminance, the fraction of texels at alpha 0x80) under its identity, retired
    with it; `GsFrontend::textureSummary(identity, out)` reads it. Lane F writes it in lane A's file, lane A reviews.
    It summarises the game's texture, never a pack's replacement, so the verdict doesn't depend on the pack.
  - **The classifier** (`platform/render/modern/glow_classifier.cpp`, lane F): the signature, then the summary's
    scores, set the material's glow bit and keep its colour by `MaterialId`. Each verdict is logged once, with the
    texture's size and scores. Installed through `GsFrontend::setSceneClassifier`, lane A reviewing.
  - **The render thread's access:** each list with a scene frame carries a `shared_ptr` to the scene cache
    (`FrameList::sceneCache`, CONTRACTS a.8), so the cache's lifetime follows the lists. A lookup of an id the cache
    has dropped returns nothing and is counted; the cache keeps an entry at least 8 frames after its last use, more
    than the lists in flight.
  - **The lamp pass** (lane F's files, a call in `nightFinish`, lane B reviews): for the main view, each
    `kSrcBillboard` candidate is kept or dropped by its instance's material class, the kept ones already in view
    space. The mirror stays unlit (c.3). Colour is the glow texture's mean (kept with the material's class) times
    the candidate's own colour when A7 step 1b sends one (the billboard item's, if its entry has one), else the
    texture's mean alone; range and intensity per class in `light_sources.cpp`, tuned against references under
    `lighting.night`.
    Only drawn sections get lights, as GT4 culls them.
- The classifier meets these materials for the first time on the first night frame, so that frame judges a burst
  of new keys; the summaries are per texture identity, so it is cheap after that.
- Depends: A7 step 1b, B6 (`kInstBillboard` instances per-frame, lane B), F0, F1. Acceptance:
  - `CTEST`: the classification and the promotion on a synthetic frame (glow and tree billboards, a lamp also in
    `entries2[]`);
  - on the Route 5 night capture, every lamp-head glow drawn has a light whose projection lands inside its quad
    (misses = 0 over the run) and no light sits where no glow is drawn. "Drawn" means the glow's draw was
    submitted that frame, not that pixels survived: a lamp head behind a building is still lit, correctly. The
    lamps' count per course section is reported;
  - on a second night course (one of lane F's sessions), the same: no miss and no light where no glow is drawn;
  - the gather's dropped count (`GatherStats::dropped`, lights over the list's cap) is 0 on the capture: a dropped
    lamp would show as a dark patch that the miss check, which only asks that the light exists, can't see;
  - tree billboards (non-glow) light nothing, counted as dropped;
  - a lamp named by `entries2[]` and as a billboard gets one light;
  - the per-frame cost reported, and `PERF` with night enhanced against
    night off in a night race: the split now runs there, so B6's front-end cost lands there too (lane A), measured;
  - `ORIG` unchanged; with scene records off nothing runs.

**F3: Clustered lighting in the deferred and forward passes (M).** Depends: F1, B10, B22. Acceptance: `VAL`;
`PERF`; the same lights as F2 in `modern`, with G-buffer normals.

**F4: Bloom and eye adaptation on linear light (S).** The pipeline's bloom and histogram exposure on the modern
scene. Depends: B15. Acceptance: `VAL`; exposure follows tunnels and night sections without a step (N1 step metric
on the sections capture); the warning capture's tint stays below N4's bar.

**F5: Wet roads (M).** G-buffer modifier, puddle masks, spray from emitters. Depends: B10, B13, B18. Acceptance:
`VAL`; wet variants only; review against the wet capture.

---

### Lane M: in-game settings menu (MENU_DESIGN.md)

Native settings pages in GT4's menu flow, not an overlay: the logic, navigation, input focus, GT4's menu sounds and
frame pacing in the game process; the pixels and the settings in the host. The tickets, their files and their
acceptance in full are [MENU_DESIGN.md](MENU_DESIGN.md) section 7.1. Every ticket: `CTEST`, `REL` where the game
changes, `ORIG` and `STREAM` with records off and no page opened. In order:

| Ticket | Size | Depends |
|---|---|---|
| **M0** Menu flow spike (`docs/modern/spikes/M0-menu-flow.md`) | S, 3 days | none |
| **M1** UI and settings records (`ui.h`, `protocol.h` 16, 17, 68, host commands 6, 7 new and 5 documented; the version serialised with P1 and P3a) | S | none left |
| **M2** Page engine (`platform/ui/`) | M | none |
| **M3** UI renderer and asset pipeline | M | E5, E6 |
| **M4** Settings bridge | S-M | M1, M2, E1 |
| **M5** Game-side page host (`runtime/menu/`; the pad focus in `iop.cpp`, lane D reviews) | M | M0, M1, M2 |
| **M6** Options entry (a copy of 00109D00: both ways into GT4's Options reach `startProject`; the hub opens over GT4's Options, whose project runs the menu update the pages tick in) | S | M5 |
| **M7** Native pause page (hooks in copies of 0038B428, 0038B2B0) | M | M5, M0 |
| **M8** Start on the GT mode map | S | M5, M0 |
| **M9** Host wiring; the old menu deleted (the pad chord kept; host commands 2, 3 retired) | S-M | M3, M4, M5 |
| **M10** Style guide and the first asset set (the owner's sign-off) | M | M3 |
| **M11** Controls, Audio and HUD pages (E8a, E8b, E9's rows) | M | M4, M5, D12, D13, D5, E6 |
| **M12** Pointer input and accessibility options | S-M | M9 |
| **Q1-Q8** QoL options (MENU_DESIGN 7.1), each with its own spike and a setting off in `original` | per ticket | M1-M9 |

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
  - **First, depth targets read as textures** (lane B, found with P2a, 2026-11-08). The race's Z buffer is sampled
    as a PSMZ24 texture at its own address and width (bp 0 and 0x8C0, width 10, about 1.7 a frame in a race;
    over the captures race7 1,000, race_boot 16,754, menus_boot 34,175, before and after P2a). The direct path
    takes only colour targets, so these reads decode `gs_memory`, which never holds the drawn depths: a picture
    bug of the GS path, `original` included. Fix: a Z-format texture whose surface is a depth target reads that
    target (the depth image, by P2a's key), its depths converted to the GS's Z24 texel: the host keeps float depth
    (`VK_FORMAT_D32_SFLOAT_S8_UINT`, `gsn_scene.cpp` `kDepth`), so each texel becomes the 24-bit Z the GS would
    hold, laid out as a PSMZ24 texel (R, G, B the Z bytes, alpha from TEXA as for CT24), then goes through the
    usual texture path; not a plain bind. It changes pictures, so it is its own commit: the draws that sample it
    named (game function, from `GT4_GSN_DRAWS` on `race7`), what the picture shows wrong, and an `ORIG`
    re-baseline under section 2's rule.
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
  - reinterpretation fallbacks counted by kind (`GT4_GSN_FALLBACKS=N` logs them): for P2a and P2b, no new kind and
    no more than at the parent commit; after P2c, 0 on `race7`, `race_boot` and `menus_boot.cap` (the menus);
  - the R9 readers' bytes unchanged (`GT4_READ_HASH` on a live session to a race: CarIconMaker, the luminance
    probe).

**P3a: Block links as offsets (S-M, lane A).** The frame list's DMA-tag block links become plain offsets
(`runtime/frame.cpp`'s walk, pgl's packet). Depends: none. It changes what the game sends, so it takes the next
`GT4_LINK_VERSION` and converts the captures; P3b does both again later.

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
  code changes. P3a and P3b each take the next `GT4_LINK_VERSION` when they land (M1 took 4). P1 needs neither a
  version nor a conversion: it only removes host code for kinds the game no longer sends. Captures don't carry
  `GT4_LINK_VERSION`, so the version alone won't refuse an old capture: the replay reads the new record layout,
  from converted REF captures or through a reader kept for the old one, and anything else is refused by its
  layout.

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
| 2. Hybrid ships (weeks 8-10) | A7 step 1a, B6c (before B8's release), B8, B9, F2, F2b, N3, N4 (hybrid parts) | `render.scene = hybrid` in presets low-ultra; `R5` gates in hybrid |
| 3. Modern lighting (weeks 10-18) | B10-B15, F3, F4, A7 step 2, A8 (N2b/c and A11b only if N2 reopens) | `R5` gates in modern; owner review |
| 4. Resolution and effects | B16-B22, F5, E6-E9, P2b, P2c, P3b | per ticket |
| 5. Optional | B23, C6 on devices, research track | per ticket |

Lane M runs beside the phases: M0-M9 first, then M10-M12 and Q1-Q8 (MENU_DESIGN.md 7.3: 10-14 weeks for one agent).

Critical path: A0 -> A1 -> A10 -> A2/A3 -> B7 (also needs B1 -> B3 -> B4 and B5 -> B6) -> B8 -> B10 -> B15.
Calendar estimates are guesses from the sizes: hybrid in about 10 weeks with lanes A, B and C in parallel.

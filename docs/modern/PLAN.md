# Implementation plan

Status: design for review. Decision (owner, 2026-11-07): modernisation comes first; the 0.1.1 release waits.
Architecture: [ARCHITECTURE.md](ARCHITECTURE.md). Contracts: [CONTRACTS.md](CONTRACTS.md). Spikes: [RISKS.md](RISKS.md).

Sizes: **S** = 1-3 days, **M** = 1-3 weeks, **L** = 1-3 months, for one agent. References at `main` a80cfca.

---

## 1. Rules

1. **main only.** No branches on origin but rule 10's three kinds. Rebase on `origin/main` before every push; never
   force-push.
2. **Small commits.** One ticket is one to five commits. Each commit builds, passes ctest and does one thing.
3. **Review before push, by someone other than the author.** The author commits locally, runs the ticket's acceptance,
   and sends the reviewer the commit hashes and the clone path over arc. The reviewer fetches from the author's clone
   (`git fetch <path> HEAD`, read-only), reads the diff, reruns the acceptance, and answers OK or the changes needed.
   Only then does the author push. The reviewer never edits the author's clone. A rebase needs no new OK when `git
   range-diff` shows the patch unchanged ('='), no conflict was resolved, and the new main overlaps the patch's files
   only in hunks the patch doesn't touch; the push message states the range-diff line (the coordinator, 2026-11-09). A
   new OK stays required when the patch changed or a conflict was resolved. The gates the rebase needs follow the
   rebase rules as before.
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
9. **Comment- and docs-only commits** (the coordinator, 2026-11-08). A commit that changes only comments or docs,
   even inside a file that triggers a gate (`game_src`, `gfx.h`, shaders, section 2's `DIFFTEST` files), skips the
   build and run gates: the release builds of rule 4, `CTEST`, `ORIG`, `DIFFTEST` and the ticket's other runs. CI
   and the review (rule 3) still apply. The author shows that the diff touches no code: `tools/comment_only.py`
   over the commit's range exits 0, and the commit message says so. The tool (lane U) compares each changed C,
   C++ or GLSL file's old and new versions with comments stripped; allows only `#` lines in
   `tools/eeref/known.txt` (its entries, reasons included, are data, and lane U reviews them); passes docs; and
   fails anything else as not provable. Reading `git diff -w` line by line is not enough: a `//` comment ending
   in a backslash swallows the unchanged line below it, and a `/*` and a `*/` on two changed lines comment out
   the unchanged code between them.
10. **Land continuously** (the owner, through the coordinator, 2026-11-09). Push each step as soon as its gates and
    its review pass: no batching, and no finished commit held locally for a later step. Work that isn't finished goes
    to main in small steps behind a setting or switch that is off by default (as `render.scene = hybrid`,
    `lighting.night` and `ui.entry = integrated` went), with its gates; a feature that shows nothing yet stays off in
    every preset until the ticket that makes it visible. Nothing stays local-only for more than a day. Three kinds of
    branch go to origin, and no others: work that failed its acceptance and is parked, as `parked/<ticket>-<what>`,
    which the ticket's PLAN entry names with its hash; debug or measurement code that must never reach main, as
    `debug/<lane>-<what>`, kept until its ticket closes and then deleted; and a backup of work on its way to main, as
    `wip/<lane>-<what>`, deleted when it lands and never older than a day. A branch is deleted only after its commits
    are checked reachable from main or a `parked/` branch, unless it is `debug/`. Main stays green: a step that fails
    its gates never goes to main.

---

## 2. Acceptance vocabulary

Every ticket's acceptance uses these names.

| Name | What runs | Pass |
|---|---|---|
| `CTEST` | full ctest, Release | all pass (77 = skipped is a pass only where the ticket says) |
| `ORIG` | `tools/frame_diff.py --baseline OLD NEW --preset original` on the REF captures, every frame; plus `host_replay` | identical frames. **Re-baselines** (a standing rule, the coordinator, 2026-11-08): `original` means matching the PS2, so a commit that fixes a correctness bug in the GS path may move `ORIG`, on these conditions: it is its own commit; the frames it changes are compared with PCSX2's software renderer and shown closer or equal; its `PERF` is checked; the new reference (captures and hashes) is recorded and announced to every lane with the commit's hash; and it is listed in CAPTURES.md's re-baselines, so a comparison never crosses one by mistake. `ORIG` gates compare against the latest reference from that commit on. Refactors and performance changes never move `ORIG`. **Per device class** (lane B's Odin diff, 2026-11-09): `ORIG` is bit-identical on the reference device class (x64, the desktop's AMD); on another class the same replay differs from the reference by the GPUs' texture sampling and filtering. Measured on the Odin (Adreno 830 under Turnip; lanes B and C, `tools/orig_compare.py`): `race7` (fields) 76 % of pixels identical, 24 % within 1-2 levels, 0.02-0.03 % over 2 (on the HUD's glyphs), worst 18-19; `race_pe2_prog` (progressive) at `original` 0.10-0.49 % over 2, spread over the 3D scene, worst 6-15. **Interim, until GS-exact texel sampling lands** (B24; the coordinator, lanes B and C): `ORIG` runs at `original` only (other presets' differences are `PRESETS` facts), and across classes a frame fails on: any pixel over 32 levels; structure, measured by `band_measure.steps()` on the device-against-reference difference (rows 45-72 %, the detrended column step, the median over six bands of rows): a frame fails when the boundary median at 32-GS-pixel boundaries is at least 0.25 levels and over 5 times the median elsewhere (lane F, 2026-11-09, on 14 clean day pairs and synthetic strips: clean pairs at most 0.05 levels and a ratio of 2.7; 1-level strips, and strips of a random 0-3 levels each, at least 0.9 levels and a ratio of 27; the owner's night bands are 2-5 level offsets on that grid; the night pairs still to come; on compressed video the ratio is only a yes or no); and, once a capture has its baselines from the every-frame run, the share over 2 levels above twice its baseline's highest frame, or the 1-2 share more than 5 points above its baseline. Until a capture has baselines, only the cap and the structure test judge it, and its histogram, share over 2 and worst pixel are reported. Within one class, `ORIG` stays bit-identical between builds: that is the device's regression test. After B24, the strict rule replaces this: at most 2 levels on every pixel, any allowance set from B24's measurement. It is measured on pixel values, not hashes: every frame once per class (to set its baselines, and at C6's close), the device's frames and the reference frames compared on one of the owner's machines (game content, never committed or served); routine device runs compare a sample of the differing frames on the desktop. `tools/orig_compare.py` (lane C) gives the verdict from `band_measure.steps()`'s output; `band_measure.py --steps --against` shows the detail. The first: the commit that makes the shared transform and lighting shaders precise (CONTRACTS b.10, B7), its near-tie pixels compared on PE race 2's countdown (`race_grid43.cap` against PCSX2 software stills; PCSX2 can't replay a capture) |
| `PRESETS` | the same at `original`, `low`, `medium`, `high`, `ultra`, and the test scene's `tests/scenarios/features.json` captures, run with `tools/host_session.py run --frame-locked` (123ba3f8: two runs of one host give 45 of 45 identical captures, whatever the machine's load). The REF captures never run FSR 2 or the display side's TAA (the GS scene has no motion vectors, so `resolveConfig` turns a selected FSR 2 into FSR 1 there, and the GS scene resolves its own TAA), so a change to those is gated on these captures, base against new | identical frames |
| `PARITY` | `render.scene=parity` against the GS path at 1x on the REF captures, per frame | >= 97.5 % of pixels identical, >= 99.97 % within 4 per channel (the GPU-models step's bar, docs/NATIVE_V2.md:90) |
| `NATIVE` | the native GS renderer above 1:1, which `ORIG` can't see: `render.gs` sets one renderer's scale (`original` draws 640 x 448, one render pixel per GS pixel; `native` draws at the render resolution; lane A, 2026-11-10), so `ORIG` exercises it at 1:1 only. Step 1, now: `tools/device_run.py --strip-steps DIR` (lane C; `strip_steps`' on32, steps on the 32-GS-pixel grid, against on40, in GS pixels from the content's width; one JSON line per frame) per frame on the REF captures and `lap_ssr5f_boot`, at exactly 2x on both axes (output 1280 x 896, preset `original` with `render.gs = native`, quality native and `aspect.ratio = window`, so the picture fills the output at 1280 x 896: with the default aspect, 640 x 448 is pillarboxed to about 1195 x 896, not 2x), every enhancement off. Step 2, once the night strips are fixed: on the progressive REF captures only (`race_pe2_prog`, `lap_ssr5r_prog`: one 640 x 448 frame target), that 2x frame reduced by a 2 x 2 box to 640 x 448 and compared with `original`'s, every frame. Field captures stay under step 1 alone: their output frame is a mix of the two fields (`display.field_mode = clean` still weaves both where nothing moved), so no box makes it one field; covering them would need a capture of the field target before display, new work, for if step 2 needs field coverage (lanes B and C, 2026-11-10; `native` sizes field targets from the output's lines, lane B's reading, to be measured) | step 1: a frame has strips when 6 or more steps sit on the 32 grid and at least 1.5 times the 40 grid's; no frame's on32 more than max(3, 25 %) above its baseline (counts vary by a few between commits that don't touch the light draw; lane A), the baselines per capture in `docs/modern/native_baselines/` from `tools/native_strips.py` (lane C), a commit that moves one past that margin re-baselining with its reason as `ORIG` does; and one rule for flagged frames on every capture (lane C, 2026-11-10): a frame the strip rule flags fails `--check` until someone has looked at it and listed it in its baseline file as judged, with the counts it was judged at and what it is (`# judged: frame 2300 on32 8 on40 5, the kerb's blocks at x 843-958; no strips`); a judged frame is exempt from the rule but not from the margin, so it fails again if its on32 moves past the margin from the judged value (lane A), and a regression bringing the strips back on a listed frame still fails. On the captures holding GT4's night light draw (`lap_ssr5f_boot`, `lap_ssr5r_prog`, the night REFs) the judgement must find it isn't the light draw, so the strips 73cfad8 fixed would fail at once. The first run (902 frames) flagged seven: the five looked at so far are ordinary edges, race7 59 (the gauges, a kerb, a wall), race_grid43 110 and 330, garage_r6 3000 and lap_ssr5r_prog 2300 (the kerb's blocks); menus_boot 6500 and 17300 are judged before the baselines are committed; step 2: per-pixel tolerance and the structure test as `ORIG`'s cross-class rule, set from its first run. Gates every change to the native GS path that isn't 1:1-only |
| `VAL` | REF captures with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, synchronization validation on | zero messages |
| `PERF(x)` | `tools/perf_report.py` on `--profile-json` of the REF captures against the baseline build; for an FSR 2 or display-TAA change, of the test scene's captures (`--frame-locked`), where those passes run | the named stages within x |
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
| B. Renderer | `platform/render/graph/`, `platform/render/modern/`, `platform/scene/mesh_cache.*`, `material_table.*`, `scene_cache.*`, `platform/gs/vif1.*` (the mesh cache's input; lane A reviews, since A1 and A10 use its runs; P1, lane C, removes its stream path), `pipeline.cpp`, `gsn_scene.cpp`, `vk.*`, shaders (except the display stages' and upscalers', lane C's from B16); for P2, the target, page and palette parts of `gs_frontend.*` and `gs_memory.*` (lane A reviews) | `renderer.cpp`, `main.cpp`: B2 **first** (day one) |
| C. Platform, ARM64, tooling | the settings table (from lane E, 2026-11-08): `platform/sdl/settings.{h,cpp}`, `settings_table.h`, the presets and the shared parts (`settings_display.cpp`, `settings_render.cpp`, `settings_effects.cpp`, `settings_scene.cpp`). The per-module files stay with the lanes that add to them (`settings_input.cpp`, `settings_audio.cpp` lane D; `settings_lighting.cpp` lane F; a future `settings_ui.cpp` lane M), lane C reviewing each change for the table's rules (preset membership, `original` unchanged, `unusedReason`), `cmake/`, `tools/build_*`, `tools/package_game.py`, `tools/frame_diff.py`, `tools/perf_report.py`, `platform/core/`, `link_host.cpp`, `docs/modern/CAPTURES.md`, `platform/gsnative/texture_pack.*`, `platform/sdl/session.*` and `tools/host_session.py` (the test harness), `runtime/link.cpp` (lane A reviews every change: its async reads and tick deferral); the display stages and upscalers (from lane B, 2026-11-08, the coordinator; lane B reviews): `platform/render/display_feature.*`, `platform/render/upscale/` (the upscaler interface and its backends, with `fsr2.*`, `fsr2_shaders.cpp` and `ffx_compat.h` moved in), `easu.comp`, `rcas.comp`, `taa.comp`, the upscaler options in `settings_render.cpp` | `main.cpp`, `renderer.cpp`: C1 **after B2**. `pipeline.cpp` (lane B's): B16's jitter call and `resolveConfig`'s `selfResolved` rule only, lane B reviewing |
| D. Systems | `platform/input/`, `platform/audio/`, `runtime/memcard.cpp`, `platform/saves/`, `runtime/iop.cpp`, `runtime/audio.cpp` | `main.cpp`: D1 **after C1**. `protocol.h`: D2 **after A1**. `stream.cpp`: D5 **after A10** |
| E. UI | no agent; its files moved on 2026-11-08 (the coordinator): the settings table to lane C, `overlay.*`, `settings_menu.*` and `layer2d.*` to lane M. Its remaining tickets E5 (SDF font), E6 (2D layer), E7 and E9 (HUD) go to lane M with their files (the coordinator), lane C reviewing | `gs_frontend.cpp` `textureFor` (E2), `gsn_display.frag` (E3): day one, **before A10** |
| F. Night lighting | `include/gt4/render/light_list.h`, `shaders/light_list.glsl`, `platform/render/modern/lights_*`, `tools/frame_metrics.py`, the Route 5 tickets | `gsn_scene.cpp` scene stage: F2 **after B4** |
| M. Menus (docs/modern/MENU_DESIGN.md) | `platform/ui/` (page engine, UI draw list, UI renderer, SDF text), `platform/render/modern/layer2d.*`, `platform/render/overlay.*`, `settings_menu.*` (deleted by M9), `runtime/menu/`, `assets/ui/`, `tools/ui_assets.py`, `include/gt4/ui.h`, the menu-flow `game_src` copies (00109D00, 0010FE40, 003C3820 `RacePause::update`, and 00109B38, the menu game object's update where the pages tick); lane C reviews the host-side UI files | `protocol.h`: M1 took link version 4 (ba5c358); a later change to what the game sends takes the next one. `runtime/link.cpp` (lane C's, lane A reviews). `runtime/iop.cpp` (lane D reviews). `main.cpp` (lane C's order) |

`settings.cpp` / `settings.h`: E1 splits the option table into per-module files on day one; after that each lane
adds its options in its own file (`settings_scene.cpp`, `settings_input.cpp`, `settings_audio.cpp`,
`settings_lighting.cpp`) and the shared table only lists them.

**The link version** (`protocol.h`) moves when a change to what the game sends would make an older host misread it,
or count it as bad: a new record type is one (R6's origin record takes 5). A new kind inside an existing record
that an older host provably skips, with no error, nothing counted as unknown or bad, and no change to its output,
keeps the version (D9's `GT4_SPU2_TAGS` in `GT4_REC_SPU2_REGS`: lanes C and A checked `HostMixer::record`). The
commit names the check. A newer host must handle the kind's absence (an older game) as its own case, never as a
zero value.

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

**A3b: Camera cuts, `kViewCut` (M, lane A; lane B reviews).** CONTRACTS' `kViewCut` (the camera moved discontinuously,
so temporal history resets) is set by nothing today (lane B, 2026-11-09), yet TAA, B16's upscalers, B16c's reset,
F2b's night level and N3's coverage gate all rely on it. A mid-race camera change, a replay's track-side cut or the
start's flyover to the grid is caught only when the view drops out for a frame, and history ghosts across the cut.
Both sources, each logged by which one fired: (a) the game's own signals, read where our copies already hold the race
camera (`cam` = method 0xD0 right before the main view's `ViewBegin` in 0033EAB8's copy, which Photo Travel's preview
reaches through 0033E368 with role `kViewPhoto` keeping its own history, and A12's 0033FD88 copy): the camera object's
mode fields and cut pulses (lane A's step 0, a night B-spec race: the object is the same all session and `cam + 0x9BC`
stays 0, so neither was a signal). `+0x1A0` (1 and 2, `+0x138` following it) and `+0x1A8` (0 and 1) step through
B-spec's three-view cycle, `+0x1A8` rising being the rear view, a 180-degree turn with almost no move; a rising edge
(0 to 1, never the falling one or any change) of `+0xAC8` or `+0xAEC` marks every trackside TV cut and every replay
cut (the pulse is usually one frame but can stay up: on a replay cut `+0xAEC` stayed 1 for about 0.67 s over a still
camera); `+0x140` steps through the start sequence's camera changes. Every one of the 43 jumps had one of these, and
each of these landed on a jump. Session 4 (a B-spec race and its replay, 17,486 main views) found no new kind of false
flag: a fast replay pan at 10.5-12.9 degrees per VBlank, the field of view at most 1.7 % without a signal, and gaps of
2 and 3 VBlanks predicted across without a flag. Photo Mode's camera, Photo Travel's preview and a deliberate 30 fps
race frame complete the list. Read through a hook `gt4_scene_main_camera` before the view begins; (b) a safety net in
the runtime's `ViewBegin`, which has `worldToView` and the Projection: per (role, `pass`), kept across frames, the
camera predicted from its last two per VBlank (`FrameBegin`'s vblank, so a 30 fps race frame, a hitch or a dropped
frame isn't read as a jump; those counted apart), a cut when the residual exceeds 1 m per VBlank, the view direction
turns by more than 90 degrees, or the field of view changes by more than 3 % (step 0's margins: on B-spec frames with
no signal the residual's 99.9th percentile is 0.081 m and its maximum 0.43, the field of view at most 1.5 %; on A-spec
frames the residual at most 0.72 m and the field of view 0.12 %; cuts are 3 to 240 m, 4 to 180 degrees, or 10 to 850
%. The turn is 90 degrees, not 20: a crash swung the chase camera up to 46.7 degrees per VBlank for 7 continuous
frames, while the turn test exists for the rear view, 180 degrees with a residual of 0.48 m. A-spec's view changes
pulse `+0xAEC`, like the TV cuts, and one of them, 0.74 m with a 20 % field-of-view change, is caught only by the
field-of-view test and the pulse), the net's history reset when it flags, since a cut shows on two consecutive frames.
`kViewCut` is set once per cut, on the first frame of the new camera: the frame after a flag isn't tested by the net
(one sample since the reset), so the consumers' just-rebuilt history isn't thrown away again (a cut on that very frame
is left to the game signals), and the game signals and the net firing on the same frame are one flag, its causes
counted per cut (lane B); a view absent last frame counts as a cut, as the host does now. Hunting every switch alone
would miss some (replay cuts, the flyover, the pause and photo cameras); a geometric test alone misses short cuts
(bumper to cockpit is about a metre, to be measured in step 0) and needs tuning against speed. Step 0 (measurement): a
debug game logging per frame the camera object, its entry, position, view direction and field of view, over a race
with every view selected in turn (chase, bumper, cockpit, roof, TV), the start's flyover, a replay with its track-side
cuts, a pause and resume, and Photo Mode's camera (photo shots from a replay) and Photo Travel's preview (001BD438, a
path to observe, not a place to hook); it says which signal shows each visible cut and gives the residual distribution
on continuous frames at speed for the thresholds. Step 1: the hook in the two copies (0033EAB8's, which Photo Travel's
preview reaches through 0033E368, and 0033FD88's), the runtime test, `kViewCut` on `ViewBegin`, a counter per cause
(camera object, entry, residual, angle, field of view, absent), and CONTRACTS' line saying who sets it. The flag fixes
nothing until each consumer reads it, so step 1 also has (lane B): `GsnScene` resets the TAA history of a Light's
target when any main view drawing into that target has `kViewCut` (in split screen either half's cut resets both, one
frame of the other half's history, the safe side, until TAA keeps history per view as A12 asks); the night level does
the same per (target, role, `pass`); B16c's `DisplaySceneState::reset` follows the anchor's Light. Consumers read the
flag from `FrameList::scene`, which only the list carrying `FrameEnd` has, so a Light in a list cut before it (a.8:
the flare's asynchronous read) sees no flag; those are counted. Depends: A3, A12 (it touches 0033EAB8's and 0033FD88's
copies, so it follows A12). Acceptance: `REL`; `DIFFTEST`; `ORIG` and `STREAM` unchanged with records off; on step 0's
sessions every visible cut flagged, and no flag on continuous frames at speed over a lap (target 0: a false flag costs
one frame of history, a missed cut a ghost), and none on resuming from a pause whose camera didn't change; no TAA
ghost on the first frame after a view change, on a capture with one. Ahead of B16c's part 4, which needs it.

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
  items, about 7 draws a frame) into a 64 x 32 colour target with its own Z (PSMZ24), tested and written, and in
  part straight into the main view's own target (lane A: two targets a frame); that fits
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

**A12: Scene records in split screen (M, lane A). Done** (lane A, ae033d2, 2026-11-09: every acceptance item met; by
day at 4:3 and 16:9 every frame opened with both halves' views and scissors; at night on C.S. Route 5 both cars' lamps
on their glow points in both halves; at `settings-playtest.ini` 0 validation messages, hybrid in both halves, GT4's
own night look). A split-screen race sends no scene records at all (lane A, 2026-11-09, measured: a 600-VBlank 2
Player Battle capture has no `FrameBegin`), so 2 Player Battle runs none of our copies of 0033DEA8, 001BD438 or
00217058. Measured (lane A, 2026-11-09, `debug/a-a12-path`): 2 Player Battle draws through
`RacePS2Base::render_world_split` (0033FD88), called twice a frame, once per half, by `RaceSplitBattleBase::render`
(0032FCC8) through the object's vtable at +0x368; 0033EAB8 never runs in the race. Per half 0032FCC8 sets the half's
rectangle (`func_001054C0`), the camera and HUD panel index (`func_00372080`), then draws (0033FD88 with arg2 always
1, so not the half) and the panel (vtable +0x358); half 0 is drawn first, top or left. `D_006184F0` picks the layout:
0 top and bottom, anything else side by side (GT4's widescreen switch). Outside split mode (mode >= 2 with the global
flag clear) it calls `RaceBasic::render_world` through vtable +0x348, which reaches our 0033DEA8 copy. LAN and net
battles show one player per console through the single-player slots, not split screen, and their records are untested
(no route). Every modern feature then leaves split screen as `original` draws it, and no feature reaches it. **Step 0,
done** (lane A, 2026-11-09): a 2 Player Battle of about 900 VBlanks at `settings-playtest.ini`'s settings (hybrid,
night enhanced, FSR 1, the host mixer) on the playtest release (main-25e9b8c, windows-x64, the desktop's AMD card),
under validation: no messages, no errors, both halves drawn with their HUDs, a clean exit. Not covered: the Odin
(Turnip, arm64) and the sound (the run had no audio out). GT4 lays the halves out side by side at `aspect.ratio =
16:9` and top and bottom at 4:3, its own widescreen layout. Two `game_src` copies: of 0032FCC8, which opens the frame
before the halves (one `FrameBegin` for the list, `kFrameLampsOn` from the course flags as 0033DEA8's copy does) and
tells the runtime each half and its rectangle before its draw; and of 0033FD88, which sends `ViewBegin` / `ViewEnd`
around its course and cars with the course bracket, the reflection bracket (it calls method 0x378,
`render_reflection`, when course flag 0x01000000 is set, bracketed as `kInstReflection` as in 0033EAB8's copy), step
1a's glow lights before 003B88C8 and A7 step 2's car lamps after its glow pass (method 0x3F0) or at its place when
course flag 0x400000 skips it, every car's lamps in each half's view. Each half also draws its own mirror (method
0x3C0, `render_back_mirror`, 00341E50, which already opens a `kViewMirror` view through our hooks); in split screen
that view gets its half's `pass` and `kViewSplit` too, so two mirror views a frame each say which half they belong to.
Each half also draws its car's environment map (0033FD88 calls 0044E9A0, whose copy sends `kViewEnvFace` and
`kViewEnvCopy` with `pass` = the face): inside a half they carry `kViewSplit` and `pass` = face | half << 8, so a
consumer keyed by (role, `pass`) keeps the two cars' maps apart without inferring the half from the views' order (lane
A, 2026-11-09). A5a's HUD markers go around each half's panel draw, so the HUD's regions are known per half. The
halves are two `kViewMain` views drawing into one target with disjoint scissors, new for the host (until now a bound
view had its own target or the whole of one, CONTRACTS a.5.7), so every feature that works per view reaches both, and
every pass at a view's Light command (hybrid's substitution, the night relight) keeps to that view's scissor:
`ViewBegin::pass` is the half (0 the first drawn, 1 the second; 0 in a single view, as today), `scissor` its rectangle
(side by side or top and bottom, as GT4 lays them out), and a new `ViewFlags` bit, `kViewSplit` = 8, marks both, so a
consumer that keeps one state per role can tell. CONTRACTS' `ViewBegin` says so in the same commit (the 'per half in 2
Player Battle' comments corrected in 19b3472). The consumers that keep state per main view are checked in the
acceptance: temporal history (TAA, B16's upscalers) per view, not per role; B16c's seam; F6's player per half (its
followed-entrant field). Depends: A3, A7 step 2. Acceptance: `REL`; `DIFFTEST`; `ORIG` and `STREAM` unchanged (records
off); on a 2 Player Battle capture (Arcade, Mode Selection, 2 Player Battle, pad 2 from the session), every frame
opened, two `kViewMain` views with `kViewSplit`, each with its scissor, and two mirror views each with its half's
`pass` and `kViewSplit`, at 16:9 and at 4:3, both cars' lamps on their glow points, and `render.scene = hybrid`
running in both halves without validation messages, each half substituted inside its own scissor only (lane B reviews
it); split frames have no Light (their scene target is 224 lines, both halves inside it, under the front end's
`sceneRead` 384-line minimum), so no scene stage runs on them at all, neither the night relight nor the occlusion or
the enhanced grade, and 2 Player Battle at night shows GT4's own look; lane B's guard on a target holding more than
one main view (3e6fa5e) is the second line, which can't fire today. The scene stage in split screen needs a Light per
half first: a split case in `sceneRead` (lane A's front end), a Light per `kViewMain` scissor in such a target, the
single-view size threshold kept as it is, the per-view follow-up's first step; and each half's picture its own, except
where TAA, which keeps one history per target, reprojects across the split line, until the per-view follow-up clamps
`gsn_taa`'s reprojection to the half's scissor (lane B). Priority: ahead of lane A's queue (the coordinator,
2026-11-09); A7 step 2's battle check is done with it (both cars' lamps in both halves).

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
reported; the same span measured on the Odin (C6b); `ORIG` and `PRESETS` identical.

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
Parked (lane B): `parked/b7-fused-dump` (03b7c7a), B7's fused GS path with the dump tooling for the PCSX2 comparison.

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
front end); C6b (the provoking vertex, logged).
- **Acceptance status** (lane B, 2026-11-08, on eea04c2): `ORIG` identical against b.10 alone (`race7` 1-100,
  `race_grid43` 1-700, `race_boot` 3800-4914, every frame, largest error 0); `PARITY` on `race_grid43` and `race_boot`
  (differences of 1 or 2 levels, inside the scene rectangle); `VAL` clean; night items checked on lane A's `a6r_r5`.
  `race7` has no scene records, so hybrid draws nothing there; in its place (the coordinator), `PARITY` on lane C's
  `race_pe2_r` (PE race 2, fields, 300 frames, scene records on lane A's `race_gt_pe2.txt` route; f90214e, judged from
  frame 20): 279 of 281 frames identical, largest error 1; 445,543 of 459,838 items from the mesh cache, 0 differ.
  `race7` stays as it is. Still owed: `PERF` from a quiet window (the CPU side: front end +31 %, render thread +27 %
  outside one), and the Odin run (C6b). R9's two every-frame readers (the luminance probe, the flare samples) are
  GS-only by default and are checked before hybrid enters a preset (step 4).

**B9: Mirror, env-map, reflection, showroom and photo views (M).** The road reflection is no view today
(`kInstReflection`, out of the binding, A6): B9 first measures its readers on a wet course and makes it a
`kViewReflection` view only if a substituted view needs it redrawn (a.5 then allows that one nesting, or the main
view splits around it). From R6 (lane A, on main since bb66156):
- **The showroom car** (garage, showroom, dealers) is `kViewMenu` (00217058), its supersampling passes
  `kMarkJitterPassBegin`/`End` pairs inside the view (`garage_r6v.cap`: two views a frame, 3 and 5 pairs). B8's
  role filter keeps every `kViewMenu` view on the GS path, so B9 lifts it for the showroom car only (menu views
  holding jitter-pass pairs, or a sub-role); the other menus' 3D stays on the GS path. B9 substitutes each pass
  with its offset (`kOriginParts`' super, the PS2's average: the `PARITY` mode), and may offer the first pass
  alone with the repeats and their accumulation skipped (the modern AA in place of the supersampling), measured
  against the each-pass mode as a known term.
- **Photo Travel's preview** is `kViewPhoto` (pass 0) with a `kMarkDof` pair after it (`pt_r6v.cap`, 69 frames).
  Before substituting it, B9 identifies what the race frames don't have: 2 instances a frame outside the view, 1
  untagged job in it and 7 runs after an instance (lane A's guess: photo mode's own draws 003C7BB0 / 003C73C0
  after the depth of field). The shot sends no frames yet: whether its tile and jitter passes become views
  (inside their frames, `kViewPhoto` with pass = the jitter pass and the tile in a spare field) or one view with
  TAA is B9's design.
- Depends: A6, B8. Acceptance: `PARITY` per view role on its captures (`garage_r6v` in the each-pass mode,
  `pt_r6v`).

**B10: G-buffer, deferred lighting, material classes (L, lane B).** Modern lighting at the scene stage from B8's
side channels, behind `lighting.materials = pbr`: in no preset until its acceptance and the owner's review;
`original` keeps today's path, and hybrid with `original` materials is identical to today. Its G-buffer is B8's side
channels, not a separate geometry pass; R3's measurements decide which items write them (opaque, Z-writing) and how
blended and equal-Z items are lit (B22).
- **Channels:** B8's (normal, material, view depth, veiled) plus one, RGBA16F in hybrid only: GT4's interpolated
  lit vertex colour, before texture and fog. The GS modulates per pixel after interpolation, and an N.L body's
  vertex colour is its lighting (`gt4_lighting.glsl`), so GT4's pixel divided by this channel is the texture, to
  rounding. Kept as GT4's colour, each counted: saturated pixels, fog (undone first or skipped), veiled pixels and
  untagged equal-Z draws over items, colours under a floor, TFX other than modulate.
- **Where it is** (2026-11-10): the channel is on main, written by hybrid's side passes (`GT4_SIDE_VIEW=lit` shows
  it), and its first reader is the night relight: with `GT4_NIGHT_SIDE=1` in hybrid, `gsn_light.comp` takes the
  albedo as the pixel over the channel and the normal from the normal channel, where an item's surface is the pixel's,
  nothing veils it, the vertex colour is at least 8 of 255 and the pixel is under the target's ceiling; elsewhere its
  estimates. Off, frames are identical to before (hybrid, hybrid with the night, the night on the GS path) and
  `ORIG` holds. Blended layers that write no Z (markings, decals) give the channel their own colour in a pass after
  the surfaces' (only layers that blend their own colour in; not shadows, glows, or N3's skipped items), run only
while the night reads the channel; a layer's texture alpha isn't read yet. Owed before the switch goes and the night
  enters a preset (the review, a fresh agent): the cases
  above that the reader doesn't keep or count yet (fog, undone or skipped; tagged blended layers folded into the
  pixel, the lit-road layer where N3 doesn't skip it; TFX other than modulate and untextured items; untagged equal-Z
  draws over items); channels left stale where a boundary had no reset (B8's limit, now feeding a divide); the
  fourth attachment's cost in hybrid, measured, the Odin included (four colour attachments is Vulkan's guaranteed
  minimum: a fifth channel needs another pass or a packed format). The first never-dark measure with it (N3;
  `lap_ssr5f_boot`, 30 frames from 8,000 to 20,000, the baked light skipped): our lamps give a median 0.63 of GT4's
  light under the skip mask per frame (0.20 to 1.22), and 41 % of the 32-pixel blocks under half of GT4's: the
  lamps' strength and spread are next (F7's street-lamp profile, then the coverage gate).
- **Classes** (RISKS R8): prelit (body 0, 0x0B0, 0x1508, 0x16F8) is never relit, only shadowed and occluded, so the
  course only darkens; car paint (0x10A8, 0x12C8); glossy (0xCA8, 0xEA0); diffuse N.L with roughness from the
  squarings; projected (0x1608) kept as drawn; unknown bodies keep GT4's colour.
- **Lights:** by day, lanes 0 and 2 of the N.L bodies, fixed directionals (0.00 degrees across a view's batches),
  plus the ambient (204), with per-object colours; the directions from the batches, no game record (`kSrcSun`
  reserved). The lane colours fold in each material's colour: B10 splits them into a per-instance material colour
  and per-view light colours by a stated rule (first candidate: lane colour over the view's brightest lane colour),
  and gets them to the scene stage through the side pass or the instance table. At night there is no sun; the lamps
  stay F2's.
- **Step 1, the closure test:** relight per vertex in the parity vertex stage with GT4's own model (the same lanes,
  directions and ambient, Lambert with the body's power), through what B10 stores: the per-instance material colour
  and per-view light colours recombined into lane colours, times the texture from the division. It must give GT4's
  pixel within a few levels on the N.L items of `race_grid43` and `race_boot`; the numbers per capture and the split
  rule go in the step's commit. Also in step 1: the division check (GT4's pixel over the channel against the
  texture, a few levels), and the count of untagged equal-Z draws over items.
- **Step 2, per-pixel lighting,** the first deliberate change. Per-pixel lighting from interpolated normals differs
  from GT4's Gouraud by a known amount (lane B, CPU renderers, the N.L bodies' drawn triangles: `race_grid43` 92.4 %
  of samples within 4 levels and 1.2 % over 16; `race_boot` 93.3 % and 1.9 %; power 2^3 the worst, mean 5.1),
  reported per frame (the share over 4 and over 16 levels, by power).
- Then the PBR terms one at a time (the classes' BRDFs, the sun's specular), each against the previous step.
- Depends: B8, R8 (closed), R3's night, showroom and destination-alpha probes. Acceptance: `VAL`; a NaN and Inf
  count; `PERF` under 4 ms GPU at 1440 x 1080 on the RX 7900 XTX; hybrid with `original` materials identical; the
  closure test; a `GT4_SIDE_VIEW=class` debug view; the owner's review against PCSX2 stills of the REF scenes.
  Before hybrid enters a preset: the CPU cost (quiet-window `PERF`, then the front end's +31 % and the render
  thread's +0.35 ms cut or justified) and R9's two every-frame readers checked (B8).

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


**B16: Upscaler interface and jitter (M; lane C, moved from lane B on 2026-11-08, lane B reviewing).** In order,
after D10 (c) and R13: (1) CONTRACTS b.11 states `DisplayInputs`, the seam between the scene side (lane B) and the
display side (lane C): at a render extent R, colour; velocity as a fraction of R, previous less current, the jitter
removed; depth in its stated convention (the GS path's Z times 2^-32, larger nearer: a reversed infinite projection
on race views); the reactive and transparency masks; the camera by role; reset; the jitter as applied. The display
side takes those and the output extent, nothing else. (2) `platform/render/upscale/`: the `Upscaler` interface,
FSR 2 moved in unchanged as its first backend.
(3) The jitter sequence (`pipeline.cpp`) one call to the selected upscaler (phase count, offset), and
`resolveConfig`'s `selfResolved` rule for any temporal upscaler. The display side works at any R; until B16b the
scene delivers integer scales. Depends: B3. Acceptance: `ORIG`; `PRESETS`, and the FSR 2 and TAA presets' frames
identical to before the move; `VAL`; `PERF` unchanged.

**B16b: Free render scale for the scene target (M; lane B).** With B8's in-place drawing, the render scale is the
scene target's: a free (non-integer) scale for the scene target as a whole, behind `DisplayInputs`
(`gsn_scene.cpp`: `ensureTarget`/`decideScale`, the scissor and exact-target paths, copies and reinterpretations
between targets, the reads' downscale). `original` never takes a fractional scale. Depends: B8 (its last
`gsn_scene.cpp` commit), B16 (1). Acceptance: `ORIG` (integer scales kept on the GS path); `VAL`; render scale
67 % at 4K upscaled by FSR 2 without TAA ghosting on the flyover (review), motion vectors exact (velocity debug
view).

**B16c: The GS path's velocity and depth at the seam (S-M; lane B, lane C reviewing).** Before FSR 3.1 (B17) can reach
the game's frames (the coordinator, 2026-11-09). Lane C's survey: FSR 2 never runs on the game's frames, because
`GsnScene::hasMotionVectors()` is false, although the GS scene already draws motion vectors for its own temporal
resolve (`gs_passes/motion vectors`); a selected FSR 2 turns on that resolve and scales with FSR 1. B16c delivers them
at CONTRACTS b.11's seam, at today's integer scales (independent of B16b's free scale): velocity from `gsn_velocity`'s
GS pixels divided by the target's extent (b.11's fraction of R), depth in b.11's GS-path convention,
`hasMotionVectors()` true when they are, and a temporal upscaler then taking over from the GS scene's own resolve
(`resolveConfig`'s `selfResolved` follows). Also delivered, since the game's frames reach the pipeline whole: a
reactive or transparency mask over the 2D draws (a constant Z per sprite: the HUD, the speedometer, menu text) and the
sky, whose depth is arbitrary (b.11), so FSR 2 doesn't smear them. GT4 draws the 3D into its scene target and its post
pass copies and grades that into the shown one, so the mask's reference and the velocity and depth's mapping come from
one anchor: the last draw into the picture the list makes (the back buffer, shown at the next flip) that covers the
displayed rectangle (98 % of it) and samples a target, followed back through such draws to a target a Light drew into
this list (at most 4 steps; lane B, 2026-11-09: one step on every progressive list measured). The coverage condition
keeps a small sprite that samples the scene target after the copy (the flare's and the luminance probe's samples) from
becoming the anchor. The picture right after it is the reference, and velocity and depth are kept with that picture; a
pixel is reactive where it differs from the reference by more than 2 levels on any channel; anything drawn later (the
HUD, text, menus) is reactive; a list without an anchor (transitions, menus with no Light) and every fields-mode frame
is reactive everywhere with velocity 0, counted; and `DisplaySceneState::reset`, so a cut doesn't ghost: always on a
change of the displayed target's size or format and on the GS scene's own history resets (its TAA already resets on
those), since a preset with `upscaler = fsr2` doesn't always want scene records; and on the camera cuts (`kViewCut`,
the race's and the replay's) and scene switches when records are on. The scene stage's jitter then follows the
selected upscaler's sequence (B16 (3)) instead of its own Halton phase, or the two disagree; and FSR 2's disocclusion
is trusted only where b.11's depth convention has been checked for the view's role (race views so far). Depends: B16
(1), B8. Acceptance: `ORIG` (`original` never uses it); motion vectors exact on `race_pe2_prog` (the velocity debug
view; the REF race captures before it are fields mode, reactive everywhere); FSR 2 running on the game's frames (its
passes in `--profile-json`), reviewed against the GS scene's own resolve for ghosting; the mask on `race_pe2_prog` and
a progressive night lap at `ultra`: every progressive list with GT4's post copy chains to a Light (none ending
elsewhere, the cap never hit), the reactive share (pixels differing by more than 2 levels on any channel) on every
sampled race frame within its view's bound, each view's measured maximum plus 2 points (measured flyover 2.4-2.7 %,
chase 4.8 %, bumper 11.0-14.8 %, the B-spec panel 14.7-18.0 %; bounds 5, 7, 17 and 20 %, and 20 % overall; a view not
listed gets its own measurement and visual check before it counts, so a view with more HUD reads as that view, not as
a leak), the mask the HUD and nothing else on a frame per view (bumper, chase, the B-spec panel), lists without an
anchor only at transitions and fields-mode screens; the HUD and a menu screen held still through FSR 2 under camera
motion (that race, and `menus_boot`'s title window with the field-blended menus); the frame after a camera cut clean
against the GS scene's own resolve (a cut on `race_pe2_prog`), run at `ultra` as shipped, scene records off, so it
measures what a player sees: if FSR 2 ghosts there and the resolve doesn't, B16c needs a cut signal that doesn't need
records; `PERF`; `PRESETS` identical except where `upscaler = fsr2`, by design.
Progress (lane B, 2026-11-09): parts 1 and 3, the anchor (`findSeamAnchor`, with its unit test), the Light's motion
vectors kept (`gsn_seam_keep.comp`), the seam (`gsn_seam.comp`) and the display pass's seam variant
(`gsn_display.frag`, `GSN_SEAM`), are on main (eecb88f) behind `GsnScene::setDisplaySeam`, off by default
(`GT4_B16C_SEAM=1` to test), and nothing calls them until part 4. With the seam off, `ORIG` is identical on race7,
race_grid43 and race_boot. With it on over `race_pe2_prog` at `ultra`, there are 0 validation messages, and all 1,024
progressive lists chain to a Light (3,255 fields-mode lists, 21 without an anchor, the cap never hit). Open before
part 4: the bumper view's near-road motion vectors are wrong (a constant band with the camera still), to be fixed in
the scene stage. Lane A's two review points on the anchor are fixed (3e0bd81, b50174f), each with a test in
`tests/seam_anchor.cpp`: the made-target search counts a grade's destination, not only draws; and at each step back, a
Light counts only if no covering pass sampling a target wrote that source after it, glows in between not hiding that
pass; `ORIG` identical and the seam's run unchanged after them. The diagnostics are on origin as `debug/b-b16c-diag`.

**B17: FSR 3.1 (M; lane C, moved from lane B on 2026-11-08, lane B reviewing).** A backend of B16's interface:
FSR 3.1 only, from FidelityFX SDK v1.1.4 (MIT), vendored unmodified with our glslang and our own Vulkan glue, at
today's integer scales. Its depth flags are the ones b.11 states (B16 (1); FSR 2's today: inverted, for GT4's
larger-is-nearer Z, and an infinite far plane). DLSS and XeSS are out: R13 finds their licences incompatible with
GPLv3 and RELEASE_POLICY.md, and only the owner can change that. Depends: B16, R13; B16c for the game's frames
(without it FSR 3.1 reaches only the test scene and the modern scene); B16b for fractional scales.
Acceptance: `VAL`; `PERF`; as for FSR 2, the same frames on every device class CI runs (the Linux and arm64 cross
builds).
Step 1 done (lane C, 2026-11-09): the SDK's FSR 3.1 upscaler vendored unmodified, the upscaler only, with no frame
generation, and a manifest of upstream hashes (6157b187); `gt4_fsr3` built on every host platform from our glslang's
SPIR-V in both FFX_HALF variants, its bindings reflected, the variant logged per device, a compile-time check that the
SDK's private context fits the public size, and linked into nothing (7d17cd5a). Step 2 done (lane C, 2026-11-09;
fd2b8d6d, 32ad5c26, 26479c48): the backend behind `upscale::Upscaler`, `render.upscaler = fsr3` in no preset, reaching
the test scene and the modern scene until B16c's part 4; VAL with no messages in both FFX_HALF variants, a failed
create falling back to FSR 1 (FSR 2's now too), `PRESETS` identical, and `PERF` at 2560 x 1440 ultra about FSR 2's
(559 against 583 us at native AA, 356 against 351 upscaling). On the way: the frame-generation link gap (a refusing
stub), twelve FSR 2 and SDK name clashes (renamed), the SDK's null KHR lookup (served as its core function), the
16-bit features enabled only when supported, and the SDK GLSL's luma-history format (a patched copy, AMD issue #161)
(the stub, the renames and the patched header listed in third_party/README). Left: the game's frames (B16c),
fractional scales (B16b), the Odin run (C6b: the variant Turnip gets, the memory line).

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

**B24: GS-exact texel sampling (M; lane B; lane A reviews the front end).** The GS path leaves a sprite's or
triangle's texture coordinates and its bilinear filter to each GPU's interpolator and sampler, and GPUs differ there:
the HUD's glyph edges moving by a texel (a texel boundary hit exactly), and texture sampling over the 3D scene
(section 2's per-device-class `ORIG`; the coordinator, 2026-11-09). Scoped by primitive: for sprites (2D: the light
image, the HUD, the post copies) B24 computes each pixel's texel coordinate as the GS does (its fixed-point UV step
and 4 sub-texel bits from the pixel position) and filters with the GS's bilinear weights in the shader, exact and
cheap. For 3D triangles above 1x there is no GS sample to match (the GS only sampled at 1x), so B24 makes their
sampling deterministic across GPUs: the texel coordinate from the interpolated STQ, rounded to the GS's sub-texel
bits, then the GS's bilinear weights, exactly GS-like at 1x only. The owner's night strips turned out not to be B24's
(2026-11-10): they were the native renderer's own, on every GPU at every scale but 1:1, and PCSX2's software renderer
shows none. The draw is GT4's 32 x 32 light image (about 28 texels of it) stretched over the road as 20 abutting
32-pixel sprites, each clamped above 1:1 to its own slice's texels (`gsn.frag`'s `insideSprite`). Fixed by 73cfad8
(lane A): abutting sprites of one image are a run, clamped as one image at its outer edges. Strips 0 of 9 frames on
`lap_ssr5f_boot` at low and at 2x (from 3 and 5), closer to `original` at 1:1 on every night progressive frame
measured (`lap_ssr5r_prog`, 11 of 11), unchanged on the day one (`race_pe2_prog`), `ORIG` unchanged; the same fix
removed a fainter seam every 32 GS pixels in a full-screen pass GT4 draws in columns (race7, mostly 1-3 levels: the
post copies' case); NATIVE (section 2) now guards it, and its check on the Odin (no band in the night replay, the
structure test) is 73cfad8's, in the next build, no longer B24's. B24 keeps its own purpose, sampling deterministic
across GPUs, its exact mode keeping each slice's own clamp (9f57886). Depends: B4. Acceptance: the desktop's frames
bit-identical (else a re-baseline under section 2's rule, with PCSX2 on a night frame and a HUD frame); on the Odin
`ORIG` within 2 levels on every pixel at `original` on the REF captures (above 1x: deterministic across devices within
2 levels, not GS-exact), after which section 2's strict rule replaces the interim one; `PERF` desktop and Odin (C6b);
`VAL`.

**B25: Depth buffers in the mid-run snapshot (M; lane B; lane A reviews the front end). Parked** (lane B, 2026-11-09:
`parked/b25-depth-start`, the Z start and the read-back; every gate passes but the acceptance, the sub-capture pair
unchanged to the pixel). The fix as designed can't work: the 3D's Z buffer at bp0000 (640x448, pages 0-139) has its
top half (pages 0-69) under the display buffer bp0 (640x224), which owns them in the page table, so the snapshot can't
carry that half, and GS memory there holds the shown picture, not Z. On the PS2 the display copy overwrites that Z
every frame and GT4's Z24 pass turns it round in place; our Z is a separate GPU image the display copy never touches,
with a history no snapshot of GS memory carries. The cause stands; the fix it needs is B26. The design as it was:
RISKS R7, named (lane B, 2026-11-09, `debug/b-r7-trace`): a capture recorded mid-run differs from the same run
recorded from game start because its snapshot carries no Z buffer. `GsFrontend::drawnTargets` skips every depth
target, so `readVramWithTargets` reads back colour targets only, and a sub-capture starts with Z from GS memory, which
the GPU-drawn Z never reaches. GT4 inverts Z in place each frame rather than clearing it, so the stale Z survives
wherever the scene keeps covering it: under the car ahead, whose depth-tested edges then come out differently (the Z
buffer bp0000 differs on 21 values of the car's silhouette at VBlank 9,944, the 3D target on 109 pixels after the
car's draws and 495 by the frame's end), and GT4's feedback buffers likely carry that on into the whole-picture
difference lane C measured at VBlank 10,926. A sub-capture recorded from a lockstep replay reads back no targets at
all (no read service). Found by limiting one VBlank's list (`GT4_GSN_LIMIT_AT`) with the history whole, the draw lists
identical. The live game and captures from game start agree with each other; whether they agree with the PS2 where Z
meets the display's pages is B26's question; the earlier leads stand as facts, not as the cause: pages written by
several texture sets (our memory keeps the last set's blocks; reads by set don't see it), and stale 4-bit reads of
drawn pages, ruled out on these pages. The fix has two halves (lane B, 2026-11-09). (1) The read-back: `drawnTargets`
includes the Z buffers, and the read service reads them with a compute pass (`gsn_depth_read.comp`; a Z buffer is not
a transfer source), turning the depth aspect into the GS's Z (floor(d x 2^32), at most the format's largest, largest -
Z while the target is inverted), packed as Z32, Z24 or Z16 for `GsMemory`'s own Z swizzle, counted. It serves the
snapshot's read only; the game's own reads are unchanged (lane B): `GT4_REC_GS_READ` goes through `GsFrontend::read`,
which picks its target from the page table, not `drawnTargets`; a read of pages a Z buffer owns fails its depth test
there and is served from the memory model as before (counted in `readMismatches`), and `GsnScene::copyRect` refuses a
non-depth request on a Z buffer as it did. A counter of depth reads served per run shows 0 in a from-boot run. (2) The
restore: `GsnScene` creates every new target cleared, so the snapshot's memory alone restores nothing. In a run that
started from a restored state (`GsFrontend::restoreMemory`, a sub-capture), a Z buffer the front end creates for the
first time takes its initial Z decoded from GS memory on the front end's thread, carried in the FrameList with the new
target, and `GsnScene` loads it into the depth image (the depth aspect, each render pixel its GS pixel's Z, never
filtered: expanded to the render scale before the copy, or by a nearest draw) instead of clearing it. A from-boot run
never restores a state, so `ORIG` and every from-boot capture are unchanged by construction. Colour targets start the
same way but stay out unless the 36 frames show they matter. A lockstep replay's recording reads its targets back too,
without the deadlock the read service has there (gs_host.cpp withholds it on purpose), as a separate step after these.
The two halves are two commits landed together (lane B): the restore first, gated on `restoreMemory`, then the
read-back; the 36 frames need both, since without the read-back the snapshot's memory still holds stale Z for the
restore to load. Depends: nothing beyond the GS path. Acceptance: lane C's sub-capture pair (`lap_ssr5r_boot` against
its sub-capture from VBlank 9,926, `subcap_night`'s 36 frames) identical; `ORIG` unchanged (the snapshot runs only
when a capture starts mid-run), with an assertion and a counter of new Z buffers loaded from memory, 0 in every `ORIG`
run, since they prove the restore can't fire from boot; `CTEST` of the Z write-back's swizzle in each format. Until it
lands, references come from captures recorded from game start, as CAPTURES says.

**B26: Colour and Z sharing GS memory (M?; lane B; lane A reviews the front end).** B25's finding: GT4's 3D Z buffer
shares GS memory with the display buffer, and on the PS2 the display copy overwrites that Z every frame before GT4's
Z24 pass inverts it in place; our Z buffer is a separate GPU image the colour writes never reach. That is a fidelity
question beyond mid-run captures: wherever GT4 depth-tests against Z the display copy wrote, our from-boot rendering
may differ from the PS2. Step 0, no dependency, in two halves. (a) Lane A: the front end's count, from its page table,
of GT4's depth-tested draws meeting pages a colour target wrote since the Z buffer's last depth write, split by
whether a Z24 pass ran between, on the REF and race captures; it sizes B26 (only the 3D's first draws after the
display copy, or everywhere). (b) Lane B, the coordinator's measure: on from-boot frames, each depth-tested draw run
against our Z and against an aliased Z built from GS memory as the PS2 has it (the display copy, then GT4's Z24 pass
applied), counting per frame the pixels whose test result differs; it says whether live play differs. Step 1, with
C12's 3D step: the PCSX2 comparison on the frames step 0 flags (PCSX2's software GS models the shared memory). Then,
only if they differ: an aliasing model, a Z buffer's GPU image taking the colour writes to its pages (and the
reverse), which would move from-boot `ORIG`, so a re-baseline under section 2's rule. It also fixes R7's mid-run
captures. Depends: nothing for step 0; C12's 3D step for step 1. Acceptance of step 0: both counts per frame on the
REF captures, with the frames they flag listed; the model's acceptance is set from them and C12's confirmation.

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

**C6: aarch64 Linux runs on the AYN Odin 3 (M).** The aarch64 host on Armada Linux (Adreno 830, Mesa Turnip), under
gamescope, with the x86-32 game under FEX. Windows ARM64 is not run (no device). Depends: C5, R4, C9 (live link with
the game under Wine). Acceptance: race capture replay `ORIG` against x64 by section 2's per-device-class rule (at most
2 levels, no structure); a live race session runs 5 minutes at the Odin settings (1280 x 800); timings reported;
`VK_EXT_provoking_vertex` reported from the line the renderer logs at device creation (for B7's parity path), that
line named in C6b. Without it parity's triangles are rotated and its Z may differ: a B7 finding, not a C6 failure
(parity is `--set` only). That line waits for B7, which adds the log.

**C6b: The Odin device run (S, recurring; lane C).** The measurements the desktop can't settle, made on the owner's
AYN Odin 3 (C6's setup: aarch64 Linux, Adreno 830 under Mesa Turnip, the game under FEX, 1280 x 800) with the latest
Odin build, and reported in one run report: the build's hash, the device's Mesa and FEX versions, and each item's
number against its pass line. Tickets that need an Odin number point here instead of saying 'the Odin run'; an item
leaves the list when its ticket is closed. Two kinds: **build** items run unattended from one command on the desktop
or the Mac, `tools/device_run.py --ssh HOST` (replays of the REF captures and scripted routes over ssh, the report
copied back by itself; the coordinator copies its inputs to the device: the captures, the reference hashes and the
save folders, about 16.1 GB, and it refuses to start under 3 GB free); **device** items need a person at the Odin (a
live game, the pad, a judgement on screen). Captures and saves stay on the device, never committed or served, as on
the desktop: the report carries numbers and logs, never frames of the game.

| Item | Measured | Waiting | Gates | Pass line | Kind |
|---|---|---|---|---|---|
| B6's span | the front end's thread CPU time per race frame with the scene split on, `race_boot.cap` after its first 300 frames, p95 and the margin; and in a night race with `lighting.night = enhanced` (F2b's laps; desktop: +684 us p95) | B6c | `render.scene = hybrid` and `lighting.night = enhanced` entering any preset (F2b) | p95 within the frame budget the coordinator sets from this first measurement (the desktop's bar is 0.5 ms; FEX's overhead is what is being measured), the margin reported | build |
| Load times | `GT4_LOAD_STATS` on a race load and the menus' loads (blocked VBlanks, seconds), on the route to PE race 2 with a copy of the approved save (desktop: the race load 0.70 s, mostly the game's own pacing; the SD card and Wine's file I/O are what the Odin adds) | D10 (c) | D10's close, and lane B's warm-up follow-up (textures before the read, pipelines from the cache) | the race load's blocked time under 0.2 s (D10 (c)'s target), else the follow-up is scheduled with this number | build (a scripted route, as `tools/photo_check.py` drives one) |
| Provoking vertex | the line the renderer logs at device creation: `VK_EXT_provoking_vertex` present or not | C6, B7 | `render.scene = parity` on the Odin (its triangles and Z) | reported; absent is a B7 finding, not a failure | build |
| B16c's kept images | the device's free video memory before and after B16c (its kept velocity and lit pictures, about 2x the shown target), beside 97d0809a's freed MSAA targets | B16c | `render.upscaler = fsr2` in the Odin's device preset | no allocation failure, and free video memory above the device's floor, over a 5-minute race at the device preset, free memory reported (the floor is what the first run shows the device needs, named in the report) | build |
| B16c's GPU cost | the GPU time of FSR 2 on the game's frames against the GS scene's own temporal resolve, at the Odin's device preset scale, from `--profile-json` on the REF captures (the fsr2 pass against `gs_passes/temporal resolve`) | B16c | `render.upscaler = fsr2` in the Odin's device preset | both times reported, and FSR 2 within the frame budget the device preset leaves it, else the device preset keeps the GS scene's resolve | build |
| Modern lights' budgets | `PERF` at 1280 x 800 per setting level, from `--profile-json` on the REF captures at night (F2b's laps) | F6, F7, F8, F9 (each ticket's own Odin budget: F6 at `player`, F8 at `low` or off) | each setting's value in the Odin's device preset | each setting has a value that fits the Odin's frame budget or is off there; the device preset says which | build |
| Host mixer | `audio.mixer = host` (with `audio.interpolation = sinc`) at each `audio.spatial` (original, stereo, hrtf): the mix's time per frame and, for hrtf, its own part and its active buses (the host summary's `hrtf: N buses a frame, T us a frame`); if hrtf is over budget, the same with the `GT4_HRTF_BUSES` environment variable (debugging only) at 8 and 4 (the cap, nearest cars first); underruns on a live race | D5, D9 (hrtf: D9 step 3) | `audio.mixer = host` becoming the default (the coordinator's call) and with it `audio.spatial = stereo` in low to ultra; hrtf's Odin cost | no underrun in a 5-minute race at each of the three; hrtf's time reported against the frame, and the bus cap that fits, if any | build (the times, from a replay of a capture recorded with `audio.mixer = host`), device (the underruns, on a live race) |
| Settings pages | the page layer's draw cost with the hub open (`GT4_UI_PAGE_TEST=hub`), against E5's 98 us on the desktop | M3, if lane M asks | none yet | reported | build |
| R4's device checks | the owner's test of 11 steps in `spikes/R4-link-across-fex.md` ("The package on the Odin"): Proton found; the first prefix creation; the `Z:\` paths; the window under gamescope; Vulkan on the Adreno with `race7` against the x64 frames; sound; the built-in controls; quitting (no Wine process left); the speed in a race; the Steam shortcut; the files beside the host. Steps 4 and 6 to 11 are still unverified on the device | R4, C6 | C6's close and the Odin build's release | each passes, or is a finding with its log | device |
| Hybrid on the Odin | `render.scene = hybrid` on `race_grid43`: the frames against `original` (`PARITY`), and its CPU cost on the front-end and render threads (desktop: +31 % and +0.35 ms) | B8 | `hybrid` in any preset | `PARITY`'s bar met; the CPU cost reported against the device's frame | build |
| Pipeline cache on Turnip | the first race's frame times with an emptied pipeline cache against a warm one (the same replay twice) | B2 | none; informs D10 (c)'s warm-up | the hitches of the cold run reported, and the warm run without them | build |
| Photo Travel's shot | the shot's time at Beacon Hill (the r5 save): its 128 synchronous CT24 tile reads (`gsn read N took` lines, `GT4_READ_HASH=0` is fine) and the shot's VBlanks, with `tools/photo_check.py` | the CT24 read fix (d7d38b8, R6 / P2 reads; lane A) | none; the Odin's shot time | the photo developed within `PHOTO_TRIES` x 600 VBlanks (`photo_check` OK on the first or second capture), and no read over 1 s | build |
| Photo Mode's shooting screen | the shooting screen's frame rate and GPU time at 1x and at the device preset's scale (with `render.upscaler = fsr2` once B16c lands), and the held picture's copy cost when P2c's parked follow-up returns | P2c's follow-up (lane B), B16c | the held picture entering main; the device preset's upscaler on that screen | the screen at the display's rate, its GPU time reported | build (the route of `tools/photo_check.py`) |
| Wheel force feedback | **Needs a wheel, not owned** (the coordinator, 2026-11-09): D7's live check, on any real wheel at `input.wheel.ffb_strength = 15` first, on Windows and on Linux (the Odin's package or a desktop): host.log's open line (`Input: wheel force feedback: NAME on PLATFORM: constant, spring, damper, autocentre`), then GT4's menus and a race on the wheel | D7 | D7's close; the 60 % ceiling in `platform/input/wheel_ffb.h` and the 15 % first run stay until it passes | the wheel pulls towards the centre and never away, in the menus and a race, on both platforms; a hitch over 0.2 s cuts A briefly, as designed (PLAN D7) | device |
| Latency | pad-to-photon with `GT4_TRACE_LATENCY=1` at the display's real refresh rate, a windowed race with a pad, default settings (vsync FIFO, interpolation on), `tools/latency_report.py` on `host.log` (the desktop only has a 30 Hz virtual display) | D11 | D11's budget, which D12-D16's filters count against | reported against D11's budget | device |
| Texture pack install | the HD texture pack installed over a fast connection on the device (`docs/TEXTURE_PACKS.md`): the download, the install and the first race with it | the texture packs | `texture.hd_pack` in the device preset | installs and loads; the times reported | device |
| Real controllers (desktop or Odin) | an Xbox pad over USB and over the Xbox Wireless Adapter: rumble from the physics, impulse triggers (lock-up left, wheelspin right); a DualSense over USB and Bluetooth: adaptive triggers (brake resistance near lock-up, a throttle pulse on wheelspin), the brake trigger free again after quitting mid-brake; each device keeping its profile after a restart; two real pads in 2 Player Battle (Steam Input and DS4Windows closed first: either can hold the pad and swallow the effects) | D13, D14 | D14's effects in the presets; D13's profiles | each check passes, or is a finding | device |
| Night bands | two pictures of the same band moment in the owner's normal play (Steam, the device preset): (a) the problem dump, the image the renderer hands to present: L3 + R3 held a quarter second (or Ctrl + F12; L3 + R3 held is reserved for the dump since 0bdaabab: no menu chord or host binding may take it; L3 or R3 alone reaches the game, and from the poll both are down neither does until both are up again, so GT4 sees neither while the dump is triggered) writes `dumps/<date-time>/` beside the host: that frame and the next as PNG, the frame's GS draw list (`draws.txt`, as `GT4_GSN_DRAWS=all` prints it), the front end's GS memory (`gs_memory.bin`), the settings in force and the presentation (swapchain size, format, present mode, image count, the video driver, the GPU); and (b) what the panel shows after the swapchain and gamescope: Steam's screenshot in game mode (Steam or Guide button + R1, which gamescope composites; to be confirmed on the Odin's Armada image), else a phone photo | the night bands (lane F's step 0 found none on the desktop; N3 is not known to be their cause) | nothing yet | both pictures of one moment. Bands in (a): our rendering on Turnip or FEX, lane B bisects. Bands only in (b): the present path (gamescope's scaling or filter, the swapchain, FIFO), a device-configuration fix (lane C). The dump copied to the desktop (the owner's machines; game content, never committed or served), so lane B can bisect it from the draw list; a trigger capture (`GT4_HOST_RECORD=PATH,trigger:FILE,COUNT`) beside it when the draw stream is needed, its replay's palettised night textures to be judged by the dump's frames (RISKS R7) | device |
| Menu movies | the City Courses menu's movie background, live: presents a second and the hitch log's lines over 20 ms (the owner's first clip: 45-47 fps, 25-28 ms presents, one 1024 KB texture each; the movie path in `gs_frontend.cpp` and `uploadTextures`, desktop measurement and fix with lane U, 2026-11-09) | E2 (its path on the Odin, lane U's fix) | the fix's release | the course menu at 59-60 fps with no hitch over 20 ms | device (a live menu) |
| C6 itself | a race capture replayed (`ORIG` against the x64 host's hashes); a live race session of 5 minutes at the Odin settings, its frame times | C6 | C6's close | `ORIG` identical; the session runs its 5 minutes, frame times reported | build (replay), device (session) |

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
  - (c) **replaced** (lane C's measurement, 2026-11-08; the architect): no faster frame clock. `LoadingMode`
    (0x00104E30) is never called on the race route, and the game loop waits at most one VBlank a frame throughout
    the race load (0.82 s: disc 0.01, CPU 0.09, waiting 0.71). The missing frames are three gaps (30 VBlanks, 0.5
    s) in which the game thread waits on no VBlank and uses almost no CPU, in line with the load's GS reads (the
    car thumbnails: 38 reads, two of them 125 and 78 ms). The other VBlank waits (`mUpdateContext::Sync` 00215298,
    135 VBlanks on the route) pace the visible fades and menu animations, which stay as they are. So (c) is the GS
    read path in loads: first measured (`GT4_READ_STATS=1`: when each read is asked and lands, in VBlanks, and the
    host's time from `GS_READ` to `GS_IMAGE`), each gap matched to the read in flight; then shortened where the
    time goes (`runtime/link.cpp` lane C's, lane A reviewing; a host change by the lane that owns its file). On a
    PS2 the read is sub-frame, so a faster read is closer to the original and applies to `original` too, on the
    coordinator's condition: only if `ORIG` on `race7`, `race_grid43` and `race_boot` stays identical, since a
    faster read must not change which frame the game sees its result in. Target (restated, lane C's measurement,
    2026-11-09): the race load's **blocked time, its VBlanks without a new frame**, under 0.2 s; the reads' bytes
    unchanged (`GT4_READ_HASH`); lap times and AI unchanged. `GT4_LOAD_STATS`' 'waiting' (wall time minus disc and
    CPU) also counts the loading frames GT4 draws at the VBlank rate (21 frames, 0.35 s), its own pacing, which
    (c) doesn't change, so it can't be the target.
  - (a) done (lane C, 38bbb77): the reads wait on a high-resolution timer, not `Sleep(1)`. The race load went from
    41-44 VBlanks with 20-22 blocked to 28 with 7 blocked (about 0.12 s): under the target on the desktop.
  - (b) measured: the 7 blocked VBlanks are the host's own serve time inside the reads, mostly the scene's warm-up
    paid by the first reads (one present of 147 ms: 27 textures uploaded, 3 pipelines created, the pipeline cache
    on). Taking it out of the reads (textures before the read, pipelines from the cache) is a follow-up for lane
    B, not a D10 gate, after the R6 re-baseline and P2c's depth texture (the coordinator): bounded by those 7
    VBlanks on the desktop, it is judged on the Odin (C6b), where uploads and pipeline creation cost more.
- Acceptance: the load times (race and menus) reported before and after, against those targets; `ORIG` unchanged in
  its picture and the game's pacing (faster reads shorten `original`'s loads too, which is the point; the frame in
  which the game sees each read's result is unchanged); saves and replays unaffected (session test).
- **Done on the desktop** (lane C, 9df2a5a; its message holds the table): the race load's blocked VBlanks 22 to 7
  (0.12 s), the race start 9 to 1, the menus' loads already unblocked; `GT4_LOAD_STATS` prints `blocked` (it assumes a
  screen drawn every VBlank: a 30 fps screen counts half its VBlanks with nothing blocking). Saves: GT4 re-encrypts
  each save, so they were checked through the game's load (Home pixel-identical from both); replays: the race and its
  six times identical before and after, main run twice as the control; the replay itself one VBlank phase off from the
  shorter loads (99.99 % within a VBlank). Open: the Odin's `GT4_LOAD_STATS` (C6b), and (c)'s
  warm-up follow-up (lane B).

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

**C12: GS dumps for PCSX2, frame-exact (M; lane C the format and the player run, lane B the front end's side).** Rule
6's PCSX2 check today pairs our replay with a separate PCSX2 run by content, which gives only a statistical match. C12
makes it exact: the host writes a frame as a PCSX2 `.gs` dump (the starting GS memory and registers, then the frame's
packets), and PCSX2's GS dump player renders the same input in its software renderer, compared pixel for pixel at 1x.
The front end already holds the frame in GS terms (lane B, 2026-11-09): its vertices are the GS's values (XY as (XYZ -
XYOFFSET) / 16, ST and Q the registers' floats, UV / 16, Z, RGBAQ and fog integers), the drawing state is per draw,
uploads and texture sets are bytes, and the starting memory is `readVramWithTargets`. GT4's 3D batches are the
exception: `gsn_models.comp` makes their vertices on the GPU from the model jobs, so 3D needs that buffer read back
after the frame (a stall, acceptable for a debug dump) or the jobs run on the CPU. Steps (lane C's scope): the writer
(`platform/gs/gsdump_writer`, no GPU; the state's 425-byte register part laid out from PCSX2's `GSState::Freeze` for
that version, filled with our registers) and its round trip; then the front end's dump (`GT4_GSDUMP=PATH,FROM,COUNT`,
lane B): most of the host's GS input arrives as records, not GIF, so `platform/gs/gsdump_encoder` turns each record
into GIF (state groups and transfer registers as A+D, vertices as REGLIST with each register's whole value, every
transfer on path 3 as PCSX2's own GT4 dumps have it, raw GIF passed through on its own path when the front end treats
that path differently, else on path 3), keeping a register shadow from VBlank 0 so the dump's starting registers are
what the stream set, and writing only from FROM; the display as privileged packets (`platform/gs/gs_privileged.h`). A
texture-set record becomes its uploads as transfers at its slot, from the front end's kept content when the runtime
didn't resend the bytes, and the start memory has every set page materialized (the front end writes set bytes into GS
memory only where something reads them). Two record kinds don't become GIF in the 2D step and are counted as not
dumped: the 3D batches (the later step) and the native effect passes (`EffectRecord` `kGrade`, the race's colour
grade, which the host draws as its own pass; a race dump isn't exact there unless the runtime also sends the GS's way,
`GT4_EFFECTS_BOTH`). The tee in `gs_host.cpp`'s native target reads `GT4_GSDUMP` once and costs a null test per record
when unset, so `ORIG` is unchanged with it off; it takes the records in the front end's order before any front-end
work; and its summary line counts what was dumped and what wasn't (lane A, 2026-11-10). 2D first (enough for B24's
sprites and the HUD), its start memory the colour targets read back under a real-time replay (B25, parked, showed the
depth buffers can't be carried that way, B26): the log counts the draws that depth-test against a Z buffer not drawn
since FROM, and a dump with any is not exact for them. A dump from VBlank 0 of a from-boot capture needs no read-back
at all and is the encoder's exact first check; `tools/pcsx2_gsdump.py` playing it in PCSX2's player at 1x with a
side-by-side compare; 3D through the read-back last. A dump holds game content (GS memory, textures), so it stays on
the owner's machines like a capture, never committed or served. The `.gs` format is the version PCSX2's player reads
(lane C has read state version 9). Depends: nothing for the 2D step (B25 parked; the depth limit counted instead).
Acceptance: `CTEST` round trip on a synthetic frame (written, read back through `GsFrontend::gif`, which needs no GPU,
and the FrameLists compared: every register and vertex equal); a HUD frame and, after the 3D step, a race frame
rendered by PCSX2's software GS from our dump, against our renderer at `original` 1x, with the differences listed by
draw; B24's exact mode then judged on it. Progress (lane C, 2026-11-10): steps 1 and 2 on main (659c3e1c):
`platform/gs/gsdump_writer` writes and reads PCSX2's .gs format (state version 9, its 425-byte register part, 4 MiB of
GS memory, the privileged registers and the packets), checked against two of PCSX2's own GT4 dumps, which decode and
re-encode byte for byte (they settled one point lane C's first reading of the source got wrong: the vertex block is 32
bytes); the `gsdump` CTEST's round trip through `GsFrontend` gives every draw, vertex, texture, target and the GS
memory equal. No dump of ours has been played by PCSX2 yet; that is the player run's job. Next: lane B's front-end
dump (`GT4_GSDUMP`), then the PCSX2 player run.

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
The game-side model's cost (lane D, 2026-11-09; 66b56e1 to 2212835): `advance()` takes each voice in closed form
between events (a block crossing, an envelope step, the voice going off; a sustain held at its clamp steps without
being one), bit-identical to `mix()`'s per-sample path by `CTEST` (`spu2_advance`, every sample) and the live check
(`GT4_SPU_ADVANCE_CHECK`: 7200 frames, 0 differences). A race's voice advance went from about 215 to about 70 us a
frame on the desktop.
The game's hash memo, profile saving (1) (lane D's `wip/d5-hash-memo`, OK'd by lane C and the architect; landed
2026-11-10, rebased unchanged): `REL`, `hook_lint` and `DIFFTEST` pass; `GT4_HASH_MEMO_CHECK=1` over the garage (1.62
million hits hashed again), Photo Travel's shot (0.67 million) and PE race 2 with the drive (1.29 million): 0 differ.
The saving is smaller than the profile's 620 us suggested: over the whole race session (menus included, one off/on
pair on one binary) the game's CPU went from 1.69 to 1.63 ms a frame, with about 170 hits of 720 lookups a walk, since
a guest write between two lookups empties the memo. The uploads and origin parts sent are the same off and on. If
more is wanted: a memo that lasts the frame and is emptied at the VBlank too.

**D6: Vehicle state discovery (L).** RISKS R14, then the fields of `scene::Vehicle`. Depends: R14. Acceptance: per
wheel load and slip plotted over a lap, consistent with the car's motion; documented addresses.

**D7: Wheels through LGDEV (L). Done except the live check** (lane D, 2026-11-09: steps 1, 2a, 2b and 2c on main,
5a221df the last; the live check needs a wheel, not owned, and is on C6b's owner list, 4b217a3). GT4 sees a Logitech
Driving Force Pro through LGDEV, and drives its three effects on the host's wheel through `SDL_Haptic` (the effects
and the interface: spikes/R15-lgdev-effects.md). Depends: R15.
- Settings, per device (D13): `input.wheel.mode` = `pad` | `dfp` (D15 adds `native`).
  - `pad` is the behaviour before D7: the wheel is a DualShock 2 through its profile.
  - `dfp` is the PS2's behaviour. It is the default for a device SDL types as a wheel, in every preset, `original`
    included; a gamepad never becomes a wheel.
  - In `dfp` the wheel's input goes to LGDEV alone. Its port keeps only what else is bound there (the keyboard on
    pad 1), so GT4 never sees the wheel twice.
- `input.wheel.ffb_strength`, 0-100 % (D15's name), scales the three effects and caps them: A's level, and the
  spring's and the damper's coefficients and saturations, never exceed it.
  - A hard ceiling in code that no setting passes: 60 % of the device's full scale until the live check has
    measured a wheel.
  - The first live run is at 15 %.
  - The budget, for D7 and D8 alike (the architect, 2026-11-09): every frame the device's whole output stays within
    min(strength, ceiling). The active effects (the constant force, D8's vibration) take it first, then the
    spring's and the damper's saturations get only the room they leave, so the sum of every effect's maximum output
    never exceeds it: a device clips the sum at its own maximum, so effects capped one by one could still ask for
    100 %. In D7 that's GT4's A first, then its spring, then its damper. The room is rounded down to sixteenths of
    the budget, so the conditions change on the device only at a step. Passive effects are not exempt while no
    wheel has been measured; the live check can revisit it. `budgetForces` in `platform/input/wheel_ffb`, `CTEST` on
    every magnitude GT4 can send and on D8's worst frames, low speed included.
- The link: two new kinds, and `GT4_LINK_VERSION` 6 (or the next free number if another lane's bump lands first;
  kinds 18 and 69 are free on main at d0ad4a3).
  - Host to game, `GT4_REC_WHEEL` = 69, every host frame like `GT4_REC_PAD`: the wheel's index (0; D15's second
    wheel needs no other bump), connected, the steering (s16), the two pedal axes (u16), the hat (0-7, 8 none),
    and the buttons in the DFP's bits 16-29 (the inverse of `func_005665E8`).
  - Game to host, `GT4_REC_WHEEL_FX` = 18, when an effect changes (at most once a frame): the wheel's index, then
    per effect its state (none, stopped, running) and its 60-byte block.
  - An older game never meets `dfp`: the game links only at its own version exactly (`runtime/link.cpp`), so a
    newer host and an older game don't link at all, and the bump alone keeps the player from a wheel the game
    would ignore.
- Input: the D13 profile binds the steering and the two pedals as it binds any action. It normalises the rest
  position and the full travel: pedals often rest at the axis's maximum, and some wheels report the two pedals
  combined on one axis.
  - Two additions to the binding:
    - a range form, -1..1 to 0..1, with an inverted variant;
    - a combined pedal axis as two bindings, one on each half.
  - Raw joystick axes for a wheel SDL has no gamepad mapping for.
  - D12's response curve is skipped: GT4 applies its own.
- The game (`runtime/iop.cpp`): the LGDEV stub reports one wheel while the host says one is connected. It answers
  the input read (fno 6) from `GT4_REC_WHEEL`, and keeps the three effects' blocks and states for
  `GT4_REC_WHEEL_FX`.
  - At a plug or unplug the stub writes 1 to the notify area (the EE address fno 12 gives at `+0x5C`), and GT4
    rescans (`func_005552B8`, `func_00560A88`).
  - `GT4_FAKE_DFP=1` stays as the bench: the same path, with pad 1 as the source.
  - What GT4's property 4 = 1 selects on the DFP (perhaps its 900-degree mode) is logged in the session test; the
    host never changes the physical wheel's rotation range.
- The host: `platform/input/wheel_ffb` (no SDL) translates the blocks into effects, with its `CTEST`. The host keeps
  one constant, one spring and one damper effect on the wheel's `SDL_Haptic`; a wheel that lacks one gets the
  others.
  - A: a constant force at magnitude / 255 x strength, on one axis; its sign is the first acceptance check. A
    lasts 200 ms, renewed every 30 ms while it runs, so a stalled host lets it lapse within 0.2 s, well inside the
    watchdog, while an ordinary hitch (100-200 ms: a shader compile, streaming) holds the last force, as the PS2's
    wheel did (lane C, 2026-11-09). The gap event carries the host's last frame time, so a gap reads as load or
    freeze.
  - C: a spring, played centred until a race shows GT4's offset (`+0x0C`) and its scale: GT4 sends 0 in every
    race seen, and an offset read in the wrong units would pull away from centre (lane D).
  - B: a damper.
  - The spring and the damper are passive and run without end.
- Stops, the host's alone: every effect stops at a settings page, a pause, a link drop, a port change, the host's
  exit, an unplug, and when the game's frames stop for 0.5 s while an effect runs (a hang, a debugger, a long
  load).
  - 'A pause' is one the host sees: our pause page, or the host's pause (command 2). GT4's own pause
    (`pause.strb`, with `ui.pause = gt4`) is GT4's to handle; the session test logs what GT4 does to A in it, and
    if A keeps running there, D7 asks before it ships.
  - The game's effect state does not change. On resume the host applies the latest blocks and states again, so
    GT4 and the wheel agree.
  - In a replay (no live game) the host applies no `GT4_REC_WHEEL_FX` and opens no haptic.
- Menus:
  - While one of our settings pages is open, the wheel drives it through its D13 profile, as a pad would. A
    wheel-only player can open Settings (Guide, or the chord through the profile) and use it.
  - GT4's own menus read LGDEV's buttons (lane D, 2026-11-09: a virtual wheel in `dfp` took the whole GT-mode route
    to the race on its buttons alone, pad 1 untouched), so the wheel's buttons never go to its pad port.
- Scope: one wheel, LGDEV index 0. A second wheel (split screen, index 1) is D15's or later.
- Acceptance, in order:
  - A's sign and the pedals' order (the code doesn't settle which axis accelerates) on the `GT4_FAKE_DFP` bench,
    against the steering in its log. The pedals are settled (lane D): pedal 0 (`+0x7E`) accelerates, pedal 1
    (`+0x80`) brakes. A has the steering's sign (left negative); the host plays -A as the force (towards the
    centre): in SDL's convention, a constant force from cartesian direction (1, 0, 0) at level +A pushes left. That
    rests on the bench and SDL's documented convention until a real wheel confirms it at 15 %, on Windows and Linux.
  - A session test. A virtual SDL wheel drives GT4's menus and a race through LGDEV, and the effect stream is
    logged. Every stop is proved: page, pause, link drop, port change, exit, unplug, the 0.5 s watchdog (a game
    that stops sending frames), and A lapsing to zero within about 0.2 s when the host's main thread freezes for
    1 s (a hung host). A replay of a capture holding FX records opens no haptic.
  - `CTEST`: the translation, on fields recorded from R15's race (numbers only), and the binding forms.
  - The gates: release build, `ORIG` and `STREAM` with no wheel (nothing changes), and `DIFFTEST` if the files
    are in its list.
  - Live, on a real wheel, at 15 % first, on Windows and Linux. It must pull towards the centre and never away, in
    GT4's menus and a race; a hitch longer than 0.2 s cuts A briefly, as designed; read host.log's force-feedback
    open line (the SDL backend, which of constant, spring and damper the wheel has, and autocentre off or not). The
    owner has no wheel (the coordinator, 2026-11-09), so D7 ends at the session test until someone has one.

**D8: Physics force feedback (M).** A modern wheel signal from GT4's physics (the audit, section 4.1 design point 3:
the self-aligning torque from the front tyres' lateral force and pneumatic trail, kerb and road texture, lock-up and
slip vibration). Depends: D6, D7. (D10 and D11 moved to lane C.)
- Scope: wheels only. In `dfp`, GT4 already plays its own aligning torque: `func_00353EE8` writes it into byte 2 of
  the feedback packet, which is LGDEV's effect A (R14, R15). So D8 is the signal for D15's `native` mode. Pads stay
  D14's: a motor has no direction, so a steady weight there reads as noise.
- It lands before D15 behind `GT4_PHYSICS_FFB=1` (an environment switch, off: rule 10; no setting and no preset
  until D15 makes it visible), played through D7's `WheelHaptics` in place of GT4's effects, and recorded in a
  session.
- The signal, from the Vehicle record (D6), in `platform/input/wheel_physics` (no SDL):
  - Aligning torque: Mz is the front wheels' sum of `forceLateral` x t(`slipAngle`), the pneumatic trail
    t = t0 x max(0, 1 - |alpha| / alpha_peak), normalised by the front axle's grip (front loads x 1.0). It falls
    to nothing as the front tyres saturate: the loss of weight at understeer. alpha_peak is the slip angle of GT4's
    own front tyre curve's peak (|`forceLateral`| / `load` against `slipAngle`): 0.14-0.18 rad on D8's first lap
    (mean 1.04 at 0.14-0.16, sagging after), so 0.15 rad. The second, gentler lap stayed under 0.06 rad and doesn't
    place the peak. With t falling linearly to zero there, Mz itself peaks near alpha_peak / 2 and is gone at the
    lateral force's peak, as a real tyre's aligning torque leads its lateral force: the shape, not a tuning accident.
    GT4's own A has no such fade (it is about k x front Fy: r = 0.978 with no fade on the first lap, every fade
    worse), so the fade is what D8 adds over the PS2's wheel. Mz's sign against `forceLateral` is -1 (mzSign), from
    GT4's A: 66 of 66 frames on the first lap, 59 of 61 on the second. Played as the constant force, with D7's sign
    and its 200 ms renewal.
  - `steeringTorque` isn't filled (R14 open 1): Mz comes from the tyre forces until it is, and if R14 later finds
    GT4's own rack torque, D8 is compared against it.
  - Kerb and road texture: a periodic effect from the front wheels' stroke speed, as D14's `roadTexture`; its
    frequency follows the speed.
  - Lock-up and wheelspin: a periodic vibration from D14's `lockUp` and `wheelspin`.
  - Low speed: a centring spring and a damper fading in below 10 km/h, where no aligning torque builds.
  - `input.wheel.ffb_strength`, D7's 60 % ceiling, D7's budget (the conditions in what the active effects leave)
    and D7's stops apply unchanged.
- Acceptance:
  - `CTEST`, on synthetic records: the formula's shape (Mz fades past alpha_peak at a constant lateral force; zero
    when stopped or in the air).
  - `CTEST`, on fields recorded from a lap (numbers only, as R15's), at its worst frames (a kerb at the limit of
    grip): the constant force plus the periodic effects never exceed the strength and the ceiling together.
  - Against GT4's own aligning torque (effect A, recorded with the same lap): Mz has A's sign on at least 95 % of
    frames where both are above 5 % of their range; their correlation is reported, and the bins where they part are
    expected past about alpha_peak / 2 (the fade, on purpose).
  - A session test over a lap with `GT4_PHYSICS_FFB=1` and a virtual wheel, the effect stream recorded:
    - kerb pulses line up within a frame with D6's kerb events (a front wheel on the ground with |`strokeSpeed`|
      over 0.15 m/s, the start of `roadTexture`'s ramp);
    - slip pulses line up with lock-up and wheelspin frames (`lockUp` or `wheelspin` above 0);
    - D7's stops all hold.
  - D11: each effect at most one frame behind its Vehicle record.
  - Live: none here (no wheel). The loss of weight at understeer and kerbs are D15's live items.
- Results (lane D, 2026-11-09; virtual wheel, the approved save's GT-mode race, `d8_lap`'s throttle and steering):
  - First lap, sweeps to 80 % lock (732 records with both fronts down above 2 m/s): sign 66 of 66; r = -0.978
    with no fade. Binned by the mean of the two fronts' slip angles, with Mz at alpha_peak 0.15: they part from
    0.09 rad (A 0.70 against Mz 0.26), and past 0.21 rad Mz is 0 while A stays 0.5-0.7 (slides, faded on purpose).
    Every kerb event (111) and slip frame (601) with the vibration on the same record. The constant force plus the
    vibration at most exactly the ceiling (19,660 of 32,767), none over (before the budget took in the conditions).
  - Second lap, gentle sweeps (852 records): sign 59 of 61 (96.7 %); r = -0.814 with no fade; the same parting
    (from 0.12 rad: A 0.44 against Mz 0.07; Mz 0 past 0.21); 272 kerb events and 457 slip frames, all with the
    vibration; none over the ceiling.

**D9: Spatial audio (L, lane D).** The audit (section 4.2, design 3) gives the design but no acceptance; this is it
(the architect, 2026-11-08; lane D reviewed). `audio.spatial = original | stereo | hrtf`, behind `audio.mixer =
host` (D5). `original` in every preset while `audio.mixer` defaults to `game` (spatial does nothing there and would
read as unused, as `audio.interpolation` does); the change that makes `host` the default (after the Odin run, C6b, the
coordinator's call) gives low to ultra `stereo`. `hrtf` is the player's choice (it assumes headphones).
- **How GT4 does it** (lane D's note, `spikes/D9-voices-to-entrants.md`): voices are allocated dynamically and no
  voice record holds a car; a voice reaches its car only through its owner (voice record +0x10: an engine pool
  entry or a `SePlayer`'s playback object inside the car's `RaceCarSound`, RaceEntryCar + 0x2900). GT4 already
  pans (equal power, with a phase inversion behind the listener), attenuates by distance and computes a Doppler
  factor for every car sound, from each sound group's listener-relative position (`RaceCarSound`'s `H` blocks,
  written every frame by 003B5E80's chain).
- **Tags:** game-side. Our `func_003B5E80` copy registers each car's sound objects (engine pool entries,
  `SePlayer` playback objects) with the entrant and its group's `H` block; at each SPUP image the runtime looks up
  each voice's owner and sends the tags beside the image (`GT4_SPU2_TAGS`, a new `GT4_REC_SPU2_REGS` kind; per
  voice the entrant or none, and the position; about 0.8 KB a frame; nothing allocated). Untagged voices (UI, BGM,
  crowd, pit crew, sequencer tracks, narration) stay a bed, mixed as `original` mixes them.
- **Directions: GT4's own** (the architect, 2026-11-08, on lane D's proposal): each tagged voice's group direction
  from the `H` blocks, in GT4's listener space, of the same frame as the image. An `H` block holds a direction,
  not a position (func_00391AF0: x and z over a distance-like scale, the raw y, the flips); distance stays in GT4's
  gain (below). So each group has its own direction, spatial never disagrees with the direction GT4 intends, and
  D9 needs no scene records.
- **The listener is the player's car** (lane D's step 1, 2026-11-08): its position and heading, not the camera.
  `H`'s azimuth matches the `Vehicle` records' direction of each car in the player car's axes within about 1
  degree, cars 50 to 650 m away. Spatial keeps GT4's listener: in the chase and in-car views it agrees with the
  camera except within a degree or two of straight ahead or behind, and in TV-camera replays it follows the car,
  as GT4's own pan does.
- **Level:** a tagged voice's level is the power sum of the image's `VOLL` / `VOLR` (signs dropped): GT4's pan and
  rear inversion removed, its class volume and distance gain kept. Spatial adds direction only: no second distance
  curve, and no host Doppler (GT4's own is applied; step 1 confirms it from a passing car's pitch). A voice in
  sweep mode stays in the bed.
- **Rendering:** `stereo` pans tagged voices with equal power from their azimuth, without the rear inversion;
  `hrtf` renders them through an HRTF library and dataset whose licence lane C checks (no Sony data). Occlusion is
  not part of D9.
- Depends: D5, D13 if the output device decides `hrtf`. Acceptance:
  - `ORIG`; `DIFFTEST` (the tags come from game code); with `audio.spatial = original`, a session's `audio.wav`
    bit-identical to D5's host mix.
  - Tags, on a race session with six cars, counted offline from a trace of STRP's loads into SPU RAM and every
    key-on's sample start address (SSA). The entrants' own banks (two per entrant, from the cars' disc region)
    hold only the engines; the tyre and other car effects play from banks shared with course, crowd and pit sounds
    (lane D, 2026-11-08). So, two rates, each at least 99 %:
    - engines: the key-ons whose SSA lies in the entrants' banks, tagged; this one doesn't depend on the tags;
    - car effects: in the shared banks, the key-ons of samples that tagged voices also play, tagged. The shared
      banks' other untagged samples are listed with their key-on counts, and the most played are identified (by
      ear, or by the request that starts them) as not car sounds; the worst case, all of them car sounds, is
      reported beside the rate. Left out of this rate, and listed with their count: `RaceCourseSound`'s key-ons
      (00398B70, the player's own wheels on the course's surfaces, played by the course at the listener with L
      and R GT4 already panned), found in the trace by their owner being one of its players. They stay in
      the bed, untagged, as GT4 mixes them (lane D, 2026-11-08);
    - and no BGM, UI or narration key-on tagged. Counted per frame (`voicesTagged`, `voicesUntagged`,
      `voicesNoPosition`); gaps the note names (e.g. a shared tyre-squeal voice) are listed with their share.
  - Direction: a session in which an AI car passes the player; in each 100 ms window where the car's |x| in the
    player car's frame (its right axis) exceeds 2 m, the sign of its tagged voices' left-right level difference
    matches the sign of x in at least 95 % of windows, for `stereo` and `hrtf` (x from the two cars' `Vehicle`
    records, with scene records on for this session only).
  - Distance: as that car pulls away on a straight, its voices' level follows GT4's own distance gain within 1 dB
    (spatial adds no attenuation of its own).
  - Balance: a lap's integrated loudness (EBU R128) within 1 LU of `original`; the bed alone identical to
    `original`'s untagged voices.
  - No audio drop in the host summary; the audio thread's time per buffer reported (`PERF`); `STREAM`'s extra
    bytes for `GT4_SPU2_TAGS` reported.

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
- **Closed, not landed (lane C, 2026-11-09): the clamp changes nothing on today's main.** Ported onto main's
  `fieldPoint` (every displayed sample) and built beside main from one tree, it gives the same frames as main on
  `menus_boot`'s text menus (the title menu, Original Circuits' row, City Courses' row; 17 frames, 1280 x 720) at
  `original`, `high` and `ultra`, in `display.field_mode = blend` (every preset's since 27f5729) and in `clean`
  (where one field's rows are sampled alone): 17 of 17 identical at each, in both. So the bilinear pull E3 was
  written against doesn't occur on main's display any more. A faint
  row in blend, if one shows, is the field modes' question (E4, E10), not this clamp's. The change as it was found
  is parked: `parked/e3-display-clamp` (7b7bada1, on 1b495a6).

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

**E10: `display.field_mode = clean` without a bob or flicker (M; lane B; parked).** clean (6df8aa1) blends where the
two fields agree and shows the newer field alone where something moved; that field sits half a line from the other, so
moving content bobs a line a frame (the owner's menu shake on the Odin; lane B, 2026-11-09). It left every preset
(27f5729: `blend` again). Parked on origin as `parked/e10-clean-still` (eeb298b): the field shown alone moved half way
by its VBlank parity, which fixes the moved-area bob (`menus_boot`'s title window: 30 of 30 frame pairs moved before,
0 after). Left: still thin edges that the motion test calls moved alternate fields (the GT map's icons and text: the
one-frame against two-frame ratio blend 0.65, clean 2.6, the parked fix 2.19); the fix is a same-parity motion test,
each field compared with its own previous field (two kept field images in `GsnScene`, the shader reading them). clean
returns to a preset only with 0 pairs moved on `menus_boot`'s GT map (3000-3030), its title (6450-6480) and an intro
window picked on it (the intro movie itself), the edge ratio at blend's on the GT map, the hidden-line check, and a
fields-mode race with no new shake.

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
- **Checked** (lane F, 2026-11-09): Clubman Stage Route 5 in the Normal direction (`lap_csr5.cap`, B-spec, over two
  laps, the GS path at `original`, every frame from 600): 31 level shifts of 5 % or more, none with one frame carrying
  70 % (the largest share 57 %, at +5.9 %; the largest shift, the tunnel's -70.5 %, carries 18 % in one frame), so the
  lap-repeat test isn't reached and N2 stays parked. With Route 5 Reverse (R10) and Clubman forward the Route 5
  layouts are checked; GT4's lamp switch (`kFrameLampsOn`, N3) is also on at Hong Kong (dusk), the 'one more city
  course' the reopening names, so it is checked there too before the reopening check counts as done.
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
- **Step 0, done** (lane F, `spikes/N3-lit-road-rule.md`; the architect accepted v4, 2026-11-09). The layer's state
  also matches road markings, the start/finish checkers, logos and crossings by day, and a chain-link fence, so the
  rule is per item at first sight, cached by (material, item), and only on frames flagged `kFrameLampsOn` (CONTRACTS
  FrameFlags: GT4's own car-lamp switch, lane A, 2026-11-09), and only in `kViewMain` views. The flag is set on all
  four Route 5 layouts (`crs/route5`, `route5_club`, `r_route5`, `r_route5_club`), on Hong Kong at dusk and on the
  Arcade menus' 3D background, and clear by day (Seoul, a dirt course). An item is lamp light when it has the layer
  state; a vertex colour (its mean over the item) of luminance over 16 and alpha at least 100; a texture centre/rim
  under 3 (not a glow); at least 0.9 of its area with |n.up| >= 0.9; a footprint width of at least 0.9 m (2 x area /
  perimeter of its up-facing pieces; markings reach 0.60 m); and at least 0.03 m per texel (the checkers are 0.017 m,
  the pools 0.039 and up). The proof is exhaustive, since GT4's night layouts are a closed set: proven on Clubman
  Stage Route 5 and Special Stage Route 5 Reverse, both full B-spec laps, every item judged in a debug host, the skip
  imaged against the base, and only lamp light changes (on mid-run replays; their verdicts are rechecked per the
  acceptance below). Every other course with the flag (C.S. Route 5 Reverse, S.S. Route 5 forward, Hong Kong at dusk)
  is hunted the same way (B-spec if an event runs there, else throttle segments) before N3 counts as proven there: the
  same textures, but other geometry, so other items. S.S. Route 5 forward is hunted (lane F, 2026-11-10, on
  `lap_ssr5f_boot`, from game start): 196 items in 50 identities, the same three families; orange bands under an
  overpass (frame 9,725) traced by bisection to the amber pool texture a93b91b4da486282, already proven lamp light on
  the reverse layout, its items passing every v4 test with room, so the band shape is the geometry between the
  overpass's beams. C.S. Route 5 Reverse is hunted on its mid-run lap (`lap_csr5r`: 230 items skipped in 38
  identities, the same families, only the road's light layer changing in the 305 changed frames), the true-colour
  rerun on `lap_csr5r_boot` pending; Hong Kong at dusk waits for a driven lap. A pool sliver under the width stays
  baked (0.058 % of pool area), the safe miss. The texture-locator fallback is parked in the spike; no hash list
  exists. The debug host and tools of the hunts and the never-dark sweep are on origin as `debug/f-n3-debug` (README
  in `tools/lanef/`), deleted when N3 closes.
- **Step 1, the mechanism** (lanes F and B write it together, lane B the back end's skip; lane A the flag): the
  verdict per (texture, mesh item) in `SceneCache` (`platform/scene/scene_cache.h`), beside the `MaterialTable`: a map
  keyed by (the texture's identity, its content's: `GsFrontend::textureIdentity` mixes the GS layout fields with the
  hash of the texels, so other content at the same place is a new key; the mesh's `MeshKey`; item index), what the
  verdict reads, the layer state still tested per material first (lane F, 2026-11-10: a `MaterialId` key was renewed
  for a material unseen 300 frames, so the course was judged again as the car came back to it, B6's front end at 620
  against 222 us a scene frame; verdicts are kept 36,000 frames unseen), filled in `SceneCache::litRoad`, called from
  `GsFrontend::sceneView` at each view's `ViewEnd` on the front end's thread, and delivered per item on
  `FrameList::ViewOnList` as a flag parallel to `materials`, which the GS back end reads when it executes the view's
  modeled draws (lane B). A new key is judged there synchronously, not on the mesh worker: the cheap material tests
  first (the layer state, a known texture summary and its centre/rim under 3), then only the items passing them get
  `decodeBatch()` from the batch where it lies (`ItemSource::batch`) for the vertex colour, the up-facing share,
  footprint width and texel size; memoised, so each key is judged once. A view's `ViewEnd` comes before its list is
  cut, so the verdict is there when the draws execute: 0 frames late. Waiting for the mesh worker would be late by the
  decode, several frames at a course's first frames, the pop N3 must not have. The one exception is a list cut inside
  a view (a read in the middle of the main view; none expected at night): items drawn before their verdict keep GT4's
  look (not counted apart today). The up-facing share comes through the instance's `objectToWorld` at the item's first
  sight, since a mesh is shared between instances and only the share depends on the rotation (every lit-road item has
  an identity rotation today); an instance of a judged item whose rotation differs counts in `n3LaterFail`, so a
  tilted copy can't inherit a flat verdict. The GS path skips the item's draws in a `kViewMain` view on a
  `kFrameLampsOn` frame with `lighting.night = enhanced` only; `original` never builds the map (it runs only with the
  scene split on). Counters per frame: `n3Judged`, `n3Skipped` (items and draws), `n3SkipGatedOff`, `n3LaterFail` (a
  later batch or instance of a cached item that would fail), `n3Misaligned` (a view given no flags because they
  wouldn't line up with its materials); the judging's cost in B6's front-end CPU line. Where it is (2026-11-10):
  the verdict map is on main (lane F's eadea5b, reviewed by lanes A and B, rebased unchanged), off by default, with
  one change its gates asked for. On `lap_ssr5f_boot` to 20,200 the map as it was judged 216,469 items for 437
  distinct measures, about 16 a frame for the whole race, and B6 cost 514 us a scene frame against 217 off: road
  markings whose textures change identity as the race goes (6,673 identities over 189 meshes), each new identity a new
  key. An item whose geometry alone fails the rule (the vertex colour, the up-facing share, the width) is now
  remembered per (mesh, item) with its `objectToWorld`, and at the same place under a new texture it gets its keep
  without a measure (`n3Judged` and `GT4_N3_LOG` count measured verdicts). After: 2,217 judged, the same 50 identities
  and 267,433 items skipped, B6 261 us on against 228 off; `CTEST`, `ORIG` and `NATIVE` (byte-identical to main's host
  with the verdicts off, `race7` and `lap_ssr5f_boot`) pass. The stand-in albedo under lane B's mask is parked as
  `parked/f-n3-standin` (29f25bb, on the old `MaterialId` key, measuring only, never reviewed or gated): one mean
  road colour for a whole course is not what ships, so the albedo under a skipped pool comes from B10's colour
  channel (the paragraph on the albedo below, its last sentence), built first (STATUS's finish line, 2026-11-10).
- **The coverage gate, per pixel** (2026-11-10; on main behind `GT4_N3_DEFER=1`, replacing the per-item disc test
  below, which was never built): the stills and the never-dark measure showed the skip leaving the road dark wherever
  no lamp glow stands (`lap_ssr5f_boot` 12,000: 0.26 of GT4's light whatever the lamps' strength), and an item is lit
  or bare as a whole. Instead the skipped items are drawn last: before the scene's Light the scene's colour is copied
  (`preLayer`), then the list's commands since the target's previous Light run again drawing only the skipped spans,
  each with its own draw's state. The light pass has the road with GT4's baked light and without it; their
  difference is the baked light at the pixel; our lamps light the surface without it (the albedo from the scene
  without the layer); and the pixel gets ours where ours reaches GT4's level, GT4's where ours is under half of it, a
  blend between: never under about 0.87 of GT4's, never both, no item popping. A wrong verdict now costs little:
  the item's contribution is treated as light and blended, not removed. Measured with F7's street-lamp profile
  (`GT4_LAMP_PROFILE=40,0.9,60,72`), hybrid, `GT4_NIGHT_SIDE=1`, 30 frames of `lap_ssr5f_boot`: ours over GT4's under
  the layer per frame 0.97 to 2.47, median 1.18; of 3,734 32-pixel blocks none under half of GT4's (41 % with the
  skip alone), 5 % under 0.95, 11 % over twice GT4's (lamps brighter than GT4 baked there: the strength's and the
  owner's look to settle). Owed before the switches go: the other night laps and Hong Kong measured the same way;
  the layer's place in the order (it is drawn after the blended draws GT4 drew over it, and after a grade between it
  and the Light); a Light with no range before it (no replay: the skip and its mask as before, uncounted);
  `lightProfile` in the GPU-against-C++ test; CONTRACTS c.1's profile bits and `Light::reserved`'s host use written
  down. **Since the same day the switches are the settings' own behaviour**: `lighting.night = enhanced` with
  `render.scene = hybrid` (`nightModernWanted`) runs the verdicts, the side-channel relight, the profile and the
  per-pixel blend; the switches remain as overrides (`GT4_NIGHT_SIDE=0`, `GT4_N3_DEFER=0`, `GT4_N3_VERDICTS=0|1`,
  `GT4_LAMP_PROFILE=off` or four values). The profile's second form lights the road the same from any mounting
  height (Clubman reverse's tunnel was at 3 times GT4's with the batwing alone; 1.69 after), default strength 0.8.
  At the defaults, per 32-pixel block: S.S. Route 5 forward median 1.11, Clubman reverse 1.00, none under half of
  GT4's on either, 10-11 % over twice; S.S. Route 5 reverse at the first form's 0.9: median 1.00, none under half.
- **The coverage gate, per item** (the architect, 2026-11-09, from lane F's never-dark phases; lane B's review; not
  built, superseded by the per-pixel form above, kept for its reasoning): a skip also
  needs a lamp that replaces the light it removes, so the road is never darker than GT4's where nothing of ours lights
  it (a tunnel's lights are not lamp billboards). It decides at execute time in `GsnScene`, with the frame's own
  lamps: (a) a skip is a vertex span per item, and `planSkips` finds each item's instance through `ViewOnList::ids`
  (`firstMaterial`, `materialCount`), so the front end's merging stays as it is (lane B); (b) `GsnScene` promotes the
  lamps at `executeBegin`, per main view keyed by view id, and `nightPrepare` reuses that result, so lamps are not
  promoted twice and `LampTotals` don't double (a Light whose target no view binds gets no lamps, as today); (c) each
  candidate's footprint, the centre and radius of all of its item's decoded vertices in object space (the skip removes
  the whole item), is kept in lane F's verdict entry, whose key (texture identity, `MeshKey`, item) fixes the
  geometry, and delivered as `ViewOnList::skipFootprint`, one float4 per material parallel to `skip` and written at
  `firstMaterial` like it (0 where `skip` is 0); the back end takes the centre through the instance's `objectToView`
  each frame and the radius times the instance's largest axis scale. The disc test runs in view space, where the
  Lights already are (`gfx::scene::Light`, kept so by `promote()`): no inverse is needed. A candidate's draws are
  skipped only if one lamp's ground disc holds the whole footprint (distance from the lamp's foot plus the footprint's
  radius at most the disc's radius, min(h x tan(theta_c), sqrt(range^2 - h^2)) with h the lamp's height above the
  footprint's centre: theta_c from the light's own cone, `cosInner` = cos(theta_c), where the lamp is at full
  strength, not the roll-off's end, and range the light's own, or its source's default when the record has none
  (gfx.h's 0, unknown), as the gather uses, so the disc never reaches past where the lamp lights the ground (h above
  the range: no disc, held); a lamp without the street-lamp profile, as F2b's point lights are until the batwing lands
  with the skip, gives no disc, so nothing is skipped and every candidate counts as held for no disc; the level where
  our lamp falls below the baked pool's replaces this geometric rule once the stand-in run measures it; the heights
  and the lamp's foot measured along world up as seen in view space (the view's worldToView applied to (0, 1, 0)), not
  along view y, which tilts with the camera's pitch); otherwise GT4's baked draw stays. Everything stays baked on a
  frame where the view's lamps can't be trusted: a view not seen last frame (matched by role and `pass`, as
  `previousWorldToView` is), a scene switch, `kViewCut` once something sends it (nothing sets it today, so a mid-race
  camera change is caught only when the view drops out for a frame), and a list cut before `FrameEnd` (a.8: the
  flare's asynchronous read), which has no scene frame and no lamps. Counters per frame: candidates, skipped, held
  back for no covering disc, and held back for a partial list or a view not yet trusted, apart, so a flicker on read
  frames doesn't read as a gate fault. The cached verdict above is unchanged; the gate is an extra test on top of it.
- **The albedo under a skipped pool** (2026-11-09, from lane F's never-dark phases 4-5): any albedo estimated from the
  picture after the skip is circular, since the skip has just taken the road's light out of it (with the skip, lane
  F's split: near road 0.50-0.52, far road 0.52-0.70, highest where the skip left the darkest blocks), so no lamp
  intensity passes never-dark on both sides with F2b's estimate (per block: the far road lifted; one level: the lit
  road washed out). Under a skipped item the relight takes the albedo of the base surface beneath it, which stays
  drawn (the skipped item is GT4's light layer, so its own texture is the light's colour). Its first piece is lane
  B's: an R8 mask of the skip's per-pixel coverage, written by the candidate draws and read by `gsn_light`, built as
  part of step 1, not as debug code. On it, lane F measures with a stand-in (the course's road base textures' mean
  colour, linear, from the texture summaries) and runs never-dark on all three laps. If that passes, the shipped
  albedo is a side channel written by the base surfaces where they are drawn (B8's side passes), which keeps the
  markings' detail; if not, N3's skip waits for B10's material colour.
Depends: R11, A7, B4, B6, F2, F2b. Acceptance: the step 0 hunt repeated on the lap of every course with the flag (Hong
Kong at dusk included) with the mechanism in place of the debug skip (only lamp light changes, every marking and the
checkers kept); v4's verdicts judged on what the live game draws, not a mid-run replay's textures (v4 reads the
texture's centre/rim, and a mid-run capture can hold wrong texture content, RISKS R7: lane F, 2026-11-09): either a
live run's verdicts per identity against the replay hunt's, or the hunt on a night capture recorded from game start
(C10); `R5` on `lap_ssr5r_boot`, the from-boot night lap whose colours equal live (band ratio at most 1.3 in every
frame, the GS path's value for comparison); no skip on a frame without `kFrameLampsOn` (Seoul by day: 0) nor outside
`kViewMain` (the Arcade menus' background: 0); the never-dark check on both sides on all three laps (the skipped
patches against the GS path, lane F's measure and bounds, stated with the rule), with the gate's held-back count per
lap; `PERF` with night enhanced against off, the gate's cost in it, and the front end's batches with and without the
gate on `lap_ssr5r_boot` (the extra draws of the unmerged candidates); `ORIG` unchanged.

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
`lighting.night = enhanced` was set by `--set` only, in no preset, so a release never lit the road twice (since
2026-11-10 it is in medium, high and ultra with `render.scene = hybrid`, N3's per-pixel form: never twice). Three
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
- **Done** (lane F, `spikes/R11.md`, 94ce8e0): both night courses, full B-spec laps, 0 misses and 0 lights over the
  cap (drops only in a replay's re-upload window); `PERF` night enhanced against off on Clubman's lap: GPU +5.0 %
  mean, the front-end thread's wall clock +30.6 %, of which B6's own thread CPU is p95 359 us, 141 us under B6's 0.5
  ms budget; `ORIG` identical to the R6 hash lists on race7, race_grid43 and race_boot; with records off the scene
  cache is never created and the lamp pass runs 0 times. Before `lighting.night = enhanced` enters a preset: the
  Odin's measurement of B6's span (B6c, C6b), as for hybrid.
- **The relight's albedo** (2026-11-09): the estimate (colour over the 8x8 block's log-average, times 0.18) inflates
  the albedo to about 0.7-0.8 on near and far road alike without N3's skip (lane B's review images; near 0.83, far
  0.71): a global scale, which F2b's calibrated I0 already absorbs, so F2b's look needs no amendment. Tried and not
  landed: one level for the whole view (`parked/f2b-view-level`, 6d077a0), which made our light land hardest where GT4
  already lit, washing out the lit road (lane B's review images), and a floor under the block level at a fraction of
  the view's (k 0.3-0.7; `parked/f2b-albedo-floor`, 8a655b5), which barely binds, since a dim road block's log-average
  is above half the view's. Left as a possible small change: a lower key (0.09) with I0 doubled, the same picture with
  fewer pixels at the 0.9 clamp, texture kept in the brightest lit areas, with its own images. The true albedo, which
  N3's skip needs, comes from the surfaces' own colour (N3 step 1, B10).

**F3: Clustered lighting in the deferred and forward passes (M).** Depends: F1, B10, B22. Acceptance: `VAL`;
`PERF`; the same lights as F2 in `modern`, with G-buffer normals.

**F4: Bloom and eye adaptation on linear light (S).** The pipeline's bloom and histogram exposure on the modern
scene. Depends: B15. Acceptance: `VAL`; exposure follows tunnels and night sections without a step (N1 step metric
on the sections capture); the warning capture's tint stays below N4's bar.

**F5: Wet roads (M).** G-buffer modifier, puddle masks, spray from emitters. Depends: B10, B13, B18. Acceptance:
`VAL`; wet variants only; review against the wet capture.

**Modern racing lights (the owner, 2026-11-09).** The light systems match what modern racing games do. Each ticket
below: `ORIG` untouched (`original` never runs it); a setting with presets, in no preset until its acceptance and the
owner's review; `PERF` against a desktop budget at 1440 x 1080 on the RX 7900 XTX and an Odin budget at 1280 x 800
measured in C6b (each setting has a value that fits the Odin's frame, or is off there, and the device
preset says which); all data our own, generated by our tools, no Sony or Polyphony assets. Order in phase 3: B10, then
B11 (the sun, and the shadow atlas F6 shares), F3 (clustered lighting in `modern`), A7 step 2 (headlights, tail and
brake lights as lights), F7 (profiles), F6 (local shadows), B14 (froxel fog), F8 (volumetric), F4 (bloom and flares),
F9 (emissive lamps), F10 (the night's ambient from the place, with N3); F5's lit wet road in phase 4. Light-list
fields (CONTRACTS c.1): `LightFlags` keeps
`kCastsShadow` and `kVolumetric`; the flags' upper bits gain a profile index (bits 8-15, 0 none) and a shadow-atlas
slot (bits 16-27, 0 none), so `GpuLight` stays 64 bytes. 'Lamps on' is one switch, `kFrameLampsOn` (CONTRACTS
FrameFlags: GT4's car-lamp switch, course flag 0x8000): A7 step 2's on state, F6's headlight shadows, F7's beams and
F9's emissive all key on it, so headlights, their shadows and their glow can't disagree. Temporal stability, where a
ticket asks for it, is N1's level-shift test as N2's reopening uses it (a shift of at least 5 % with at least 70 % of
it in one frame), counted with the setting on against off on `lap_ssr5r.cap` and `lap_csr5.cap`: no new shift. Lane F
owns F6-F9, lane B reviewing (the G-buffer, B11's atlas and B14's froxels are lane B's).

**F6: Shadows from local lights (M-L, lane F; lane B reviews).** The player's headlights cast shadows (the car ahead,
barriers, posts) and the nearest street lamps cast the cars' shadows onto the road. A shadow atlas shared with B11's
cascades, which B11 owns and allocates each frame: fixed regions for the sun's cascades, then F6's local lights in the
rest by priority, a stated budget of slots a frame; a light with no slot is drawn unshadowed and counted. The budget
of slots (the setting `lighting.shadows.local = off | player | near | all`: the player's headlights; plus the 4
nearest lamps; plus up to 16 lights), chosen by importance (screen-space contribution, nearest and strongest first)
with hysteresis so a light doesn't flicker in and out of the budget. 'The player' is the car each main view follows:
the player's entrant in a race; in 2 Player Battle each half's car, both players' headlights; in a replay the car the
replay shows (lane A): the target car of the camera the main view opens from (0033EAB8, from RacePS2Base method 0xD0's
camera, the same call in every case), sent with the main view's `ViewBegin` in a spare field (the followed entrant,
0xFF none; a CONTRACTS change with it). A step 0 reads the camera's target on a race, a 2 Player Battle and a replay;
in a single race entrant 0 is the player, which checks it. Headlights and street lamps are spot shadows (perspective
maps); a point light gets none. A street lamp's spot points down from its glow's centre (F2b's lamp, often on an arm
over the road), its cone angle from F7's street-lamp profile, not a number of its own. Casters: the cars (their
mesh-cache meshes through each instance's `objectToWorld`) and the course's instances near the light; depth-only draws
in a modern vertex stage (shadow maps needn't match GT4's Z). Receivers: B10's deferred lighting and F3's forward
pass; F2's night relight in `hybrid` samples them too. Filtering as B11's (PCF, contact-hardening optional),
normal-offset bias. Depends: B11 (the atlas and filtering), F3, A7 step 2 (the headlight cones), F2b (the lamps),
B6/B7 (meshes). Acceptance: `ORIG`; `VAL`; `PERF(<= 1.5 ms)` desktop at `near`, Odin budget at `player`; review images
on both night courses: the player's headlights on the road (F6 turns `kSrcHeadlight` on in the gather, which A7 step 2
left off; moved from A7 step 2), the car ahead's shadow in the player's headlights, cars' shadows under the street
lamps, no acne or detached shadows; temporal stability as above, as lights enter and leave the budget.

**F7: Light profiles (S-M, lane F; lane B reviews).** Its street-lamp part is on main behind
`GT4_LAMP_PROFILE=RANGE,SCALE,INNER,OUTER` (2026-11-10): an analytic profile in the light list's flags (bits 8-15),
`kProfileStreetLamp`, a road luminaire's batwing (1 / cos^3 of the angle from the axis, held at 16), so a lamp
pointing down lights the road evenly inside its cone; the course lamps become spots along the world's down axis. The
cookie textures below remain for an asymmetric cut-off line. Since then (the same day) the profile lights the road
the same from any mounting height, and **the headlights are lit** on the night's modern path (F6's gather switch):
A7 step 2's spots get a beam in `GsnScene::lampsOf` (tipped down 4 degrees, full to 14 and ending at 30, 50 m) and
`kProfileLowBeam`: the inverse square cancelled out to the range, and a wrapped diffuse term for the road's answer at
grazing incidence (`lightProfile`, `lightDiffuse`). They pass through what stands in the beam until F6's shadows.
Headlight beams and street lamps get shapes. A profile is
an angular intensity table (IES-style: intensity against vertical and horizontal angle) baked into a small 2D
cookie (a slice of a texture array, indexed by `GpuLight`'s profile index): the low beam with its hot spot,
asymmetric cut-off line and spread; the high beam (used where the game's car light state says so, else low only);
street lamps by their F2b look (downlight, wide road spread). Authored as parameters in `tools/light_profiles.py`
(our own numbers, from published beam-pattern regulations' shapes, not measured from GT4 or any vendor's data); the
textures built at start-up or by the tool into a generated header. The cone and falloff functions of
`light_list.glsl` multiply by the cookie. Setting `lighting.profiles = off | on`. Depends: F0, F1, A7 step 2, F3 (F2
reads them in `hybrid`). Acceptance: `ORIG`; `VAL`; `PERF` (the cookie lookup <= 0.1 ms); review images: the low
beam's cut-off line on the road and on a wall, no light above it on the car ahead, the street lamps' pools shaped
as the profile; `CTEST`: the tool's output reproducible and each profile's integral within 1 % of its stated flux.

**F8: Volumetric light (M, lane F with B14; lane B reviews).** Lamp and headlight scattering in B14's froxel fog, on
one grid: B14's froxel depth slices are F1's cluster slices, so the lights per froxel are F1's per cluster, with no
remap (lane B designs B14 on F1's slicing): in-scattering per froxel from the clustered lights with `kVolumetric`
(profiles from F7, shadows from F6 where the light has a slot, so headlights and lamps make shafts), a phase function
with forward scattering, temporal reprojection with the froxels' history. The medium: GT4's own fog density and colour
per section (B14) plus a night haze setting, never invented where GT4 has none. Setting `lighting.volumetric = off |
low | high` (froxel resolution and lights per froxel). Depends: B14, F1, F7; F6 for shafts. Acceptance: `ORIG`; `VAL`;
`PERF(<= 1 ms)` desktop at `high`, Odin at `low` or off; review images: headlight beams and lamp cones in night haze
and fog; no flicker (temporal stability as above).

**F9: Emissive car lamps (S, lane F; lane B reviews).** Headlight, tail and brake lamp lenses glow from the light's
state, not from GT4's baked lens texture alone: first a probe, as R8's for bodies or F2b's texture summary, since no
record names a lamp lens: it finds the lens materials per car, kept as a class bit with the material id (lane B's
MaterialTable). Then the emissive comes from that class bit and A7 step 2's light state (on, brake) per instance in
the lighting pass, before F4's bloom; no new G-buffer channel. GT4's own lamp glow sprites stay where `kGameGlow` says
so (no double glow). Setting part of `lighting.materials = pbr`. Depends: B10, A7 step 2, F4. Acceptance: `ORIG`;
`VAL`; `PERF`; the brake lamps follow the brake state day and night, as in modern racing games (the coordinator for
the owner, 2026-11-09), while `original` keeps GT4's way (brake glow only with the lamp switch on). By day a brake
lamp's glow lights only the lamp itself and feeds bloom; it lights the car behind only above a small level threshold.
Review: brake lamps brighten under braking on the car ahead, by day and at night; under braking the car behind lit red
(F9 turns `kSrcBrakeLight` and `kSrcTailLight` on in the gather, which A7 step 2 left off, and decides whether step
1a's `kSrcCarLamp` stays, off too until F9 since c92b64c: lit at an untuned strength all round each car, it painted
the road red in the owner's Odin playtest, and magenta at the same curve on the desktop; moved from A7 step 2);
headlights and tail lamps glow only with the switch on; on the warning capture the tint stays below N4's bar.

**F10: The night's ambient from the place (M; the owner, 2026-11-10).** "If the track is in the middle of nowhere
it's darker than in the middle of a city": at night the light that isn't a lamp's or a headlight's is the place's own,
and no lighting ticket may add or assume one level for every course. Today the night relight (F2) adds lamp light only
and leaves GT4's picture as the ambient, which already differs by course; this ticket keeps that true as more of the
picture becomes ours (N3's skip, B10's materials at night, F4's exposure), and gives those a measured term to use.
- **Step 0, the measurement** (no picture change): per main view of a `kFrameLampsOn` frame, two numbers, logged and
  in `GT4_GSN=stats`: the sky's level (the mean linear luminance of the view's sky draws, which the GS path still
  draws: found by what they are, as N3's layer is, or by A5a's markers if they bracket it) and the lamps' level (the
  summed flux of the view's promoted lamps within a stated distance, F2b's lights with their I0). Reported per
  course on the night laps (`lap_ssr5r_boot`, `lap_ssr5f_boot`, `lap_csr5`, `lap_csr5r_boot`), Hong Kong at dusk
  and, for the dark end, a lap of a course whose flag is set with few or no lamps if GT4 has one (step 0 lists the
  courses with `kFrameLampsOn` and their lamp counts; if every one is a city, it says so, and the dark end is checked
  on a tunnel-free stretch between lamps).
- **Step 1, the ambient term** (`light_list.glsl`, `gsn_light.comp`; CONTRACTS c.3): an ambient radiance per view,
  sky level plus a stated share of the lamps' level (the city's glow on what no lamp reaches directly), smoothed
  over about a second so a lamp entering the view doesn't step it (N1's level-shift test), in linear light. Its
  users: the albedo under a skipped pool (N3: the road there must come out at GT4's unlit level for that place, not
  at a constant); B10's relit materials at night (their ambient, in place of the day's 204); F9's emissive lamps'
  threshold; F4's exposure when it lands. It never lifts a pixel our renderer hasn't taken over: a course without
  lamps keeps GT4's own darkness, and the headlights (F6's gather switch, F7's beam) are what light it.
- **No constants per course and no course list:** the term is computed from what the frame holds, so a mod's or
  another region's course gets the same treatment.
- Depends: F2, F2b; N3 for its first user. Acceptance: `ORIG`; `VAL`; `PERF` (the measurement under 0.05 ms); step 0's
  table in the ticket; with step 1, the never-dark check of N3 on both sides using the term; temporal stability as
  above; review images, a lit city stretch against the darkest stretch found, side by side with PCSX2's: the dark
  one no brighter than GT4's away from our lights.

Additions to existing tickets, with the same rules:
- **A7 step 2** (done: lane A, fab0402, 2026-11-09): headlights, tail and brake lamps sent as lights with their state.
  Step 0 (a night B-spec race): `RaceCarModel` virtual 35's lamp groups, at `*(*(model + 0x18) + 0x10)`, are group 0
  the headlights (count at +0x18; usually 4 elements, two a side, each with a model-space point, a beam point
  0.15-0.45 m ahead of it, and a size), group 1 the brake lamps (+0x28; the rear pair), group 2 the tail lamps (+0x20;
  the same pair). The state: on is the course flag 0x8000 (`kFrameLampsOn`); the brake state is car + 0x590, 0 or 1;
  the brake lamps' intensity is k (RaceCarModel + 0x54), which GT4 ramps 0.25 a frame up from the brake's start and
  down after its release. Every car's elements go out every frame, read from the car model, since virtual 35 doesn't
  draw every car every frame (by day, and the player's car in an Arcade time trial at night), as spot Lights of the
  main view: `kSrcHeadlight` along its beam, `kSrcTailLight` and `kSrcBrakeLight` pointing backwards, range and cones
  0 (F7's profiles decide), id `entrant << 8 | group << 5 | index`. Placement: each model point through CONTRACTS'
  Vehicle mapping (body = (-z, x, y), d0ad4a3) plus a per-car offset along row 0, calibrated from GT4's own draw
  transform the first time it draws the car's lamps (per car, 0 to -0.39 m on the night run's six, each 0.000 off row
  0; a default of -0.31 m until then, counted), then the Vehicle's `bodyToWorld` and the main view. Intensity:
  headlights and tail lamps 1 with the switch on, else 0 (still sent); brake lamps k with the switch on, else a ramp
  of 0.25 a frame from car + 0x590 (GT4's own ramp), day and night (F9's day brake glow). They light nothing yet: the
  three sources have `lights = false` in the gather until F6 (headlights) and F9 (brake and tail lamps) tune them, so
  `lighting.night = enhanced` shows step 1a's `kSrcCarLamp` glows as before. Acceptance, met: at night 56 lamps a
  frame for 6 cars, on step 1a's glow points within 0.0002 m, 6 calibrations and none rejected; by day headlights and
  tail lamps 0 and the brake ramp 0.25 a frame. Its two lit reviews (the car behind lit red under braking, the
  player's headlights on the road) moved to F9 and F6. The 2 Player Battle check: done with A12 (ae033d2), both cars'
  lamps in both halves.
- **F4**: lens flares and streaks from real lights: from the light list's brightest sources (occlusion from the depth
  buffer), replacing GT4's flare pass in `modern` (N4 suppresses it there); acceptance adds the flare's occlusion
  behind a car (review), no flare from a light outside the view, and on the warning capture the tint stays below N4's
  bar with the flares on, so modern flares don't bring the warning-light tint back.
- **F5**: the lights on the wet road: stretched specular highlights of lamps, headlights and tail lights from the
  clustered lights on the wet G-buffer and SSR (B13); acceptance adds the reflections' streaks on the wet capture.

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
| **M3** UI renderer, in SDR on E5's overlay primitives (HDR at `display.hdr_paper_white` until M3b; the asset packer moves to M10) | M | E5 |
| **M3b** The UI on E6's 2D layer, HDR at paper white | S | M3, E6 |
| **M4** Settings bridge (and M7's Settings-in-a-race checks) | S-M | M1, M2, E1; M7 for those checks |
| **M5** Game-side page host (`runtime/menu/`; the pad focus in `iop.cpp`, lane D reviews) | M | M0, M1, M2 |
| **M6** Options entry (a copy of 00109D00: both ways into GT4's Options reach `startProject`; the hub opens over GT4's Options, whose project runs the menu update the pages tick in) | S | M5 |
| **M7** Native pause page (a copy of 003C3820 `RacePause::update`, which every mode's `updatePause` reaches) | M | M5, M0 |
| **M8** Start on the GT mode map | S | M5, M0 |
| **M8b** Settings at the mode select (the fourth way in: Start if free there, else our item beside GT4's two) | S | M5, M8 |
| **M9** Host wiring; the old menu deleted (the pad chord kept; host commands 2, 3 retired) | S-M | M3, M4, M5 |
| **M10** Style guide, the first asset set and the asset pipeline (the owner's sign-off) | M | M3 |
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
    as a PSMZ24 texture at its own address and width (bp 0 and 0x8C0, width 10: about 10 lookups a frame on
    race7, from one full-screen Z24 sprite copy a frame (a field's Z from the 448-line Z buffer at 0 / 0x8C0,
    alternating by frame); the counter counts texture lookups, not draws; over the captures race7 1,000,
    race_boot 16,754, menus_boot 34,175, before and after P2a). The direct path
    takes only colour targets, so these reads decode `gs_memory`, which never holds the drawn depths: a picture
    bug of the GS path, `original` included. Fix: a Z-format texture whose surface is a depth target reads that
    target (the depth image, by P2a's key), its depths converted to the GS's Z24 texel: the host keeps float depth
    (`VK_FORMAT_D32_SFLOAT_S8_UINT`, `gsn_scene.cpp` `kDepth`), so each texel becomes the 24-bit Z the GS would
    hold, laid out as a PSMZ24 texel (R, G, B the Z bytes, alpha from TEXA as for CT24), then goes through the
    usual texture path; not a plain bind. It changes pictures, so it is its own commit: the draws that sample it
    named (game function, from `GT4_GSN_DRAWS` on `race7`), what the picture shows wrong, and an `ORIG`
    re-baseline under section 2's rule.
    **Done** (lane B, c5ff20a): race7's fallbacks 1,000 to 0; not an `ORIG` re-baseline after all, since `ORIG` is
    identical against main with R6 on race7, race_grid43 and race_boot: nothing displayed in them samples that Z
    (CAPTURES' row says so). **Parked follow-up** (lane B): the focus probe's GS read of the Z buffer from the GPU
    (1b75e8f, Photo Travel's 8 x 4 Z32 reads; the photo itself unchanged against PCSX2) with a held picture for the
    synchronous read's cut (8eda650), both on origin as `parked/p2c-held-picture` with the trace commit a5aab72 at its
    tip; the earlier iterations are `parked/p2c-hold-v1` (ddc8f39) and `parked/p2c-keept` (59144cf). They failed the
    shooting screen's acceptance: half frames in some captures and pacing below main's, from field buffers and screen
    changes the copy didn't cover. Its start: that trace, lane A's per-buffer hold, a VBlank stamp on session
    captures, measured in a quiet window. Until then the focus probe reads the memory model, as before.
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
Done (2026-11-09, `spikes/R18-sound-driver-without-image.md`): no, the register image stays; no ticket follows.

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
| 3. Modern lighting (weeks 10-18) | B10-B15, F3, F4, F6-F9, A7 step 2, A8 (N2b/c and A11b only if N2 reopens) | `R5` gates in modern; owner review |
| 4. Resolution and effects | B16-B22, F5, E6-E9, P2b, P2c, P3b | per ticket |
| 5. Optional | B23, C6 on devices, research track | per ticket |

Lane M runs beside the phases: M0-M9 first, then M10-M12 and Q1-Q8 (MENU_DESIGN.md 7.3: 10-14 weeks for one agent).

Critical path: A0 -> A1 -> A10 -> A2/A3 -> B7 (also needs B1 -> B3 -> B4 and B5 -> B6) -> B8 -> B10 -> B15.
Calendar estimates are guesses from the sizes: hybrid in about 10 weeks with lanes A, B and C in parallel.

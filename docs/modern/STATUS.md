# GT4-Native modernisation status

Status as of 2026-11-10. All eight agent lanes stopped and retired, their clones and worktrees removed. Main is the
source of truth: PLAN.md and MENU_DESIGN.md in this folder hold each ticket's full text. This file is the short view.

## Finish line (the owner, 2026-11-10)

The owner's direction: finish with a working build that has most of the modernisation in, and don't overdo it. One
session now runs the work in the order below; the lanes' queues further down are history. PLAN's "Next" list was
about three times this, so it is cut to what a player sees and what removes PS2 mechanisms. Everything cut stays in
PLAN as written, for after the release.

**Where we stand.** The foundations are done and on main, but the presets don't show them yet: `render.scene =
hybrid` draws the same picture as the PS2 path and is in no preset, `lighting.night = enhanced` is in no preset,
and the day lighting (B10 onwards) isn't started. So the remaining work is mostly making finished machinery
visible, plus one large new piece (B10).

**In scope, in this order.** Each step lands on main behind its setting (rule 10) and enters the presets at its
own gate.

1. **Main green.** Done (e61ce10): the Linux x86_64 CI job ran out of memory on about half the pushes, so it
   builds with two jobs.
2. **Land what is finished.** Done: the hash memo (664f506; the game's CPU 1.69 to 1.63 ms a frame over a race
   session) and N3 step 1's verdict map, with a fix its gates asked for (PLAN N3). Left: `wip/m13-help-band`.
3. **Night lighting, as modern games do it** (the owner, 2026-11-10: real lights, and a place in the middle of
   nowhere darker than one in a city). So no shortcut over GT4's baked lamp light: the stills of 2026-11-10
   (`lap_ssr5f_boot`, three ways) show `enhanced` only adding to it, and N3's skip alone leaving the road dark
   wherever none of our lamps reaches (frame 9,725, the pools under the overpass). In scope, in order:
   - done so far (2026-11-10, on main behind switches until tuned): B10's lit-colour channel and the relight from
     the surface's own colour and normal (`GT4_NIGHT_SIDE`), the street lamps' profile (`GT4_LAMP_PROFILE`), and
     GT4's baked light kept per pixel where no lamp of ours replaces it (`GT4_N3_DEFER`): on S.S. Route 5 forward
     no part of the road is darker than GT4's any more, and lamps light it smoothly where they stand (PLAN N3).
     Since then these are what `lighting.night = enhanced` does on `render.scene = hybrid`, with no switch, and a
     lamp lights the road the same from any height (the tunnels were three times too bright). The cars' headlights
     light the road, the car ahead and the walls (a dipped-beam profile; no shadows yet), and painted lines keep
     their own colour under the lamps;
   - first, B10's channels from step 4 (the surface's own colour, normal and material, written by hybrid's side
     passes): the night relight then lights the real surface colour with real normals, in place of today's
     estimate from the picture, which is circular once the baked light is skipped. This replaces lane F's
     stand-in (one mean road colour for the whole course, measuring only: `parked/f-n3-standin`), as PLAN's N3
     foresaw ("if not, N3's skip waits for B10's material colour"). So the night lighting runs on hybrid;
   - N3 whole: the coverage gate (a baked pool goes only where one of our lamps replaces it), the never-dark
     runs, the hunts on the night courses left;
   - F7 (light profiles): the street lamps' spread, which the coverage gate's disc needs, and the headlights' beam;
   - the player's and the other cars' headlights lighting the road (F6's gather switch), then F9 (lamp lenses,
     brake and tail lights);
   - F10 (new, PLAN): the night's ambient from the place, measured per view from its sky and its lamps, never one
     constant; a course without lamps stays as dark as GT4 made it, lit by headlights;
   - F6's shadows from the player's headlights, after B11's shadow atlas (step 4);
   - then `lighting.night = enhanced` in presets, after the Odin's cost (C6b) and the owner's look.
4. **Day lighting, and hybrid in presets with it:** B10 (per-pixel lighting and material classes), B11 (the
   course's shadows on cars), B12 (GTAO). Hybrid alone draws the PS2 path's picture at a higher CPU cost, so it
   enters presets low-ultra only with B10 (B8 step 5, as PLAN has it): the quiet-window `PERF`, the front end's
   +31 % and the render thread's +0.35 ms cut or justified, R9's two every-frame readers checked, and the owner's
   review against PCSX2 stills.
5. **FSR 3.1 on game frames:** A3b step 1 (the camera-cut hook), B16c part 4.
6. **PS2 mechanisms out:** P1 (the dead GIF and VIF records), P4 (the preemptive delivery fallback), P5 (the
   32 MB spike, and the lift if something uses it), P3a (block links as offsets). Then P2b (native palettes) and
   P3b (draw state in renderer terms), last because they are large and change no picture.
7. **Driving feel:** what is left of D8 (physics force feedback, behind a switch today), D12 (steering and pedal
   response) and D14 (gamepad haptics from the physics), checked against main first, since parts are in; D13 from
   `archive/d-laneD-review` if its gates pass.
8. **Release 0.1.1** from main, after an Odin pass (C6b).

**Cut until after the release.** B9 (showroom and photo views stay on the PS2 path), B13 (SSR, probes), B14 (sky,
froxel fog), B15 and F4 (modern mode, tone map, HDR, modern bloom and flares), B18-B23, F3, F5, F6's shadows
beyond the player's headlights, F8 (volumetric light), N4, E4, E6-E9, E10, M10-M12 and Q1-Q8, D15, D16, B24's
later steps, B26 (unless its picture bug is seen), C12 step 4, split-screen relight and TAA, C7.

**What "no PS2 patterns" means here.** PS2_REMOVAL.md has the full table. After step 6, what is left is the game's
own logic and data, which a recompiled game can't shed: its 32-bit address space, the 60 Hz frame, the kernel and
SDK calls it makes, PS2 float rounding, and the PS2 renderer path that `original` keeps and that still draws what
hybrid doesn't replace (sky, GT4's post effects, HUD, menus).

**Physics.** No ticket changes GT4's simulation, and none is added: it is the game's own code at 60 Hz in PS2
arithmetic, and replays, ghosts, licence times and imported saves depend on it bit for bit. What modernises around
it is step 7: force feedback, haptics and steering response computed from the physics state.

**Review with one session** (rule 3 needs someone other than the author): each code step is read by a fresh agent
that gets the diff and the ticket's acceptance, not the author's conclusion, before it is pushed.

## Latest Odin build

- Last device build: main-874f32e, main's head at the wind-down (night bands fix, magenta fix, native menu by default,
  CPU cuts, rumble test). Not yet tried on the Odin.

## Done in the 2026-11-09 session (on main)

**Owner-visible fixes**
- Night bands across the road: our renderer clamped each 32-px light sprite to its own slice above 1:1 (73cfad8).
  PCSX2 confirmed the PS2 has no bands. NATIVE check added so it can't come back.
- Magenta road / red pools at night: car-lamp glows lit the road at full strength; off until F9 (c92b64c).
- Interface shake in menus and intro: every preset blends interlaced fields again (27f5729).
- City Courses menu stutter: movie frames decode twice as fast, no mip chain, no stall (25e9b8c).
- Photo Travel black photo (d7d38b8), and photo_check.py.

**Native settings menu (M-track)**
- M9 closed: the old Select+Start overlay is deleted; low-ultra use the native menu and pause; Settings opens
  mid-race in every mode (GT mode, time trial, single race, B-spec); original keeps GT4's own pause but
  Esc/Guide/Select+Start still reach Settings; old settings files migrate once; double-open guard; the pages'
  sounds; README screenshot. Also M3, M4, E5 (SDF fonts).

**Renderer**
- R6 (exact screen offset), P2c part 1 (Z texture), B16 (2) upscaler interface, B16c parts 1+3 (motion vectors
  for FSR, off until part 4), B24 step 1 (GS-exact sampling switch, for cross-GPU determinism), FSR 3.1
  selectable (render.upscaler = fsr3, no preset; game frames need B16c part 4), unused settings marked, no MSAA
  memory for game frames.

**Lighting**
- F2b night lamps accepted; A7 step 2 (every car's lamps as lights, not lit until F6/F9); kFrameLampsOn; A12 split
  screen sends scene records; A3b camera cuts designed and measured; N3 step 1: back-end skip, verdict map,
  coverage gate design, skip mask. Phase 3 lighting tickets added (F6 shadows, F7 beam profiles, F8 volumetric,
  F9 emissive lamps, tail/brake lights, flares, wet roads).

**Audio and input**
- D9 complete: stereo (with the click fix) and HRTF headphone sound. Bulk SPU2 voice advance (215 -> 70 us a frame).
- D7 wheels done except the live check (no wheel owned). D8 physics force feedback behind a switch.
- Rumble diagnostics (summary, driver, 3-pulse test).

**Performance (Odin CPU)**
- Runtime CPU cuts (2) clock read, (4) stack copy, (3) batched link records: game race CPU about 2.5 -> 1.95 ms a
  frame on the desktop.

**Tools and process**
- Odin device script (device_run.py) with night_strips, mixer_cpu, c6_orig; problem dump key (L3+R3 / Ctrl+F12);
  video recorder (--record); PCSX2 GS dump writer steps 1-2 (C12); slot.py memory fixes (two big jobs max, peak
  memory logged, automatic WSL memory drop); rule 10 (everything on main or origin, parked/debug/wip branches);
  rebase rule 3.

## PLAN ticket status

- **Done:** A0-A3, A5a, A5b, A7 (steps 1a, 1b, 2), A9, A10, A11a, A12; B1-B8 (B8 with its CPU condition), B6c;
  C1-C5, C9 (the POSIX link ships; device test pending), C10, C11; D1-D6, D7 (except the live check), D9;
  E1-E3 (E3 parked); F0-F2, F2b; N0, N1; P2 (P2c, follow-up parked); P6 (answered).
- **In progress:** A3b (camera cuts); A4, A6, A8, A11b (to confirm with lane A); B16c (part 4 waits for A3b);
  B17 FSR 3.1 (game frames wait on B16c); B24 (step 1 behind a switch); C6/C6b (Odin runs); C12 (steps 1-2 in,
  3 in review, 4 the PCSX2 player run); D8; runtime CPU cuts (hash memo next); M13; N3 step 1; NATIVE baselines.
- **Next:** the Finish line section above, in its order.
- **Parked:** B25 (fix moved to B26), E10 (clean field mode), N2, F2b's view-level albedo and albedo floor.

## Where the work lives

- Every unpushed local branch from the lanes is on origin as archive/<lane>-<branch>; the main checkout's old
  uncommitted edits are parked/main-checkout-edits; unfinished work is under wip/, parked/ and debug/ (rule 10).
- Test captures, PCSX2 references and Odin reference frames stay on the owner's machines (game content), under the
  main checkout's build/caps (CAPTURES.md lists them).
- To resume: start each new agent in its own worktree of the main checkout, from this file and PLAN.md.

## Where each lane stopped (queues in order)

History from the wind-down: the Finish line section decides what is picked up from these queues.
Nothing is left only on disk: unfinished work is on origin under wip/, parked/ or debug/ (rule 10).

- **Architect:** nothing open of its own; writes N3's rule, NATIVE's tool names, A3b's last sessions, C12's
  result and B26 into PLAN as the lanes finish them.
- **Lane A** (scene extraction): main 73cfad8 (night bands fix), 4469947. Next: A3b step 1, the camera-cut hook
  (Photo Mode's camera still unmeasured); B26 step 0(a), the page-table count; split-screen Light per half; HUD
  round meter. Branches: debug/a-a3b-step0, parked/a-display-edge-clamp (old uncommitted edits found in
  gt4-native-sprite).
- **Lane B** (renderer): main: N3 alignment guard, skip mask, lamps once per view; C12 step 3 (GS dump encoder,
  GT4_GSDUMP). Next: N3's realistic mask check (after lane F's map lands); N3 coverage gate; C12 race_boot dump
  in PCSX2 with lane C; B26 step 0(b); B24's render.gs_sampling setting; split-screen relight and TAA;
  B16c's near-road motion vectors. Many debug/ and parked/ branches.
- **Lane C** (platform, tools): main: night_strips, --strip-steps, C12 steps 1-2, slot.py WSL memory drop and its
  fix, native_strips.py with NATIVE's baselines. Next: Linux x86_64 CI out-of-memory (GT4_JOBS=2 proposal);
  C12 step 4 (PCSX2 player); native_strips follow-ups; the Odin device runs (night_strips, native-host session).
- **Lane D** (systems): main: bulk voice advance, rumble diagnostics, CPU cuts (2), (4), (3). Branch:
  wip/d5-hash-memo (checks to run). Next: the hash memo; the remaining ~55 VirtualQuery calls; the Odin rumble
  test; the host mixer default; D15/D16 on hardware.
- **Lane F** (night lighting): main c92b64c (car-lamp light off). Branches: wip/f-n3s1-verdict-map (reviewed, gates
  to rerun), wip/f-n3-standin (not reviewed), debug/f-n3-debug. Next: land the verdict map; the stand-in and the
  never-dark run on three laps; C.S. Route 5 Reverse true-colour hunt; the pop (owner's recording); Hong Kong.
- **Lane M** (settings menu): main: M9 closed (old menu deleted, native menu default, migration, double-open
  guard, screenshot). Branch: wip/m13-help-band. Next: M13; M10 (original menu art); M11 pages.
- **Lane U** (upstream, releases): nothing open. Next: the menu speed check from the owner's Odin log; 4-hourly
  upstream checks with prune.py.

## Open notes

- gsn_scene's buffer() may invalidate references when its ring vector grows (not looked at;
  nothing depends on it today). For the renderer lane.

## Waiting on the owner

- Install main-874f32e on the Odin; check a night race on S.S. Route 5 for the bands.
- Night lighting: answered 2026-11-10 (as modern games do it, the ambient by place: the Finish line's step 3).
- Rumble: run the 3-pulse test in a race and say which buzz; check the GT4 Native shortcut's Steam vibration toggle.
- Try the native Linux build (gt4-native.sh shortcut) for speed: it takes the host out of FEX.
- Optional: a recorded creep under the S.S. Route 5 overpass (the popping light); a Hong Kong dusk lap.
- The 0.1.1 release stays held until the modernisation is done.

## Parked (on origin)

- parked/p2c-held-picture, parked/p2c-hold-v1, parked/p2c-keept (P2c's GPU Z read follow-up)
- parked/e10-clean-still (clean field mode), parked/e3-display-clamp (E3, not needed)
- parked/f2b-view-level, parked/f2b-albedo-floor (night albedo, tried and not needed)
- parked/b25-depth-start (mid-run Z; cause moved to B26), parked/b7-fused-dump
- parked/a-display-edge-clamp (old uncommitted edits from gt4-native-sprite, never gated)

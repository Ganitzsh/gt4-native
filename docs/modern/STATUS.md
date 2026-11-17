# GT4-Native modernisation status

Status as of 2026-11-17, main 1e7be22. One session runs the work from the main checkout; the eight agent lanes were
retired on 2026-11-10. Main is the source of truth: PLAN.md and MENU_DESIGN.md in this folder hold each ticket's
full text, PS2_REMOVAL.md the PS2 audit. This file is the short view.

## Direction (the owner, 2026-11-17): the PS2 out of the picture path, first

The owner's direction: everything PS2 is gone; the whole pipeline uses proper shaders and Vulkan and does nothing
PS2-related; then the tracker says what is needed. The audit of that day (PS2_REMOVAL.md, at main 1e7be22) found:

- **It is not gone.** The pipeline is Vulkan and shaders throughout, but they compute the PS2: every pixel of every
  game frame, in every preset, is drawn from GS state by the GS's rules (`gsn.vert`, `gsn.frag`), 3D through GT4's
  VU1 programs as shaders. Measured at `high`: a day race is 738 draw batches a frame, a night race 641, the garage
  528, a 2D menu 80, and 100 % of them go that way.
- **Hybrid is not a modern renderer.** It draws about 92 % of a day race's 3D batches and 88 % of a night race's
  from the mesh cache, with GT4's vertex program and the GS's fragment stage, into the GS's targets. By day its
  picture is the PS2 path's. Menus, the garage and showroom, the HUD, the sky, particles and GT4's post effects
  are not touched by it.
- **`render.scene = modern` does not exist**, and the display pipeline's bloom, tone mappers, auto exposure, FSR 2
  and FSR 3.1 never run on game frames. What is modern on a game frame is screen-space work on the PS2's picture:
  the night relight, the occlusion, the temporal resolve, the height fog.
- **The game still sends PS2 terms**: GS register fields, surfaces by their address in GS memory, GS texels and
  palettes, 12.4 vertices, VU1 batch memory with a program entry, CRTC registers, all framed as a DMA chain.

So the work below comes first, and it is the largest block in the plan (PLAN P7-P20, spike R19: fifteen tickets,
three of them L, and P16 alone holds several older ones). It takes in tickets the finish line of 2026-11-10 had
cut or put last: A8, B9, B10's step 2, B13, B14, B15, B18, B22, F3, F4, E6, E7, E9, P2b, P2c, P3b.

| Phase | Tickets | Reaches |
|---|---|---|
| 0. Measure and clear | **P7** the PS2-path counters; **P8** dead code out; **R19** spike, one race view drawn natively | a number to drive to 0; the unknowns named |
| 1. Every draw described | **P9** every view role substituted; **P10** untagged draws described; **P11** 2D as 2D records | no draw the host can't name |
| 2. Native resources and state | **P12** textures as images; **P13** targets as named images; **P14** materials; **P15** meshes; P3a | no GS or VU term on the wire |
| 3. The native renderer | **P16** `render.scene = modern`, role by role; **P17** the 2D layer; **P18** reads; **P19** display | every pixel by the renderer's own shaders |
| 4. Delete | **P20** the GS path, `original`, the PS2-term records | the audit's inventory is empty |

**Next, in order:** P7, P8, R19. Phases 0 to 2 are meant to leave the picture as it is; the few changes are
named in their tickets (P8's two faults, P9's newly lit views, P10's particles, P11's menus in whole frames).

**Three decisions the plan assumes, the owner's to overrule** (PS2_REMOVAL section 4):
1. The `original` preset and the PS2 picture are deleted at the end (P20); until then they are the reference and
   the fallback.
2. Release 0.1.1 waits for this. The alternative: release from today's main, PS2 path inside, and this after.
3. The scope is the picture path. Sound, input, saves, the kernel and the game's memory stay at the game's own
   interfaces (PS2_REMOVAL section 6: the spikes R17 and R18 said no; not re-audited on 2026-11-17).

**What this does to the rest of the order:**
- No new lighting or effect is built on the GS target from here, so it isn't written twice: F10 (the night's
  ambient), F6's shadows and B16c part 4 (FSR 3.1 on game frames through the seam) wait for P16, which gives them
  native depth, colour and motion vectors. The night path that is in the presets keeps getting its fixes.
- P4's freeze still goes when an Odin play log has no late delivery; P3a stays as it was.
- Driving feel (the finish line's step 7) touches no renderer file and can run between renderer steps.
- Open from the owner's Odin playtests of 2026-11-11 (the session's notes; not rechecked since): the night path's
  GPU cost on the Odin (19 ms a frame at ultra before e864635's early-out, not measured after); a demo replay whose
  AI drives into a wall (the Paris rain demo; PS2 against IEEE arithmetic suspected) and a sky fault in it; the
  course menu slow on its first visit; one late delivery in gt4fn_00358608; the arm64 Steam shortcut.
- CI: three pushes from 08b2955 stopped in the two Linux host jobs at their package install (the runners couldn't
  resolve Ubuntu's mirror). That is over, and it had hidden a real failure since bafcb9e: `ui_page_goldens` on
  Linux x86_64, the hub's three golden images not regenerated when that commit changed the panel's line and
  dropped the Change prompt. Regenerated (40b4007). The test's cap is loose: the desktop passed with the changed
  text (0.24 % of pixels over, the cap 0.30 %); see the open notes.

## The finish line of 2026-11-10 (its record; the order above replaces it)

The owner's direction then: finish with a working build that has most of the modernisation in, and don't overdo it.
PLAN's "Next" list was cut to what a player sees and what removes PS2 mechanisms. Its steps, with where each stands:

1. **Main green.** Done (e61ce10): the Linux x86_64 CI job ran out of memory on about half the pushes, so it
   builds with two jobs.
2. **Land what is finished.** Done: the hash memo (664f506; the game's CPU 1.69 to 1.63 ms a frame over a race
   session), N3 step 1's verdict map, with a fix its gates asked for (PLAN N3), and M13 (the menu's help in its
   own three-line band; no row's help overflows it).
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
     light the road, the car ahead and the walls (a dipped-beam profile; no shadows yet), tail and brake lamps
     light what is behind a car, and painted lines keep their own colour under the lamps;
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
   - **in the presets since 2026-11-10:** medium, high and ultra (and the defaults) set `render.scene = hybrid`
     and `lighting.night = enhanced`; `original` keeps the PS2's picture and `low`, the handheld's, keeps the GS
     path until the Odin's cost is measured (desktop, 1080p high, a night lap: GPU 2.3 to 3.0 ms, render thread
     1.1 to 1.9 ms, front end 1.5 to 1.8 ms a frame). Older settings files under those presets move once;
   - **after the owner's Odin playtests of 2026-11-11** (0fc6a8f to 1e7be22): street lamps keep lighting where
     they stand while GT4 doesn't send them; a list cut by a game read gets a scene frame from its views'
     packages, so the road no longer flips between the two looks as flares come and go; lamps light in over 45
     frames; overlapping lamps and baked stretches are held level; red marker lamps light nothing; the light loop
     leaves a lamp that can't reach the pixel (the Odin: 0.7 ms with no lamp in view, 4.1 ms with 87, before it);
     `GT4_GPU_PROFILE=2` times the side passes, the baked layer, the occlusion and the clusters apart. Never-dark
     holds on `lap_ssr5f_boot` and `lap_csr5r_boot`;
   - **left:** the night path's GPU cost on the Odin (measured next from its log); F10 and F6's shadows, which
     since 2026-11-17 wait for P16.
4. **Day lighting: cut from this finish line** (2026-11-10; B10's step 2, B11 and B12 stay in PLAN for after it).
   Why: by day GT4's cars are already lit per object from the place they are in, and per-pixel lighting of the same
   model differs from GT4's by under 4 levels on 92-93 % of samples (B10's own measure), so the largest ticket left
   would change little a player sees; B11's shadow maps would need casters GT4 doesn't send (it submits what the
   camera sees; to be checked before B11 starts), and the horizon AO already runs. What was done instead: hybrid,
   in medium to ultra for the night, now draws no side channels by day (d459c3c's parent): on a day race at high its
   GPU time is the GS path's (1.04 against 1.05 ms) and its main thread 0.68 against 0.60 ms a frame (was 0.79).
   Since 2026-11-17 it is back on the path as part of P16: the native renderer lights per pixel (B10's step 2).
5. **FSR 3.1 on game frames:** A3b step 1 (the camera-cut hook) is done: `kViewCut` is set and TAA's history
   resets on it. B16c part 4 is not pursued on the GS path since 2026-11-17: P16 brings motion vectors of its own.
6. **PS2 mechanisms out:** now the first block of work, widened (the Direction section). Of the old list: P1 is
   done (53b7455: the dead GIF and VIF records). P4 (the preemptive delivery fallback): its diagnostic is in, 0
   late deliveries in the garage, Photo Travel and a race; a replay and a licence test since (0 in both); the
   freeze goes once a play session's log from the Odin has none. P5 is answered (R17: the race fills the PS2's
   heap by design, nothing would use more: no lift). Left of this step: P3a (block links as offsets). P2b (native
   palettes) and P3b (draw state in renderer terms) are now parts of P12 and P14.
7. **Driving feel:** what is left of D8 (physics force feedback, behind a switch today), D12 (steering and pedal
   response) and D14 (gamepad haptics from the physics), checked against main first, since parts are in; D13 from
   `archive/d-laneD-review` if its gates pass.
8. **Release 0.1.1** from main, after an Odin pass (C6b): after P20, by decision 2 above.

**Cut then, and where each is now.** In the PS2 work, as parts of P9-P17: B9, B13, B14, B15, B18, B22, F3, F4, E6,
E7, E9, and split screen's lighting (P9). Dropped by it: B24's later steps, B26, C12 step 4 and E10 are about the GS
path, which P20 deletes; they are done only if a fault of theirs is seen before then. Still cut until after the
release: B19-B21, B23, F5, F6's shadows beyond the player's headlights, F8, N4, E4, M10-M12 and Q1-Q8, D15, D16,
C7.

**What "no PS2" means.** PS2_REMOVAL.md section 4: the host knows no PS2 concept, and one importer in the game
process reads the disc's formats. What a recompiled game can't shed stays: its 32-bit address space, the 60 Hz
frame, the kernel and SDK calls it makes, PS2 float rounding (section 6 there).

**Physics.** No ticket changes GT4's simulation, and none is added: it is the game's own code at 60 Hz in PS2
arithmetic, and replays, ghosts, licence times and imported saves depend on it bit for bit. What modernises around
it is step 7: force feedback, haptics and steering response computed from the physics state.

**Review with one session** (rule 3 needs someone other than the author): each code step is read by a fresh agent
that gets the diff and the ticket's acceptance, not the author's conclusion, before it is pushed.

## Latest Odin build

- Last build: main-1e7be22 (2026-11-11 17:30, `build/release/main-1e7be22`: Windows x64 and Linux arm64), main's
  head, with the fixes after the owner's two playtests of that day (builds 34c194a and bafcb9e, on the Odin).
  Whether 1e7be22 itself reached the Odin isn't recorded.

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
  E1-E3 (E3 parked); F0-F2, F2b; N0, N1; P2a (its three steps), P2c's first item (its follow-up parked); P6
  (answered).
  Since 2026-11-10: M13; the hash memo; N3 step 1 and the night's modern path in the presets (N3's coverage per
  pixel, F7's profiles, F9's lights, B10's lit channel for the night); A3b step 1; P1; P5 (answered).
- **In progress:** P4 (the freeze waits for an Odin log); A3b (step 1 done; Photo Mode's camera unmeasured); N3
  (its owed list in PLAN); the runtime's CPU cuts; A4, A6, A8, A11b (as the lanes left them; A6 and A8 continue as
  P9 and P10); B10 (the night reads its channel; step 2 is P16's); C6/C6b (Odin runs); D8; NATIVE
  baselines. Stopped where they are, as GS-path work P20 deletes: B16c (parts 1 and 3 behind a switch), B17's game
  frames, B24 (step 1 behind a switch), C12 (steps 1-3 in).
- **Next:** the Direction section above: P7, P8, R19, then its phases.
- **Not started, now on the path:** P7-P20, R19; B9, B13, B14, B15, B18, B22, F3, F4, E6, E7, E9 as their parts.
- **Parked:** B25 (fix moved to B26), E10 (clean field mode), N2, F2b's view-level albedo and albedo floor.

## Where the work lives

- Every unpushed local branch from the lanes is on origin as archive/<lane>-<branch>; the main checkout's old
  uncommitted edits are parked/main-checkout-edits; unfinished work is under wip/, parked/ and debug/ (rule 10).
- Test captures, PCSX2 references and Odin reference frames stay on the owner's machines (game content), under the
  main checkout's build/caps (CAPTURES.md lists them).
- To resume: start each new agent in its own worktree of the main checkout, from this file and PLAN.md.

## Where each lane stopped (queues in order)

History from the wind-down: the Direction section decides what is picked up from these queues.
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
- `tests/ui_page_goldens.py` lets a changed line of text through on the GPU the goldens were made on (0.20 to
  0.24 % of pixels over 24 levels, under its 0.30 % cap; 40b4007's message). Its cap should come from what two
  GPUs differ by on the same picture (lavapipe against the desktop on fresh goldens), or the goldens be compared
  per panel.

## Waiting on the owner

- The three decisions of the Direction section (the PS2 picture deleted at the end; the release after it; the
  scope the picture path).
- Install main-1e7be22 on the Odin; with the high preset (or the playtest file over low), a night race on S.S.
  Route 5: the frame rate against low's, the lamps after the fixes of 2026-11-11, and the `host.log` (P4's freeze
  goes when it has no "waited over 50 ms" line).
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

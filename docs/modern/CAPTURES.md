# Captures and test inputs

The reference captures and the PCSX2 procedure come with ticket C10 (PLAN.md, lane C). This page starts with the
inputs every lane's session tests use on the main dev machine, so that all lanes run the same ones. None of them is
committed (PLAN.md section 1, rule 5).

## Standard test inputs on the main dev machine

| Input | Where | How to use it |
|---|---|---|
| Disc image | `C:/Users/julie/gt4-decomp/orig/SCUS-97328/Gran Turismo 4 (USA) (v1.01).iso` | Read-only, in place: never copy it (5.3 GB) and never write to that folder. |
| Game executable | `C:/Users/julie/gt4-decomp/orig/SCUS-97328/CORE.GT4.elf` | `--elf`. The game takes the `.iso` beside it when `--iso` is not given. SHA-1 as gt4-decomp's `orig/README.md` gives it (checked 2026-11-07). |
| Save | `C:/Users/julie/gt4-native-game/build/package/saves-pe-race2/odin-saves-pe-race2.tgz` (SHA-256 `2da5b79defae19b939aba2f2011fb07e3728262b6fc67fba2bdb37766a2d3e14`) | The owner's own save: GT mode with cars, Pan Euro race 2 next. Copy it into your own clone's `build/saves/` and unpack it there, again before every run that saves. Point the game at it with `--saves` (never at `%LOCALAPPDATA%` or another lane's folder). Never commit it or serve it outside this machine. |

A session on a release game, from the clone's root:

```
build/host-windows-Release/gt4_host.exe --session-dir build/sessions/NAME
    --game build/game_release/gt4_game.exe
    --game-args "--elf C:/Users/julie/gt4-decomp/orig/SCUS-97328/CORE.GT4.elf"
    --saves build/saves/saves
```

## Heavy jobs: machine-wide slots

The machine is shared by every lane, so heavy jobs go through slots (a slot is a folder made with `mkdir`, which
fails while someone holds it: wait and retry; `owner.txt` inside with your lane and the time; removed when the job
exits, on failure too). Do not start one under 8 GB available, free plus standby
(`(Get-CimInstance Win32_PerfFormattedData_PerfOS_Memory).AvailableMBytes`).

Take them with `tools/slot.py` (`py -I tools/slot.py --slot heavy --what TEXT --log FILE -- CMD ...`; `--help`), every
lane the same: it waits for the memory and the slots, runs the commands, and gives the slots back. Its `owner.txt`
names the holder's process (`pid=N start=T`): a waiter that finds the holder gone (killed, or its PID reused) logs the
slot and removes it, so a stopped run no longer keeps a slot. On Windows the holder's commands die with it (a job
object), so a slot freed that way has no work left running.

| Slot | For | Limits |
|---|---|---|
| `C:/Users/julie/locks/gt4-decomp-release` | game builds (`tools/release_game.py`, `tools/game_cc.py`): one at a time | `--jobs 4` |
| `C:/Users/julie/locks/heavy-1`, `heavy-2` | host builds, live game runs, replays: two at a time | `cmake --build --parallel 6` |
| `C:/Users/julie/locks/live-game` | the console's focus: a windowed game, PCSX2 (with a heavy slot) | one at a time |

A replay takes one heavy slot whatever its capture's size: `gt4_host --replay` reads the capture one frame at a time
and, on Windows, without the system's file cache (about 0.5 GB for the host; a replay of a 14.5 GB capture through
the cache took the machine's free memory to nothing while it ran). Hosts older than that reader still take both
heavy slots for a capture over 2 GB.

Live games from different `gt4_game.exe` files run at once, each in its own heavy slot: the guest's shared memory
is per executable file (`runtime/guest.cpp`), and a second launch of the same file is refused ("another gt4_game is
running from ..."). A game built before that allows one per machine: a second one is refused whatever its file.

Close PCSX2, replays and HTTP servers when the run is done.

A fresh clone also needs the static 32-bit FFmpeg the game links for its movies, in `build/ffmpeg-i686`:
`tools/build_ffmpeg.sh` builds it (FFmpeg 7.1.1), or copy a clone's (`GT4_FFMPEG=DIR` points the link elsewhere).

---

## Reference captures (REF)

### `ORIG` re-baselines

A commit that fixes a correctness bug in the GS path may move `ORIG` (PLAN section 2's rule). Each one is listed
here, with its commit, the captures re-recorded or re-hashed, and the PCSX2 comparison, so a comparison never
crosses one by mistake: compare a build against the reference of the latest re-baseline at or before it. The
reference hashes column names each capture's frame-hash list, so that choice can be checked mechanically.

| Commit | What it fixes | References renewed | Reference hashes | PCSX2 comparison |
|---|---|---|---|---|
| B7's b.10 commit ("render: GT4's lighting and transform in shared, precise shader code", 225d5e8, 2026-11-08) | shared transform and lighting shaders precise (no fused multiply-add): about 9 % of `race7`'s pixels, at near-equal depth tests | `race7`, `race_boot`, `race_grid43` at `original` (the REF captures a host replays; `race.cap`, `event.cap` are refused) | lane B's `build/caps/orig_b10_<capture>.sha256`: each frame's SHA-256 (`race_boot`'s every tenth), `--size 1280x960`, interpolation and HD pack off, from a clean host of the stack's head | PE race 2's countdown (`race_grid43.cap` against lane C's 59 PCSX2 software fields, 4:3; 26 matched under 12 levels mean difference): of 109,473 changed pixels, fused closer 54,532, precise 52,898, tied 2,043 (49.2 % precise); interior 31,867 vs 33,788; within 2 of PCSX2 36,390 vs 37,275. A near-draw, mixed by shot; against the CPU renderers precise is closer on 78 % (2,347,724 of 3,006,148). Not shown closer on PCSX2 (fused ahead by 1,634 of 109,473): kept as an exception to the rule's PCSX2 condition by the coordinator's decision (a), for device independence and B7's bit-identical Z (2026-11-08) |
| (planned) P2c's first item | depth targets read as textures from the drawn depths | to be listed | to be listed | the frames of the draws that sample it |

Kept in each clone's `build/caps/` (never committed), listed in `build/caps/REF.txt`. Every comparison on them
says how many of its frames show a picture: a capture that replays black proves nothing.

| Capture | VBlanks | What it shows | Notes |
|---|---|---|---|
| `race7.cap` | 100 | a race | **A fields-mode race** (recorded at `display.race_frames = fields`): its frames are interlaced and the field blend applies, so `display.field_mode = clean` changes it (98 of 100 frames at ultra). Do not use it as a progressive race. 99 of 100 frames lit at `original`. |
| `menus_boot.cap` | 18,931 | from boot: the title menu over its attract video, Arcade > Time Trial > Original Circuits and City Courses, browsed | Recorded live from boot (14.5 GB) with a copy of `C:/Users/julie/gt4-native-game/build/r5_saves`, `--preset original`, interpolation and texture pack off. Windows (host frames, within a few VBlanks): title menu 6,450-8,050; Original Circuits row 12,900-15,050 (taps at about 13,600, 13,730, 13,860, 13,985, 14,110); City Courses row 17,200-18,700. A replay of it takes one heavy slot with a host from 70e7fdb on (both with an older one: over 2 GB). |
| `race_boot.cap` | 4,914 | from boot, GT mode with the approved save: Home, the event screen, the race-start flyover, the grid countdown and the race | Recorded live from boot (2.1 GB) by the procedure below with `docs/modern/routes/race_gt_pe2.txt`, scene records on. Windows (replay frames, the capture's VBlanks): event screen (the entry list) 3,225-3,425; race-start flyover 3,475-3,850; grid and countdown 3,900-4,050; START at 4,075 and the race to the end (bumper view, chase view from about 4,750). Dark only at transitions (3,175-3,200 the race load, 3,450): 117 of 121 frames sampled with `--frames 4914 --capture-frames 1900-4914/25` are lit (over 5 % of their pixels not black). Replaces `race.cap` and `event.cap`, which replayed black (no display records) and hold the GIF / VIF records PLAN.md P1 retired: a host from P1 on refuses them with a message. One heavy slot. |

### Recording a REF capture

A REF capture is recorded from boot (`FROM` 0), so every model and texture its scenes use is in it; a capture that
starts mid-run begins with a state save instead. The captures live in each clone's `build/caps/`, the stills taken
at their scene points in `build/refs/`; neither is ever committed or served outside this machine.

1. A release game from current main (`tools/release_game.py`) and a host built from an empty folder (any recent
   one: the command below turns interpolation and the HD pack off).
2. A fresh copy of the approved save (above) unpacked into `build/saves/<name>/`.
3. The route's commands queued into `<session>/commands/` (one file per command, named so they sort in order;
   `tools/host_session.py step` writes them the same way). Either remove the session folder, then write the
   commands, then start the host; or start the host first and queue only once the NEW session has written its own
   `events.jsonl` (newer than your start). A queuer that sees an old folder's `events.jsonl` writes into a folder
   the host then empties, and the game waits in the menus with no input and no `quit`. Routes are in
   `docs/modern/routes/`: `race_gt_pe2.txt` (lane A's race route: GT mode Home to the Pan Euro race 2 and a lap's
   start) for `race_boot.cap`. A route only works from the save it was written for (this one: the approved save
   `odin-saves-pe-race2`, its GT mode, car and next race); from another save the same presses land elsewhere. The
   save itself stays out of the repository. The routes use SDL's button names (`a`, `b`, `back`, `start`,
   `dpup`...), which the session takes besides the PS2 names (`cross`, `circle`, `select`).
4. One live session, in one heavy slot:

```
GT4_ELF=<decomp>/orig/SCUS-97328/CORE.GT4.elf
GT4_ISO="<decomp>/orig/SCUS-97328/Gran Turismo 4 (USA) (v1.01).iso"
GT4_HOST_RECORD=build/caps/<name>.cap,0,<COUNT>
GT4_HOST_RECORD_SCENE=1
gt4_host --headless --size 1280x720 --session-dir build/sessions/<name> --no-audio --game <release>/gt4_game.exe
         --game-args "--elf <decomp>/orig/SCUS-97328/CORE.GT4.elf" --saves build/saves/<name>/saves
         --frames <COUNT> --capture-frames every:50 --capture-dir build/sessions/<name>/every50
         --preset original --set display.frame_interpolation=off --set texture.hd_pack=off
```

   `<COUNT>`: the route's `vwait` total plus a margin (`race_gt_pe2.txt`: 4,906, so 5000). The host exits 1 when
   the route's `quit` comes before `--frames` runs out; the capture is complete all the same.
5. Replay the capture once (one heavy slot) with `--frames <N> --capture-frames <first>-<N>/25`, N the capture's
   VBlanks (the replay's "vsyncs" line; past them the host shows the last frame again, which is not in the capture;
   the race: `--frames 4914 --capture-frames 1900-4914/25`, 121 frames from Home on) and read the scenes' windows from those frames: the live session's
   frame numbers lag the capture's VBlanks by an amount that changes from run to run (23 to 90 frames on the race
   route), so windows are given in replay frames, and a second recording's transitions fall a few frames apart.
   Count the lit frames (over 5 % of the pixels not black, as tools/frame_diff.py's lit check).
6. Add the row to the table above and the file name to `build/caps/REF.txt`.

### PCSX2 stills at the same scene points

PCSX2's software renderer is the reference for what the PS2 shows (menus, showroom, paused race, Photo Travel). It
takes the keyboard focus on the console desktop: run it only with the coordinator's go, under the live-game lock and a
heavy slot, and close it when done. The stills stay in `build/refs/` and the BIOS in `build/pcsx2/bios/` (both Sony's:
never committed or served).

1. A portable PCSX2 copied into your clone's `build/pcsx2/` (with `portable.txt`), its BIOS from the owner's
   install, the ISO read in place. `inis/PCSX2.ini`: `[EmuCore/GS] Renderer = 13` (software),
   `upscale_multiplier = 1`; `[Hotkeys] Screenshot = Keyboard/F8` (into `snaps/`), `ToggleSlowMotion =
   Keyboard/F9`; `[Framerate] SlomoScalar = 0.1`; pad 1 on the keyboard (arrows, X cross, C circle, T triangle,
   S square, Return start).
2. Start it with `pcsx2-qt.exe -fastboot -- <the ISO>`. A host save-folder copy is not a PS2 memory card: the game
   says it finds no data; answer Cancel, then Start game (Arcade mode needs none). Skip the intro with Start.
3. Keys reach PCSX2 only while its window has the focus (SetForegroundWindow, then SendInput); check every step with
   an F8 screenshot rather than timings, which change from one boot to the next.
4. The Original Circuits menu (Arcade > Time Trial > Original Circuits): at rest, F8 a few seconds after it opens;
   mid-slide, F9 (slow motion, 10 %), hold Right for about 1.5 s (in slow motion the game samples the pad a few times
   a second, and a short press is missed), F8 every half second, then F9 again. At full speed the slide is over before
   the first F8.

Taken 2026-11-08 (lane C): `build/refs/original_circuits_rest.png` (El Capitan, at rest) and
`build/refs/oc_slow2_1..8.png` (El Capitan to High-Speed Ring, mid-slide), 640x480.

## Comparisons and timings

A replay or session runs with `--fixed-step` (the default headless and with `--session-dir`): frame interpolation
then counts presents instead of the wall clock, and the HD texture pack decodes each replacement at its first use
instead of on its worker threads, so the same capture gives the same frames every run with both on
(`tests/replay_determinism.py`; set `GT4_DETERMINISM_CAPTURE` to a capture with 3D, such as race7.cap, to run it).
`tools/frame_diff.py` still turns both off by default.
A comparison with interpolation or the HD pack on needs BOTH hosts built from 330c41b or later: an older host (they
stay in build folders) still takes the wall clock and loads the pack in the background, so its frames vary run to run.

For timings (`--profile-json`, `tools/perf_report.py`), the synchronous decode stalls the GS front end on the frame
where a pack texture is first used: keep `texture.hd_pack=off` for a perf comparison, or skip the start of the run
(`--skip`) and compare runs that both have the pack on. A live game without `--fixed-step` loads the pack in the
background as before; the host's "Texture pack:" line says which mode runs.

Bases recorded across a change of what the game sends do not compare. Since 74ac4bb (D14) the presets low to ultra
ask the game for `kOptionSceneRecords` 2: every race frame carries a vehicles-only block (FrameBegin with
`kFrameVehiclesOnly`, six Vehicle records, FrameEnd), about 0.5 % more STREAM bytes. A STREAM or PERF base for low to
ultra recorded before 74ac4bb does not match a run after it; `original` is unchanged (option 0). A gate on a quality
preset needs a base from 74ac4bb on, or `--set input.haptics=original` on both sides. Since ba5c358 (link version 4)
a game and a host pair only on the same side of it; captures carry no link version and replay across it.

Since P1 (the GIF and VIF records retired) a capture holding any GT4_REC_GIF, VIF1, VIF0 or old GS_PRIV record is
refused whole at the first one (the replay stops, gt4_host exits 1 with the reason), even if the rest of it is native
records. Of the shared captures that is `race.cap` and `event.cap`: point no acceptance at them. The GS summary line
says `renderer packets N` where it said `GIF packets P1/P2/P3`: a STREAM base from before P1 compares its P1 count
with `renderer packets` (the game sent nothing on P2 or P3, nor any VIF1 stream).

`tools/perf_report.py --baseline` compares stages by name: a stage found on one side only (renamed, split, new or
gone) compares against nothing and cannot fail `--max-change`. It is listed as "only in the baseline / only in the
run" (and `one_sided_stages` in `--json`). Across B4 step 4 (cced3ed), the GS passes' names changed and part of
`colour grading` moved into `draws`: hold that comparison on their sum, or on `gpu_ms`.

## Scripting a live session

A scripted session runs the host with `--session-dir DIR`: it attaches a virtual pad and runs the commands it
finds in `DIR/commands/*.cmd`, in name order (`tools/host_session.py send` writes one; the syntax is in
`platform/sdl/session.cpp`: `button NAME 0|1`, `axis NAME VALUE`, `wait FRAMES`, `vwait VBLANKS`,
`capture NAME [COUNT]`, `quit`; `button2` and `axis2` drive a second virtual pad, the game's pad 2, plugged in at
their first use and removed by `unplug2`; `unplug` removes pad 1 until the next `button` or `axis`, each pad keeping its port (D13); `reset` releases both pads: without them port 2 is empty, as before). Two pitfalls:

1. **Queue a press and its release together.** Each command is run when the host sees it. Sending them one at a
   time through `host_session.py` costs about half a second each (a Python start), so `button cross 1`,
   `vwait 4`, `button cross 0` sent as three calls holds the button for 30 or more VBlanks, and menus that
   repeat or glide overshoot. Write all the commands of a step at once (one `.cmd` file each, names that sort in
   order: `<ns timestamp>-<index>.cmd`, written as `.tmp` and renamed), then wait for a `capture` at the end of
   the step to appear in `DIR/frames/`. Do it from one Python process (files written and waited for in-process),
   not from shell loops: on this machine every process forked through MSYS / Git Bash leaks kernel objects.
2. **GT4's title menu ignores Cross until a direction is pressed.** After Start shows "Arcade Mode / Gran Turismo
   Mode", press Down then Up (40 VBlanks apart) before Cross; Cross alone leaves the menu and the attract
   video comes back.

GT4's menus move a pointer that glides: in the course rows a one-host-frame tap (`button dpright 1`, `wait 1`,
`button dpright 0`) moves it two to five places, not one, and not the same number each time. Select by checking
a screenshot after each tap (below: the course's length line), not by counting presses.

---

## Route 5 at night (ticket N0)

Three captures on S.S. Route 5 at night show the three night artefacts of ARCHITECTURE.md section 8: **sections**
(the brightness step at section boundaries), **lamps** (the lamps that throw bands on the road), **warning**
(the blinking yellow warning lights tinting the frame). 300 to 600 frames each, in `build/caps/`, each with a
notes file beside it (`r5_<name>.notes.json`, the format of `tools/frame_metrics.py`) giving the frame of every
section crossing and blink. Re-recorded with scene records (`GT4_HOST_RECORD_SCENE=1`) after A3 and R7.

### Game and save

- A release game (`tools/release_game.py`) and a host built from an empty folder. Scouted on
  `gt4_game.exe` 80177ce8... (native 5279728, decomp cbae636).
- Save: a fresh copy, per run, of `C:/Users/julie/gt4-native-game/build/r5_saves` (GT mode at 0 %,
  100,000,000 Cr, a Nissan 240SX '96 in the garage), given with `--saves COPY`. The test save
  `odin-saves-pe-race2` cannot be used: it is in the middle of the Pan Euro Championship, and GT4 refuses to
  leave GT mode, so Arcade mode cannot be reached.

### Host

```
GT4_ELF=<decomp>/orig/SCUS-97328/CORE.GT4.elf
GT4_ISO="<decomp>/orig/SCUS-97328/Gran Turismo 4 (USA) (v1.01).iso"
GT4_HOST_RECORD=<clone>/build/caps/r5_<name>.cap,trigger:<session>/rec_go,<COUNT>
gt4_host --headless --session-dir <session> --no-audio --game build/game_release/gt4_game.exe --saves <copy>
         --preset original --set display.frame_interpolation=off --set texture.hd_pack=off
```

The recording starts at the VBlank at which `<session>/rec_go` appears (the host deletes it).

### Route (VBlank waits after each press; presses are held 6 VBlanks)

| Step | Input | Wait | Screen after it |
|---|---|---|---|
| 1 | (boot) | 1,400 | intro movie |
| 2 | Start | 600 | GT mode, Home (the save loads into GT mode) |
| 3 | Triangle | 1,000 | GT mode map |
| 4 | Triangle | 500 | "Do you really want to quit Gran Turismo Mode?", OK selected |
| 5 | Cross | 1,800 | title attract video |
| 6 | Start, Down, Up, Cross | 200, 40, 40, 150 | Arcade: Mode Selection |
| 7 | Right, Cross | 40, 600 | Time Trial: World / Original / City / Dirt & Snow |
| 8 | Down, Cross | 40, 600 | City Courses row, Citta di Aria selected |
| 9 | one-frame taps Right / Left | 60 each | until the bottom line reads "Course length:1.53miles Elevation change:48.2ft" (C.S.Route 5); Right from Citta di Aria or Tokyo R246, Left from Seattle or Seoul |
| 10 | Cross | 600 | Normal / Reverse, Normal selected |
| 11 | Cross | 600 | Car Selection, By Manufacturer |
| 12 | Cross, Cross, Cross | 300 each | Ford, Ford Ka '01, colour |
| 13 | Cross | 600 | Driving Options: Automatic, TCS & ASM, Sports: Medium, OK |
| 14 | Cross | 900 | Time Trial screen (Clubman Stage Route 5, Ford Ka '01) |
| 15 | Cross | 900 | on the grid, night |

Driving: hold Cross (the accelerator). With automatic and the aids the car rides along the walls but keeps going:
in 20 s it covers about a third of the lap.

The frames of each capture's section crossings, lamp bands and blinks follow in the notes files when the three
captures are recorded.

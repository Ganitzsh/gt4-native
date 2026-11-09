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
object), so a slot freed that way has no work left running. Waiters are served in the order they came: each leaves a
ticket in `C:/Users/julie/locks/queue` and takes nothing while an older live ticket waits for one of the same slots,
the memory floor included, so a job started later never passes one that is waiting (a ticket whose waiter is gone is
removed). Only runners from main since the queue take tickets: run `tools/slot.py` from an up-to-date checkout.
Two waits that are meant, not bugs. While a `heavy-both` waiter waits for the second heavy slot, the free one stays
idle: the younger `heavy` waiters behind it don't take it, so the two-slot job is never starved. And the oldest
waiter, held at its memory floor, holds back the younger ones for the same slots, even those with lower floors.
`LOW_MEMORY` in `tools/slot.py` is off: every job waits for its own `--min-gb`, and `heavy-both` runs. It was on from
2026-11-08 to the owner's reboot of 2026-11-09, while available memory stayed under 7 GB, and is turned on again only
by the coordinator's word. When on, it caps each job's floor whatever `--min-gb` says (4.5 GB for `heavy`, with or
without `live-game`; 5.5 GB for a job whose slots are all `game` or `live-game`). Heavy jobs then run one at a time,
never beside a game build (`heavy` takes only `heavy-1`; `game` and `heavy` also take a `low-memory` slot), and
`heavy-both` is refused. The heavy cap came from the jobs' own measured peaks (a clean host build 1.27 GB, a game
link 1.10, a replay 1.21) plus 1 GB, raised to the 4.5 GB minimum.

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
| B7's b.10 commit ("render: GT4's lighting and transform in shared, precise shader code", 225d5e8, 2026-11-08) | shared transform and lighting shaders precise (no fused multiply-add): about 9 % of `race7`'s pixels, at near-equal depth tests | `race7`, `race_boot`, `race_grid43` at `original` (the REF captures a host replays; `race.cap`, `event.cap` are refused) | lane B's `build/caps/orig_b10_<capture>.sha256` (a copy in `C:/Users/julie/gt4-native-game/build/caps/`): each frame's SHA-256 (`race_boot`'s every tenth), `--size 1280x960`, interpolation and HD pack off, from a clean host of the stack's head | PE race 2's countdown (`race_grid43.cap` against lane C's 59 PCSX2 software fields, 4:3; 26 matched under 12 levels mean difference): of 109,473 changed pixels, fused closer 54,532, precise 52,898, tied 2,043 (49.2 % precise); interior 31,867 vs 33,788; within 2 of PCSX2 36,390 vs 37,275. A near-draw, mixed by shot; against the CPU renderers precise is closer on 78 % (2,347,724 of 3,006,148). Not shown closer on PCSX2 (fused ahead by 1,634 of 109,473): kept as an exception to the rule's PCSX2 condition by the coordinator's decision (a), for device independence and B7's bit-identical Z (2026-11-08) |
| R6's re-baseline ("fix(gsn): draw XYOFFSET as the GS has it", 7f58836, 2026-11-09) | XYOFFSET drawn as the GS has it (`gs_frontend.cpp`'s `originMask()`): the field modes' constant quarter pixel and the supersampling passes' offsets, masked to half-pixel steps since d3a50ff (`GT4_GSN_JITTER=mask` keeps that for comparisons). It moves every field-mode frame: `race7` 99 of 100, `race_grid43` 699 of 700, `race_boot` 354 of 492 sampled (median 38.6 % of pixels identical, 83.2 % within 4 levels, largest 105); of lane C's recordings `race_grid43_r6` 301 of 440 sampled, `race_pe2_r` 299 of 300, `garage_r6` 272 of 392 | `race7`, `race_boot`, `race_grid43` at `original` | lane B's `build/caps/orig_r6_<capture>.sha256` (a copy in `C:/Users/julie/gt4-native-game/build/caps/`; `race_boot`'s every tenth), `--size 1280x960`, interpolation and HD pack off, from a clean host of 7f58836 | closer on every kind of scene it moves, judged where mask and keep differ: PE race 2's countdown with the gauges (`race_grid43_r6` against lane C's 4:3 software fields) keep 66.8 % against mask 33.2 %; the garage car (lane A's frame-exact pairing) 65.8 % against 30.8 %; a field-mode menu (lane C's pre-race entry list, `race_grid43_r6` 3,250-3,400, over the page's own pixels since its background is a moving camera) 86.4 % against 12.7 %. No field shake at 1x to 4x, 4:3 and 16:9 (one-frame against two-frame difference, keep at or below mask on in-car, racing, turntable and night ranges): d3a50ff's shake isn't reproduced on today's tree |
| P2c's first item ("feat(gsn): a Z buffer read as a Z texture is built from the depth image", 2026-11-09): not a re-baseline | a Z-format texture over a Z buffer of its layout built on the GPU from the depth image, not decoded from the memory model (race7's pre-race frames: 1,000 such reads, now 0) | none: `ORIG` identical on every REF capture at `original`, `race7` 100 of 100, `race_grid43` 700 of 700, `race_boot` 492 of 492 sampled (every tenth), against main with R6 (7f58836) | the `orig_r6_*` lists stand | none needed (no frame moves); Photo Travel's photo as main (photo_check 52.5 / 40.7). The focus probe's Z read from the GPU is parked (PLAN P2c) |

Kept in each clone's `build/caps/` (never committed), listed in `build/caps/REF.txt`. Every comparison on them
says how many of its frames show a picture: a capture that replays black proves nothing.

A copy of each is also kept in the owner's main checkout, `C:/Users/julie/gt4-native-game/build/caps/`
(gitignored; it outlives the lane clones, and tools/prune.py never touches a `caps` folder): the captures
below, `race.cap`, `event.cap`, lane A's `pt_r6v.cap` and `garage_r6v.cap` (B9's), the night references of
the section below, and lane B's `ORIG` reference hashes (`orig_b10_*` and `orig_r6_*.sha256`). Each copy was
checked by SHA-256 against its source (2026-11-09, lane B's `build/copy_caps*.log`), the originals left where
they were. Read them in place. Also kept there: `race_pe2_prog.cap` and `orig_desktop_race_pe2_prog.sha256`, lane C's
frame-hash list of it (below; both copied by the coordinator, 2026-11-09), which `tools/device_run.py`'s c6_orig
compares the Odin's frames with.

| Capture | VBlanks | What it shows | Notes | Kept in |
|---|---|---|---|---|
| `race7.cap` | 100 | a race | **A fields-mode race** (recorded at `display.race_frames = fields`): its frames are interlaced and the field blend applies, so `display.field_mode = clean` changes it (98 of 100 frames at ultra). Do not use it as a progressive race. 99 of 100 frames lit at `original`. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane B's `build/caps/` |
| `menus_boot.cap` | 18,931 | from boot: the title menu over its attract video, Arcade > Time Trial > Original Circuits and City Courses, browsed | Recorded live from boot (14.5 GB) with a copy of `C:/Users/julie/gt4-native-game/build/r5_saves`, `--preset original`, interpolation and texture pack off. Windows (host frames, within a few VBlanks): title menu 6,450-8,050; Original Circuits row 12,900-15,050 (taps at about 13,600, 13,730, 13,860, 13,985, 14,110); City Courses row 17,200-18,700. A replay of it takes one heavy slot with a host from 70e7fdb on (both with an older one: over 2 GB). | `C:/Users/julie/gt4-native-game/build/caps/`, and lane C's `build/caps/` |
| `race_boot.cap` | 4,914 | from boot, GT mode with the approved save: Home, the event screen, the race-start flyover, the grid countdown and the race | Recorded live from boot (2.1 GB) by the procedure below with `docs/modern/routes/race_gt_pe2.txt`, scene records on. Windows (replay frames, the capture's VBlanks): event screen (the entry list) 3,225-3,425; race-start flyover 3,475-3,850; grid and countdown 3,900-4,050; START at 4,075 and the race to the end (bumper view, chase view from about 4,750). Dark only at transitions (3,175-3,200 the race load, 3,450): 117 of 121 frames sampled with `--frames 4914 --capture-frames 1900-4914/25` are lit (over 5 % of their pixels not black). Replaces `race.cap` and `event.cap`, which replayed black (no display records) and hold the GIF / VIF records PLAN.md P1 retired: a host from P1 on refuses them with a message. One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane B's `build/caps/` |
| `race_grid43.cap` | 700 | PE race 2's grid, countdown and START at in-game Screen 4:3 | Recorded mid-run (it starts with a state save), VBlanks 3,600-4,299 of `race_gt_pe2.txt` with the approved save, `--size 1280x960 --set aspect.ratio=4:3`, `original`, scene records on, before R6's origin record (f90214e). B7's b.10 PCSX2 comparison: lane B's matches for the PCSX2 stills k 1-200 sit at its VBlanks 3,919-4,118. Kept as is for b.10's reference. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane B's `build/caps/` |
| `race_grid43_r6.cap` | 4,400 | as `race_grid43`, from boot, with R6's origin record | Recorded live from boot (2.0 GB) by the procedure below: `race_gt_pe2.txt`, the approved save, `--size 1280x960 --set aspect.ratio=4:3`, `original`, scene records on, game and host from f90214e (link version 5: 201,521 origin parts). The grid and countdown fall near replay frames 3,900-4,150, the '2' rear shot at about 3,950; R6's PCSX2 pairing places k 1 itself. Dark only at transitions (2,075; 3,175 and 3,200 the race load; 3,450): 97 of 101 frames sampled with `--frames 4400 --capture-frames 1900-4400/25` are lit. One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane C's `build/caps/` |
| `race_pe2_r.cap` | 300 | PE race 2 racing, in fields mode, with scene records (B8's hybrid `PARITY`, replacing `race7`, whose route is unknown) | Recorded mid-run (state save first), VBlanks 4,300-4,599 of `race_gt_pe2.txt` with the approved save, `--set display.race_frames=fields`, `original`, `--size 1280x720`, scene records on from game start, game and host from f90214e (231,282 origin parts). The in-car view, the player's car idle (the route doesn't drive), the field cars ahead. 11 of 12 frames sampled with `--frames 300 --capture-frames 1-300/25` are lit; frame 1, the first after the state save, is dark. Lane B judges hybrid from its 20th frame (the caches' warm-up). One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane C's `build/caps/` |
| `garage_r6.cap` | 3,920 | GT mode's Garage, the Nissan 240SX '96 on the turntable (R6's garage pairing) | Recorded live from boot (1.6 GB): a copy of `C:/Users/julie/gt4-native-game/build/r5_saves` (GT mode at 0 %, the 240SX), lane A's presses, each held 6 VBlanks: vwait 1,400, Start, vwait 600 (Home, the cursor on Garage), Cross, vwait 240, Down, vwait 60, Down, vwait 60, vwait 30, Cross (the car view), vwait 1,500, quit. `original`, `--size 1280x720`, scene records on, f90214e (219,692 origin parts). Replay frames (lane A): the list fades out 2,418-2,430, black 2,431-2,433, the car's first lit frame 2,434 (the pairing's anchor), the turntable turning from about 2,470, once in about 525 frames. 80 of the 81 frames sampled every 25 from 1,900 to 3,900 (`--capture-frames 1900-3900/25`, inside its 3,920) are lit (2,025, a transition, is not). One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane C's `build/caps/` |
| `race_mixer_host.cap` | 8,121 | from boot, as `race_boot`, then a scripted drive in the race: the host's SPU2 mixer's input (`audio.mixer = host`; D5, D9) | Recorded live from boot (3.1 GB) by the procedure below with `docs/modern/routes/race_gt_pe2_drive.txt` (`race_gt_pe2.txt` to the race's start, then 4,080 VBlanks of driving into the walls, then quit at 8,121), the approved save, `--preset original`, `--set audio.mixer=host`, scene records on, game and host of f1d5688. With the host mixing, it carries the game's SPU2 records (register images, SPU RAM writes, each frame's run) and D9's `GT4_SPU2_TAGS`, so a replay feeds the host's mixer: 8,120 frames mixed, 0 stream 1 values unmatched, 24.3 tagged voices a frame (a replay's host mixer summary). A headless replay mixes only with `GT4_HOST_AUDIO_WAV` set (no audio output there), and stops at the capture's end only with `--frames 8121`. 246 of 249 frames sampled with `--capture-frames 1900-8121/25` are lit. SHA-256 0aef57ae861bc05e65d5a775fb69cc86114c5049f63c8559a7fd50514b320d8f. `tools/device_run.py`'s mixer row (C6b). One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/`, and lane D's `build/caps/` |
| `race_pe2_prog.cap` | 4,915 | from boot, as `race_boot`, raced in progressive mode: the progressive race REF (B16c, FSR 2 and FSR 3.1 are judged on it) | Recorded live from boot (2.3 GB, 2,313,051,136 bytes) by the procedure below with `race_gt_pe2.txt`, a fresh copy of the approved save `odin-saves-pe-race2` (deleted after), `GT4_HOST_RECORD=...,0,5000` with scene records on, `--size 1280x720`, `--preset ultra` with `--set display.race_frames=progressive` named (the game logged option 3 = 1), interpolation and HD pack off. Host: a clean Release build of a0d3a7c; game: lane B's clean release build (native 56836ef, decomp 6dbbf48f4), by the coordinator's OK while game builds were blocked (`platform/ui` reaches the game, but a race with `ui.entry = hotkey` sends none of those pages' records: the race stream is the same). Progressive from the event screen on; replay frames up to 3,259 are the game's fields-mode menus. Replay windows: the event screen (the entry list over the 3D course) 3,260-3,441; a transition 3,442-3,458; the race-start flyover 3,459 to about 3,850; grid and countdown about 3,900-4,050; START about 4,075 and racing to the end (bumper view, chase view from about 4,750). Every displayed list from 3,260 on has GT4's post copy chained to a Light except the transition's 17: 1,639 (182 on the event screen, 1,457 from 3,459). 115 of 121 frames sampled with `--frames 4915 --capture-frames 1900-4915/25` (a clean host of a0d3a7c, `original`) are lit, dark only at transitions (2,075; 3,175-3,250, the race load; 3,450). Its `ORIG` list (no R6 list exists for it): lane C's `orig_desktop_race_pe2_prog.sha256`, every frame at R6's flags (`--size 1280x960`, `original`, interpolation, HD pack and overlay off) from main 0bdaabab's host (`tools/device_run.py --make-hashes`). `race_boot`, `race_grid43` and `race_pe2_r` stay the fields-mode races. One heavy slot. | `C:/Users/julie/gt4-native-game/build/caps/` (the coordinator's copy, 2026-11-09, SHA-256 4bf59b86211ff6a19c4d48f6dbdc78ea427a13442fa2171bbb31e55de1b02716 as its source), with its hash list, and lane B's `build/caps/` |

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
   start) for `race_boot.cap`; `race_gt_pe2_drive.txt` (the same to the race's start, then lane D's scripted drive:
   throttle, steering pulses into the walls, braking, then quit) for `race_mixer_host.cap`. A route only works from
   the save it was written for (these two: the approved save `odin-saves-pe-race2`, its GT mode, car and next race);
   from another save the same presses land elsewhere. The save itself stays out of the repository. The routes use
   SDL's button names (`a`, `b`, `back`, `start`, `dpup`...), which the session takes besides the PS2 names
   (`cross`, `circle`, `select`).
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

N0 planned three captures for the three night artefacts of ARCHITECTURE.md section 8: **sections** (the
brightness step at section boundaries), **lamps** (the lamps that throw bands on the road), **warning** (the
blinking yellow warning lights tinting the frame). The names `r5_lamps` and `r5_warning` were never recorded: R11
found no bands in any capture, and N4 worked on lane A's captures. The night references (2026-11-07 to 09) are
these, each recorded in B-spec (the game drives) from a fresh copy of the approved save `saves-ss-r5`:

- `r5_ss_ab.cap` with `r5_ss_ab.notes.json`: S.S. Route 5 Reverse, Professional Events > Race of NA Sports race 4;
  3,600 frames; game 80177ce8, host e8fd29e. The sections capture (crossings at frames 507, 1049, 2514, 2859); R11
  and F2b step 0 use it.
- `lap_ssr5r.cap`: the same race, a full lap plus the start stretch; 10,200 frames; game and host a75a6c2, scene
  records. The lamps capture in practice: N3's proof and its step 1 acceptance.
- `lap_csr5.cap`: Clubman Stage Route 5, Professional Events > Clubman Cup race 4; 9,000 frames; a75a6c2, scene
  records. N3's second layout and F2b's second course.
- `lap_ssr5r_boot.cap`: `lap_ssr5r`'s race recorded from game start (C10: `GT4_HOST_RECORD=PATH,0,20200`), 20,200
  frames (8.0 GB): boot, GT mode, the race from about frame 9,050, a full lap and part of the second. Release game
  and host from empty folders at 95a639b (kFrameLampsOn's flag in its FrameBegins), scene records, `original`,
  1280 x 720. 183 of 184 frames sampled every 100 from 1,900 are lit (8,000, a load, is not). It replays the live
  game exactly: 13 live shots spread over the lap equal the replay's frames (mean difference 0.00; a live shot at
  session frame F is replay frame F + 172 to 176, the frames the session skipped while booting). The night
  reference to judge colours on: N3's v4 check, lane C's R7 sub-capture test, the Odin's night replay. Heavy-both.
- `a5b_r5.cap` (lane A's): S.S. Route 5 Reverse, the same race; 1,200 VBlanks after 300 of driving, scene records,
  the A5b game (flare markers around the flare pass). The warning capture: the pit entry's warning lamps blink
  through it; N4 uses it. `glow_r5.cap` (the A7 step 1a game) is the same stretch.

All six, with `r5_ss_ab.notes.json`, are also kept in `C:/Users/julie/gt4-native-game/build/caps/`
(the originals in lanes F and A's `build/caps/`).

The routes above start from GT mode's map: Professional Events, the event, regulations OK, the race, Entry, Don't
Clear, then B-spec. C.S. Route 5 Reverse is Beginner Events > Sunday Cup race 4 (or Reverse at step 10 below); S.S.
Route 5 forward is in the Arcade City row. A replay's first 300 to 500 frames show textures not yet uploaded again
(a mid-session recording); counts leave them out.

Colours in these replays, except `lap_ssr5r_boot`'s, are not always the game's (RISKS R7). The others were recorded
mid-run: lane F's sessions boot from game start, but the recording starts at a trigger once the race runs
(`lap_ssr5r` at VBlank 9,926, `lap_csr5` at 9,478), and lane A's start after 300 VBlanks of driving, so each replay
starts from a state snapshot. Some textures the live game drew are not right in it: in `lap_ssr5r` the lane
markings and the edge line are green (frame 3,500) and the left kerb is cyan and garbled (frame 1,000), at every
setting, even `--preset original` with the HD pack off; live shots at the same race times show white markings and
a red and white kerb. Lane C is tracing which textures (RISKS R7). N3's hunts compared both runs on the same
replay, so they agree with themselves, but v4 also reads the texture (its centre/rim ratio): on `lap_ssr5r`, some
textures it judged have means that look wrong (256 x 256 at (0, 0, 0) beside the right (127, 127, 127)), while
`lap_csr5` has almost none. Whether v4 decides the same live is checked on a live run or on `lap_ssr5r_boot`, not
on these replays. Don't judge colours from these replays, and record new references from game start (C10). The
captures recorded from boot (`race_boot`, `race_grid43_r6`, `garage_r6`, and at night `lap_ssr5r_boot`) are the
ones to judge colours on.

The Arcade route below is the one the first, throttle-only sections captures used (`r5_sections*.cap`, no scene
records; not references).

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

Section crossings are in `r5_ss_ab.notes.json`; no capture shows lamp bands. `a5b_r5.cap` has no notes file: its
flare markers bracket each frame the warning lamps' flares draw.

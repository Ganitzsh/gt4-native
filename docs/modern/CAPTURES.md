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

| Slot | For | Limits |
|---|---|---|
| `C:/Users/julie/locks/gt4-decomp-release` | game builds (`tools/release_game.py`, `tools/game_cc.py`): one at a time | `--jobs 4` |
| `C:/Users/julie/locks/heavy-1`, `heavy-2` | host builds, live game runs, replays: two at a time | `cmake --build --parallel 6` |
| `C:/Users/julie/locks/live-game` | a live game run, taken with its heavy slot: one at a time | until one running `gt4_game` per executable (`runtime/guest.cpp`) is on main |

A replay takes one heavy slot whatever its capture's size: `gt4_host --replay` reads the capture one frame at a time
and, on Windows, without the system's file cache (about 0.5 GB for the host; a replay of a 14.5 GB capture through
the cache took the machine's free memory to nothing while it ran). Hosts older than that reader still take both
heavy slots for a capture over 2 GB.

Close PCSX2, replays and HTTP servers when the run is done.

A fresh clone also needs the static 32-bit FFmpeg the game links for its movies, in `build/ffmpeg-i686`:
`tools/build_ffmpeg.sh` builds it (FFmpeg 7.1.1), or copy a clone's (`GT4_FFMPEG=DIR` points the link elsewhere).

---

## Scripting a live session

A scripted session runs the host with `--session-dir DIR`: it attaches a virtual pad and runs the commands it
finds in `DIR/commands/*.cmd`, in name order (`tools/host_session.py send` writes one; the syntax is in
`platform/sdl/session.cpp`: `button NAME 0|1`, `axis NAME VALUE`, `wait FRAMES`, `vwait VBLANKS`,
`capture NAME [COUNT]`, `quit`). Two pitfalls:

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

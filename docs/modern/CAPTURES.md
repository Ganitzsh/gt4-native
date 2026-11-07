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
exits, on failure too). Do not start one under 6 GB free (`Get-CimInstance Win32_OperatingSystem`,
`FreePhysicalMemory`).

| Slot | For | Limits |
|---|---|---|
| `C:/Users/julie/locks/gt4-decomp-release` | game builds (`tools/release_game.py`, `tools/game_cc.py`): one at a time | `--jobs 4` |
| `C:/Users/julie/locks/heavy-1`, `heavy-2` | host builds, live game runs, replays: two at a time | `cmake --build --parallel 6` |

Close PCSX2, replays and HTTP servers when the run is done.

A fresh clone also needs the static 32-bit FFmpeg the game links for its movies, in `build/ffmpeg-i686`:
`tools/build_ffmpeg.sh` builds it (FFmpeg 7.1.1), or copy a clone's (`GT4_FFMPEG=DIR` points the link elsewhere).

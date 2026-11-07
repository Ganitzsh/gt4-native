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

## Release game builds

`tools/release_game.py` stages into the shared decomp (`gt4-decomp/build/native_*_release`) and empties those
folders, so two at once break each other. Until the staging moves into each clone, hold the lock while it runs:
`mkdir C:/Users/julie/locks/gt4-decomp-release` (it fails while another lane holds it: wait and retry), your lane
and the time in `owner.txt` inside, removed when `release_game.py` exits, on failure too.

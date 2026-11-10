# The finish line's gate and measurement scripts (2026-11-10)

Debug branch (PLAN rule 10): never merged to main. Written by the session that runs the finish line
(docs/modern/STATUS.md), from the lanes' scripts, with this machine's paths: the worktree
`C:\Users\julie\gt4-wt-land`, the captures in the main checkout's `build/caps`. Run each under `tools/slot.py`
(a heavy slot); every one stops its own hosts.

- `ref_then_new.cmd BRANCH`: main's host from empty into `build/ref/gt4_host_main.exe`, then BRANCH's from empty,
  both with ctest (copy it to the worktree's `build/` first; `inc.cmd` is the incremental host build for looks).
- `n3s1_gates.py orig`: `ORIG` against the `orig_r6_*` hash lists; also the verdict map's on/off run and `NATIVE`.
- `same.py CASE...`: the new host's frames against the reference host's, byte for byte.
- `val_diff.py CASE...`: the validation layers' messages, new host against reference (main has 5 warnings of its own).
- `shots.py NAME CAP FRAMES SHOTS [--set k=v] [--env K=V]`: stills as PNGs for a look (game content: local only).
- `never_dark.py CAP FRAMES SHOTS [--env K=V] [--tag T]`: N3's measure, our light over GT4's under the lit-road layer.
- `memo_sessions.py`: scripted live sessions (race, garage, photo) with a save copy each.

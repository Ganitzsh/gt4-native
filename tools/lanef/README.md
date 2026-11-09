# Lane F's N3 / F2b measurement tools (branch lanef/n3-debug, never main)

The debug host and scripts behind N3 step 0, F2b's close, the night bands' step 0 and the never-dark sweep
(2026-11-07 to 09), kept on origin under the owner's rule that nothing stays local only. Not for main: printf
logging and environment knobs, measurement only.

- The commit's source changes are `patches/n3_knobs_all.patch` applied to bbd7a47 (the base the measurement host
  `gt4_host_n3knobs.exe` was built from): v4 judged per item with `GT4_N3_V4SKIP` (skips by texture identity),
  `GT4_N3_V4NARROW=C` (keep neutral textures: never-dark option (d)), `GT4_N3_GLOW_RANGE`, `GT4_N3_GLOW_INTENSITY`,
  `GT4_N3_GLOW_SPOT=cosOuter` (course lamps' range, intensity, downward spot: option (a)), F2b drop and gather logs.
- `patches/`: the earlier stages (n3_v4_all, n3_locator_all, n3_f2b_all) and other local patches.
- The scripts drive live sessions (`r5_session.py`, `run_save_job.py`: the approved save, a fresh copy per run,
  deleted after) and replays through `tools/slot.py`; paths point at C:/Users/julie/gt4-lane-F. Screen crops the
  scripts compare against (game content) are not here.
- Main entry points: `n3_dark.py` (never-dark), `n3_sweep.py` (the (a)/(d) sweep), `n3_v4hunt2.py` and
  `n3_v4log.py` (v4 hunts), `lapboot.py` and `lapboot_check.py` (lap_ssr5r_boot), `structure_forms.py`
  (PLAN section 2's structure measure, four forms), `bands_repro.py` (the bands' step 0).
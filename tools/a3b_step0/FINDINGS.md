# A3b step 0: GT4's camera cuts (lane A, 2026-11-09/10)

Debug branch only, never for main. The debug commit below this one logs the race camera object's small-integer
fields and each view's geometry (runtime `view_begin`, needs `GT4_HOST_RECORD_SCENE=1`). The scripts here drove the
sessions and read the logs. Logs, frames and saves stayed on the owner's machine.

## The signals

The race camera object (`cam` = method 0xD0 in 0033EAB8) is one object all session; `cam+0x9BC` stays 0, so the
PLAN's planned signal was wrong. The real ones, each matched against the camera's geometry (session 1, night B-spec
C.S. Route 5 Reverse, 8,077 main views: all 43 jumps carry one, and every one lands on a jump):

- `+0x1A0` (1 <-> 2; `+0x138` mirrors it) and `+0x1A8` (0 <-> 1): B-spec's three-view cycle. `+0x1A8` rising is the
  rear view (a 180-degree turn).
- `+0xAC8` and `+0xAEC`: pulses on trackside TV cuts. In replays a pulse can stay 1 for 0.67 s: flag the rising edge.
- `+0x140`: 0 -> 1 -> 2, the start sequence's camera steps.
- A-spec's view changes (Select) pulse `+0xAEC`; one moved only 0.74 m with a 20 % FOV change.

## The safety net (geometry, for cuts without a signal)

Measured on sessions 1, 2 (A-spec, with a crash) and 4 (replay), and on those logs thinned to every 2nd-8th view
(20-30 fps and hitches, `a3b_hitch.py`):

| Test | Threshold | Worst without a signal |
|---|---|---|
| residual of a constant-velocity prediction | > 1 m per VBlank | 0.95 m |
| the step's turn | > 90 degrees | 67.5 degrees (every 8th view); a crash swings 13-47 per VBlank |
| FOV change, per VBlank: (f2/f1)^(1/gap) | > 3 % | 2.1 % |

Per step, the FOV test false-flags GT4's smooth zooms at 20-30 fps (41-254 flags); per VBlank it gives none at any
thinning. Reset the history on a flag and on any gap over 8 VBlanks with no main view (s4: 495 VBlanks from the
Result menu to the replay, its first view 246 m away: a new sequence, not a cut). Pause and resume: no jump.

## Other cameras

- Photo Travel (s5, s6): the preview is a role-6 view. Cross + left stick pans and tilts; the camera only turns, at
  most 1.7 degrees per VBlank, at 30 fps, with no move, FOV change or field signal. No special case needed.
- Photo Mode: not reached. Start in a live replay ends it (s7). Lane C's lead: the photo option sits with saved
  replays in the Replay Theater. Lane C agrees Photo Travel's numbers cover a hand-moved camera if it can't be
  reached. s8 (the live replay's buttons one at a time) is written but was not run (the wind-down).

## Scripts

- `a3b0.py`: the sessions (s1-s8) through r5's session driver under a heavy slot; fresh save copies, deleted after.
  Routes: the Result menu is reached by Crosses until the bright prize dialog has passed (`to_replay`).
- `a3b_cams.py`, `a3b_geo.py LOG [ROLE]`: the fields against the geometry; `a3b_hitch.py LOG...`: the thinned runs.

Next (A3b step 1): the hook in the 0033EAB8 and 0033FD88 copies on the fields above (rising edges), and the runtime
net with these thresholds.

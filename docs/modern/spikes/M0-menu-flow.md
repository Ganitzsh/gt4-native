# M0: GT4's menu flow, measured (lane M)

Status: answered for M6, M7 and M8, with three open points (end of this note). 2026-11-08.

**Method.** Local instrumentation, never pushed: copies of six matched functions with a `gt4_logf` line each
(`func_0010B290`, sequence 1's call of `startProject` 00109D00; `func_0010FE40`, the quick menus' project loop;
`func_001279E8`, the quick menu's selected command; `MSound::play` 0023E2F8; `RaceBasic::pauseStart` 0038B428;
`RaceBasic::updatePause` 0038B2B0). Release game from 9934fc9 plus that commit (decomp 6dbbf48f4, `hook_lint` 0
mismatches), host from an empty folder (ctest passed, two skipped). Three headless live sessions stepped by hand
with `tools/host_session.py step`: two from a copy of the approved save `odin-saves-pe-race2`, one from a copy of
`r5_saves`. The screenshots are game pixels and stay in the clone's `build/sessions/m0*`.

The log fields: the project name, G's current and previous sequence (`G + 8`, `G + 4`), last and next project
(`G + 0x34`, `G + 0x54`), the menu result (`game + 0x3A41C`). An `end` line prints the name buffer after the call,
so it shows the next project the finished one chose.

## 1. Projects and pages

| What the player sees | What the log shows |
|---|---|
| Opening movie, title | project `boot`, sequence 1 |
| Start during the opening (both saves) | `boot` ends with result `terminate`, next `gtmode`: GT mode's map at once |
| GT mode map, Home, the event maps | all pages of project `gtmode` (no project change between them) |
| Home > Options | `gtmode` ends, next `option`; sequence 1 starts `option` with last `gtmode` |
| Options, Triangle, "Exit Options?" OK | `option` ends with next `gtmode`, result empty; `gtmode` starts again on the **map** (cursor on Home) |
| European Events > continue Pan Euro race 2 | `gtmode` ends; sequence **3** (championship); `func_0010FE40("quick-championship")` |
| Pre-race menu, sixth icon "Options" | quick command **22**; `quick-championship` ends; `func_0010FE40("option")`; GT4's Options |
| Leaving that Options | `option` ends with result empty, so `func_0010FE40`'s loop ends; the sequence starts `quick-championship` again |
| Pre-race menu, Start icon | quick command 1, the race |
| Exit after a race, "Exit this race and go to menu screen?" OK | quick command 2; sequence 1 starts `gtmode` (previous 3) |

- **New quick project:** `quick-championship` (sequence 3) besides `quick-event`, `quick-arcade` and `quick-tt`. Its
  icons: Start, two driver and data icons, Settings (toolbox), a hand, **Options**, Exit; the cursor wraps. The
  Session Results screen after an exited race has an Options icon too (not selected in this run).
- **The mode select** ("Arcade Mode / Gran Turismo Mode", CAPTURES.md) is a page of `boot`: no other project runs
  between `boot` and `gtmode`. With both saves, Start during the opening went straight to GT mode, so the page
  itself was not reached; it has no Options item (CAPTURES.md lists its two).
- GT4's Options project: General (Race, Replay, Screen, Picture Quality, Sound), Arcade Mode (Arcade, 2 Player
  Battle, LAN Settings, Network, Course Registration), Photo Mode (Photo Lab), more below the scroll; Race holds
  Transmission, Screen Display, Map Display, Steering Display (GT4's own HUD choices).

## 2. Buttons in GT4's menus

- **Cross and Circle both confirm** (Circle opened Options > Race); **Triangle goes back** (sound `cancel`).
- **Start and Select do nothing on the GT mode map**: no sound, no project, no page (three tries, cursor on Home).
  Start in Home did nothing either (cursor on Options, then on Save), although GT4's map help line says "Pressing
  the START button in Home allows you to change car settings" [U: not reproduced].
- Triangle on the map plays `disable` and stays: GT mode has no way back to the mode select from the map.
- In a race, Start opens GT4's pause; Select changes the camera (lane A's route).

## 3. Sounds

- Through `MSound::play`: `ok` (confirm), `cancel` (back), `disable` (refused). One handle, 0x8C0458, in every
  project of the run (the global sound object).
- **Cursor moves log nothing**: those sounds are played inside the widgets, not through `MSound::play` [U: which
  function]. The pause page's sounds don't go through it either (the strobe's `PLAY_SOUND`, `RacePause::callFunction`).

## 4. The race pause (Pan Euro race 2, GT mode)

- `pauseStart`: `arg` = `RaceBasic + 0xE400` (the `RacePause`), time limit **0** (no timeout in this mode).
- `updatePause`'s answer `k`: **1** while open, **3** Continue, **4** Exit. Triangle anywhere in the pause, inside
  Screen Settings too, answers 3 (the race goes on).
- Items in a championship: **Continue, Exit, Screen Settings**; no Retry (GT4's `RETRYENABLE` off here). Exit asks
  "Are you sure?" with Continue preselected. Screen Settings: Brightness and Contrast as signed steps (+4 and -2 in
  this save) and OK. D-pad and Cross presses there were not taken in our run [U: M7 measures the page's input].
- Exit lands in `quick-championship`, then Session Results (the race counted, 0 points).

## 5. What the design takes from this

- **M6** hooks are confirmed for both paths: `startProject("option")` from sequence 1 (Home's Options) and
  `func_0010FE40("option")` from quick command 22 (pre-race and, by the same command, Session Results). On close the
  hub leaves what GT4's Options leaves: next = last (`gtmode` here), result empty. `quick-championship` joins the
  list of quick projects in MENU_DESIGN 1.3.
- **M8**: Start is free on the map, so the hub takes **Start** there (the coordinator's decision, case "Start opens
  nothing"). Home and the event maps are pages of the same project, so M8 needs the **map page** detected, not
  only `gtmode` [U: page detection, below].
- **Mode select**: a page of `boot` with two items and no Options. That Start there is GT4's own (it opens the
  menu) is [I], from CAPTURES.md, not this run, which never reached the page. If so, per the decision our Settings
  item is drawn beside GT4's two with its own button prompt (Select). That waits for a run that reaches the page,
  with page detection (open point 1).
- **M7**: the native pause page repeats Continue, Exit (with GT4's confirmation, Continue preselected), Screen
  Settings, and Retry only where GT4 enables it; Triangle continues from anywhere, as GT4's does. With a time
  limit of 0 in GT mode, the time-limit point of MENU_DESIGN 3.2 applies only to modes that set one.
- **The pad chord**: GT4 used neither Select nor Start on the map and in Home, and Select + Start was never seen as
  a GT4 input, so the chord stays Select + Start (lane C's point; L3 + R3 is not needed).
- **Sounds**: the hub plays `ok`, `cancel` and `disable` by name on the global handle; the cursor sound needs the
  widgets' own call (open point 2).

## 6. Open points (each before the ticket named)

1. **Page detection** (M8): which page of `gtmode` (map, Home, event maps) and of `boot` (opening, mode select) is
   up. Candidates in matched code: `MGame.major_menu_page` (001626B0) and the page natives `pushPage` 0022BCE0 and
   `closePage` 0022BD98 (0022BC20 is a corrected draft: not hooked). One more logging run.
2. **The cursor sound** (M5): the call the widgets make when the cursor moves.
3. **`MRenderContext + 0x6E0` and the menu repeat timing** (M5): not measured; GT4's pointer glides
   (CAPTURES.md), so the repeat is measured on our own page against GT4's list behaviour.

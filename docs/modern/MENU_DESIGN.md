# In-game settings menu: design (lane M)

Status: design for review (architect, lane C), then the coordinator's approval. No implementation code before it.
References: `main` 249d453, decomp pin 6dbbf48f4. Owner's direction (2026-11-08): the settings menu is **native**,
reached from GT4's own menus, behaves like one of GT4's screens and looks consistent with GT4; the host owns the
pixels (assets and text); the current host overlay menu goes.

Labels: **[C]** read in the code (decomp `src/`, the repo), **[I]** inferred, **[U]** unknown until spike M0.
Mockups (our own drawings, no game imagery): `docs/modern/menu/mock_*.svg`.

---

## 0. Decisions in one page

1. **Native pages in GT4's menu flow, not an overlay.** The page logic, navigation, input focus, GT4's menu sounds
   and frame pacing run **in the game process**, as native C++ hooked into matched code: the project flow
   (`startProject` 00109D00, which both ways into GT4's Options reach, M6) and the race pause
   (`RacePause::update` 003C3820, which every mode's `updatePause` reaches, M7). GT4's scripted menus on the disc
   are not edited.
2. **The host owns the pixels.** The game sends a draw list each frame (rectangles, asset IDs, text, at a virtual
   1920 x 1080 canvas with anchors); the host draws it at output resolution with its own images and SDF fonts.
   Nothing new goes into guest memory, GS memory or GT4's font pages.
3. **The host owns the settings.** The page engine gets the settings' schema and values from the host and asks the
   host to set them; the host validates, applies and saves, as `setOption` does today. One settings table
   (`settings_table.h` and the per-module files), no copy in the game.
4. **One page engine, built twice.** `platform/ui/` (plain C++17, no SDL, no Vulkan) is compiled into the game's
   runtime (x86-32) and into the host. In the game it runs GT4's flow; in the host it serves the modes without a
   game (replay, test scene). The host overlay menu (`settings_menu.*` and its drawing in `overlay.cpp`) is
   deleted; the pad chord and its guard (`chord_guard.h`) stay as a way in.
5. **Entry points**: GT4's own Options items (the top menu's and the quick menus' `COMMAND_OPTION`), Start on the GT
   mode map, and the race pause (a native pause page with a Settings item). The Guide button, Esc and the pad
   chord open it at the next safe point anywhere. In the `original` preset GT4's screens are unchanged and only
   those three open ours.
6. **Look**: GT4's design language (section 5), drawn with our own generated assets (Codex), prompts committed
   next to each asset, reviewed against GT4's screens for consistency, never copied.

---

## 1. What exists

### 1.1 The host overlay menu (to be deleted)

- `platform/sdl/settings_menu.{h,cpp}` [C]: a flat list of 30 rows in four sections (Graphics, Display, Gameplay,
  Image), Left / Right to change, a help line, `*` restart and `+` next-race markers.
- `platform/sdl/main.cpp:740-1082` [C]: opened by Esc, Guide or Select + Start (`ChordGuard`); while open the host
  sends `GT4_REC_HOST_CMD` 2 (the game's bus clock stops: `kernel_set_paused`, `runtime/link.cpp:392`), zeroes both
  pads, and holds them back until every button is up (`padHeldBack`). Drawn by `Renderer::setMenu` with the
  bitmap font of `overlay.cpp`.
- Its problems: it freezes the whole game (GT4's menus stop animating, its music stalls), it looks like a debug
  overlay, and it lives outside GT4's flow. Its pad chord works (`ChordGuard`: the chord's buttons never reach
  GT4, a short Start alone still does; `tests/host_settings.cpp:29-45`) and is kept.

### 1.2 GT4's menu system [C unless marked]

- **Main loop.** `func_00108040` switches on a *sequence* (`*D_00622F4C + 0x3A368`, struct G: previous `+4`,
  current `+8`, last menu project `+0x34`, next menu project `+0x54`, menu result `+0xB4`, during-quick-menu
  `+0xA5C`). Sequence 1 ("menu") runs `func_0010B290(G + 0x54)` -> `MenuGameObject::startProject` (00109D00),
  which loads `/projects/<name>/<name>` and returns when the project finishes. Races are sequences 2-16.
- **Menus are Adhoc scripts.** Each project is compiled Adhoc (`.adc`) with `.mproject` layouts on the disc, run
  by the VM at 0x2EA548-0x32F027. Pages and widgets are `MProject`, `MRootWindow`, `MComposite`, `MWidget`;
  `MRenderContext` starts, pushes and closes pages (0022BC20, 0022BCE0, 0022BD98; 0022BC20 is a corrected draft
  in `game_src`). The community's GT4 mods change menus by recompiling these scripts (section 6), which we will
  not do: the scripts are disc data.
- **Menu input.** `mUpdateContextPS2` owns two `MenuControl`s (`+0x1A0`, `+0x274`) that fetch a pad packet each
  frame (`PDISTD::ControlBase::fetch` 0055F7B0) from the pad thread, which reads `scePad2Read`, which the
  runtime answers from the link's pad record (`runtime/iop.cpp:504-642`, `link_pad()`).
- **Sounds.** Scripts play menu sounds by name through `MSound.play` (0023E2F8) -> `func_0023F8C8(handle, name)`.
  The names GT4 uses for cursor, confirm and cancel are [U] (M0 logs them).
- **Option values.** `GranTurismo4::Option` at `*D_00622F4C + 0x38CB0` (wide mode `+0x14`, menu BGM `+0x60`, menu
  SE `+0x68`, race BGM `+0x70`, automatic gear `+0x10C8`, race colour correction `+0x1184`, about 130 in
  `config/adhoc_methods.txt:715-900`). The controller assignment is at game data `+0x39E80`.
- **Render context pause.** `MRenderContext` has a pause word at `+0x6E0`, exposed to scripts; `func_0022F6A0`
  sets it around a sync. What it stops (widget updates, animations, both) is [I]; M0 measures it.

### 1.3 The four places, in the code

| Place | Where | How we detect it | Status |
|---|---|---|---|
| Mode select at game start | a page of project `"boot"` (M0) | `startProject("boot")`, then the page (M0 open point 1) | matched |
| GT mode map | a page of project `"gtmode"` (`D_0068CE08`), with Home and the event maps | `startProject("gtmode")` until it returns, then the page (M0 open point 1) | matched; Start and Select do nothing there (M0) |
| Pre-race menu | projects `"quick-event"`, `"quick-championship"` (M0), `"quick-arcade"`, `"quick-tt"` in `func_0010FE40`'s loop | the quick menu's command 22 `COMMAND_OPTION` runs `func_0010FE40("option")` (GT mode `func_00113B48:217`, arcade `func_00111498:155`, time trial `func_00112620:271`) | matched |
| Race pause | `RaceBasic::updatePlayCheckPause` 0038B0E0 -> `pauseStart` 0038B428 -> `RaceBase::setPause` (`+0xCD8 = 1`) | `RaceBase + 0xCD8 != 0`; the pause object at `RaceBasic + 0xE418` | matched; its menu is a strobe movie, `pause.strb`, and `RacePause::callFunction` 003C3260 is 0 % matched |

`RaceBasic::updatePause` acts on the pause object's return code: 3 continue (`pauseEnd` 0038B4B8), 4 exit, 5
retry, 2 timeout. GT4's pause items are Continue, Retry, Exit and screen brightness / contrast (the strobe's
`SETVAL` on `race_color_correction`); it has no Options item.

---

## 2. Integration

### 2.1 The options weighed

| | (a) host overlay | (b) items injected into GT4's scripts | (c) native pages in GT4's flow (chosen) |
|---|---|---|---|
| Behaves like a GT4 screen | no: freezes the game, its own input and timing | entry only | yes: GT4's frame, input path, sounds, flow |
| Needs disc data changed | no | yes: recompiled `.adc` / `.mproject` from Sony's scripts, a derived work we cannot ship | no |
| Draws through | host | GT4's sprites and font pages (guest and GS memory) | host, from the game's draw list |
| Cost | lowest | high, and a legal problem | about 1.5 x the overlay (section 7.3) |

The owner rejected (a). (b) is out: GT4's menu items exist only in disc scripts, and an Adhoc compiler (the GPL-v3
OpenAdhoc) would only help us ship modified Sony scripts. Writing our own Adhoc pages from scratch would avoid
that, but they would draw through GT4's sprite and font path, which the owner ruled out. So (c), with the entry
points placed where GT4's **native** code already branches on a menu choice or a button (section 3.2).

### 2.2 The split

```
 game process (x86-32)                                     host (x64 / ARM64)
 ---------------------------------------------------      --------------------------------------------------
 game_src hooks (matched code)                            settings (owner of every value; settings_table.h)
   startProject 00109D00  ->  menu: hub over "option"        ^ SettingsSet           | SettingsSchema/Values
   menu update 00109B38   ->  menu tick, sounds, leave       |                       v
   RacePause::update      ->  menu: pause page            runtime/menu (the game side)
 runtime/menu/  (page host in the game)                      page engine (platform/ui, shared)
   page engine (platform/ui, shared)  --GT4_REC_UI-->     platform/ui/ui_renderer: assets, SDF text,
   input focus (pad to GT4 neutral)                         at output resolution, after the display pipeline
   GT4's menu sounds (func_0023F8C8)                       asset registry: assets/ui/*.png + manifest
```

The game decides what is on screen and when; the host decides how it looks at the output's resolution.

### 2.3 Why the logic is in the game and the settings in the host

- In the game: the page must open and close at GT4's frame, take GT4's pad input away from GT4's own pages, play
  GT4's sounds, hold GT4's race pause, and return to GT4's flow with the state GT4 expects (menu result, next
  project). Only code in the game process can do that.
- In the host: every option's meaning (validation, presets, `unusedReason`, apply) is host code today. The page
  engine needs only a schema: rows, kinds, choices, ranges, labels, help, flags. The host sends it.

---

## 3. Architecture

### 3.1 Modules

| Module | Where | Lane | New / Changed | Notes |
|---|---|---|---|---|
| Page engine | `platform/ui/menu_model.{h,cpp}`, `menu_layout.{h,cpp}`, `draw_list.h` | M | New | Pages, tabs, rows, focus, repeat, transitions; lays out at 1920 x 1080 with anchors; emits a `DrawList`. No I/O. Built into the runtime and the host. |
| UI records | `include/gt4/link/protocol.h`, `include/gt4/ui.h` | M (A, D review) | Changed / New | Section 3.6. |
| Game-side page host | `runtime/menu/menu_host.{h,cpp}` | M | New | Ticks the engine once a game frame, owns input focus, plays sounds, holds the race pause, talks to the host. |
| Flow hooks | `game_src/func_00109B38.cpp` (M5), `func_00109D00.cpp` (M6), `func_003C3820.cpp` (M7) (copies of matched code) | M (A reviews) | New | One call each into `runtime/menu` (section 3.2). `tools/hook_lint.py` clean. No copy of `func_0010FE40`: its `"option"` reaches `startProject` too (M6). |
| Pad focus | `runtime/iop.cpp` `gt4_replace_scePad2Read` | D's file, M's change | Changed (one test) | Neutral pad to GT4 while a page owns the pad. |
| Settings bridge | `platform/ui/host/settings_schema.{h,cpp}` (host only: `tools/game_link.py` compiles `platform/ui`'s own sources into the game) | M (C reviews) | New | Builds the schema from `describe()` plus the page layout table (section 4); applies `SettingsSet`. |
| UI renderer | `platform/ui/ui_renderer.{h,cpp}` on `platform/render/modern/layer2d.*` | M (C reviews) | New | Draws a `DrawList`: textured quads, nine-slices, gradients, SDF text (E5), at output resolution, HDR at paper white. |
| Assets | `assets/ui/` (PNG sources, `manifest.json`, `prompts/`), `tools/ui_assets.py` | M | New | Section 5.4. Packed into an atlas at build time; installed beside the host. |
| Host wiring | `platform/sdl/main.cpp` | C's order | Changed | Old menu removed; Guide / Esc sends "open"; UI list to the renderer; no-game modes run the engine on the host. |
| Old menu | `platform/sdl/settings_menu.*`, menu drawing in `overlay.cpp` | M (C reviews) | **Deleted** | After M9 replaces every use. `chord_guard.h` and its test stay (the chord is a way in). |

### 3.2 Entry points

Each hook is a `game_src` copy of a matched function with one added call, guarded by `ui.entry` (section 3.8).

1. **GT4's Options, everywhere it is offered.** The quick menus' `COMMAND_OPTION` calls `func_0010FE40("option")`,
   and the top menu's Options goes through sequence 1 with next project `"option"` (M0). Both reach
   `startProject("option")`, so one hook there serves both (M6): with `ui.entry = integrated`, once GT4's Options
   project runs, our **Settings hub** opens over it (GT4's screen dimmed behind, its pad neutral). Its first tab,
   *Game*, has the row **GT4 options** (sound, controller functions, display) first: it closes the hub and GT4's
   own Options screen, already running, gets the pad, so nothing GT4 offered is lost and D1's rule holds (GT4's
   functions are assigned in GT4's screen). Closing the hub any other way ends GT4's Options as GT4's own exit
   from it does (M0: the result left empty, so `func_0010FE40`'s loop ends; next project = last, `G + 0x34`, for
   sequence 1): the menu update's hook calls MGame's set-next (0042FC50) with the last project and leaves the
   project's event loop (`MUpdateContext::leaveEventLoop`'s body, 00251E80), and `startProject` returns as after
   GT4's own exit. The hub opens over the project rather than in its place because a menu project is what runs the
   menu update the pages tick in and keeps GT4 drawing frames; the cost is GT4's Options loading first.
2. **Start on the GT mode map.** While project `"gtmode"` is on its map page, Start opens the hub. The pad hook
   (section 3.3) sees Start's edge before GT4's `MenuControl` does and swallows it. Start does nothing there in GT4
   (M0, M8), so Start opens the hub. The rule (coordinator for the owner, 2026-11-08; the owner asked for Start on the
   GT mode map): where Start is free, Start opens the hub; where Start already opens a GT4 menu, Settings becomes an
   item of that menu; Select only on a screen where Start does something else and there is no menu to add to. The map
   is read, not hooked (M0 section 9): the top of the menu project's page stack, a root window named `MAP`, active and
   at rest, so a dialog over the map, Home and every other page are not it; the pad thread takes Start only while the
   menu tick saw the map within two VBlanks. With `ui.entry = integrated` only (3.8).
3. **The mode select at game start.** It has no Options item (M0: two items). Point 2's rule applies once M8b's
   spike finds what Start does there: if Start is free, Start opens the hub, as on the map; if Start does
   something, Settings becomes our item beside GT4's two, with its own button (the mode select is itself the menu to
   add to).
4. **The race pause.** `ui.pause = native` (presets low to ultra from M9, 3.8): the copy of `RacePause::update`
   (003C3820) keeps GT4's pause (the race frozen by `RaceBase + 0xCD8`, the course clock stopped) but answers with
   our **pause page** instead of `pause.strb`, which never starts (`+0xC` stays 0, so `RacePause::render` draws
   nothing). The page offers only the items the pause object enables, as the strobe reads them, in GT4's order
   (Continue, Retry, Exit, Screen, as the object enables them; M7 measured Continue, Exit, Screen Settings in GT
   mode, and puts Retry where a mode with Retry shows it), then Settings last: Retry when `+0x28` (`RETRYENABLE`);
   Exit, confirmed with Continue preselected when `+0x2C` (`CONFIRMEXIT`); Screen when `+0x30`
   (`SCREENSETTINGENABLE`: brightness and contrast, -5 to 5 as GT4's sliders, written where `SETVAL` writes them,
   `D_00622F4C + 0x39E34`, or `+ 0x39E44` when `+0x34`); Settings, the host's tabs after the pause tab (until M4
   the host sends no schema, and Settings shows disabled). Triangle on the pause's pages continues, as anywhere in
   GT4's pause (M0). The answers are GT4's codes (1 open, 3 continue, 4
   exit, 5 retry), so each mode's `updatePause` switch and all after it are GT4's; the race's continue and exit
   events (`0x16`, `0x17`) and the time limit (2) end the page with the pause. With `ui.pause = gt4` the copy runs
   as GT4's function (DIFFTEST) and `pause.strb` shows as before, except in a pause command 6 opened (point 5).

   Modes covered: no class derives from `RacePause`, and its `update` is reached only through the pause's vtable, from
   `RaceBasic::updatePause` 0038B2B0 (GT mode, arcade, championship, free practice, license, machine test, training,
   LAN, net and split battle, arcade demo) and `RaceFreeRun::updatePause` (free run); both switches take 2 to 5.
   `SimplePause` (bit 0x80: B-spec and replays, M9d2's spike; no menu) stays GT4's in both settings.

   **A deliberate difference, native only:** GT4's time limit (`PauseBase::setTimeLimit`; 0, none, in GT mode, M0)
   counts the frames without input. Our page clears the count on input, as the strobe's input step does, and on
   every frame a Settings tab is shown: changing settings must never run out the clock and end the race.
5. **Anywhere: Guide, Esc, the pad chord.** Guide is often taken on the target devices (Steam's button on a Deck, Home
   under gamescope on the Odin), so a pad-only player needs the chord: Select + Start (M0: GT4 uses neither on the map
   or in Home). L3 + R3 is not a menu chord: held, it is the problem-dump key (`DumpChord`), and a host test keeps the
   two chords' buttons apart. `ChordGuard`'s rule stays: the chord's buttons never reach GT4, a short Start alone
   still does. Each of the three makes the host send `GT4_REC_HOST_CMD` 6 (open settings). The game opens the hub at
   the next safe point: at once in a menu project; in a race, a `game_src` copy of `RaceBasic::updatePlayCheckPause`
   (0038B0E0, matched; M9d2, from lane M's spike) acts on a pending command 6 as if Start (`RaceInput` 0x40) were
   down, only where GT4's own branch would reach `pauseStart(R + 0xE400)`: not in visual mode (`R + 0xCD0` 0), in play
   (`R + 0xCC8` 0: not B-spec, a replay or the demo), `R + 0xCDC` 0, no end delay (`org + 0xD4` < 0, `org` = `*(R +
   0x6C)`), and past the start (`R + 0xD4C` 1 at the green light, or `org + 0xBC` 0 without a countdown). GT4's own
   code then pauses exactly where Start would, with no pad bit and no remapping question; without a command the copy
   is GT4's function (`DIFFTEST`). Otherwise the command waits and is logged once. Every single-player class uses that
   function as it is (GT mode, arcade, championship, free practice, license, machine test, mission, free run), so
   Settings are reachable mid-race in all of them, time trials and single races included. B-spec races and replays
   pause through the `SimplePause` (bit 0x80; the spike corrects M0, which had it before the start), which has no
   menu: M9d3 brings B-spec in (below), and until then a command 6 there waits for the race's end. LAN, net and photo
   have their own handlers and wait for the next menu. The pause it opens shows our pause page with Settings, under
   `ui.pause = gt4` too: a pause opened by command 6 takes the native branch of the `RacePause::update` copy for that
   pause only, while a Start the player presses still gives GT4's `pause.strb`. That is a deliberate exception for
   `original` (the architect, 2026-11-09): GT4's own pause stays GT4's, and the host's Settings stay reachable in a
   race, as the old host menu had them. While the game waits (loading, movies, saves, the opening, a race before its
   start) the host says so, once, if no page arrives within a second: "Settings open at the next menu screen" until
   M9d2 (M9d1 can't tell a load from a race, so a command 6 in a race waits for the next menu too), then, from M9d2,
   the reason the game gives: when command 6 has to wait, the game sends a `GT4_REC_SETTINGS_SET` action, key
   `page.waiting`, value the reason (`load`, `race-start`, `race-end` for B-spec until M9d3, `menu`); the bridge
   answers it without touching a setting, and the host shows one line per reason: "Settings open when loading ends",
   "... when the race starts", "... when the race ends", "... at the next menu screen". No new record and no link
   version: an older host refuses an unknown action, which the game ignores. Without a game (replay, test scene) the
   host runs the engine itself.

No hook touches `RacePause::callFunction` (0 % matched) or the corrected draft 0022BC20.

**Threads.** `menu_host` ticks and calls `link_send` only on the guest thread that runs GT4's menu or pause update,
inside the hooked flow. It never runs on the frame clock's thread, which runs `on_frame` under `g_lock` and defers
ticks during a walk (CONTRACTS a.5.1). Command 6 is read where `link_poll` already delivers host records, and only
sets a flag; the hub opens at the next hooked update that sees it.

### 3.3 Input focus

- While a page is open, `gt4_replace_scePad2Read` gives GT4 a neutral pad on both ports (the pad thread, the
  `MenuControl`s and the race's `RaceInput` all read through it), and the page engine reads the same link pad
  bytes. After close, the pad stays neutral to GT4 until every button is up (the host's `padHeldBack` rule, moved
  into the game), so the closing button never acts in GT4.
- The page uses GT4's own confirm and back buttons as GT4's menus read them: Cross and Circle confirm, Triangle
  goes back (measured in M0). Shoulders change tabs, the D-pad and left stick move, held directions repeat with
  GT4's menu repeat timing: 20 VBlanks, then every 6 (measured in M0's second run).
- Remapping (D13) happens on the host before the pad record, so pages and GT4 see the same mapped pad.
- Mouse and keyboard on PC: the host maps keys to the pad as today; pointer input is sent as a `UiPointer` record
  and hit-tested by the engine against its own layout (M12, later).

### 3.4 Keeping GT4's state

- **Menus.** GT4's page under ours keeps running and drawing (its background animates, its music plays), it only
  gets no input. If M0 finds it must also be held (a page with a timer), the hook sets `MRenderContext + 0x6E0`
  for the duration, as `func_0022F6A0` does around a sync.
- **Races.** GT4's own pause holds the simulation. The host's bus-clock pause (`HOST_CMD` 2 and 3) is no longer
  used for the menu: GT4 keeps running its frame, its menu sounds and our page. M9 retires commands 2 and 3 (their
  numbers stay reserved) unless something else is found using them. A page never opens over a running race: the
  pad focus gives GT4 a neutral pad (no throttle, no steering), so in a race a page is only ever over GT4's pause
  (M7). M5's page host opens only while a menu project runs; an open asked for in a race waits for one.
- **Checks** (M5's acceptance): GT4's menu cursor, page and project unchanged after close; race time, lap, positions
  and the car's state identical before and after a pause with the settings opened (the vehicle record, A9, or
  D6's fields).

### 3.5 Sounds and transitions

- Cursor, confirm, cancel, page open and close: GT4's own menu sounds by name through `func_0023F8C8` with the
  current project's sound handle [U: names and handle from M0]. Changing a value plays the cursor sound; a
  refused value plays GT4's error sound.
- Transitions are driven by the game frame (60 Hz): the engine animates opacity and offsets in time, the host
  interpolates each item between two game frames at the display's rate, so a 144 Hz screen sees smooth motion.
  `ui.reduce_motion` replaces slides with fades.

### 3.6 Link records

`GT4_LINK_VERSION` goes up by one; lanes A and D review. A1, D2, A10 and D1 have landed. P1 (retiring the GIF and
VIF kinds, which keeps 1-3 reserved) and P3a also edit `protocol.h` and bump the version: whichever of M1 and P1
lands first takes the next version, the other rebases and takes the one after, one version per landing.
Record 16 and 17 (game to host), 68 (host to game) and host commands 6 and 7 are free. Command 5 is already used
and undocumented (`runtime/link.cpp:393`, `frame_forget_sent_contents`, sent by `gs_host` when it lost a texture
content): M1 lists it in `protocol.h`'s comment beside 6 and 7.

| Record | Direction | Payload |
|---|---|---|
| `GT4_REC_UI = 16` | game -> host, once a game frame while a page is open (and one empty list at close) | `ui::FrameHeader` (vblank, list id, flags: dim game, block HUD) then `ui::Item`s: quad, nine-slice, gradient, text, clip push / pop. Positions in canvas units, anchors, opacity, transform. Text as UTF-8 with font, size, weight, alignment, max width. Asset references by 32-bit ID from the manifest. |
| `GT4_REC_SETTINGS_SET = 17` | game -> host | `{u32 request, key, value}` as text; also `reset key`, `apply pending`, `revert pending`. |
| `GT4_REC_SETTINGS = 68` | host -> game | at open: the schema (pages, rows) and values; after each set: the new values of every row whose value or availability changed, the request's result and its apply class. |
| `GT4_REC_HOST_CMD` 6, 7 | host -> game | 6: open settings at the next safe point; 7: close (the host is shutting down). |

`include/gt4/ui.h` holds the structs (no 8-byte members, as CONTRACTS a.2).

- **Frame pairing.** Frame lists close at `GT4_REC_VSYNC`, and a UI record may arrive before or after it. So
  `GT4_REC_UI` carries its vblank, and the host draws a UI list with the `FrameList` of that vblank. It keeps
  drawing the last list on interpolated presentations until the next one arrives; the empty list at close clears
  it. The page and GT4's frame below never drift apart.
- **Size.** One list a frame. A text-heavy page is a few kilobytes, against the 96 MiB game-to-host ring; M1 caps a
  list (64 KiB) and counts the ones it drops.
- **Captures** record UI records only while a page is open; `original` replays never carry any unless a page was
  opened.
- **One way to the game.** A gameplay option changed in the menu reaches the game as `HOST_CMD` 4, the existing
  path, like every other setting the game reads.
- **Canvas.** The host scales a list's canvas (1080 units high, `canvasWidth` wide) by the content rectangle's
  height / 1080 and centres it there (M3, lane C's point). So at an aspect ratio narrower than the window the page
  stays inside the pillarbox, over the game's picture, never over the black bars, at every ratio alike. The page
  engine lays out for the content's own aspect (`LayoutOptions::canvasWidth`), which M9 gives the game.
- **Text.** One line in its box, or wrapped by the host at spaces when the box's height holds more than one of the
  face's lines; what still does not fit is cut at a code point with an ellipsis (the pages' Latin text: not
  grapheme-aware). The game has no font metrics, so wrapping is the host's (`draw_list.h`).

### 3.7 Settings bridge and apply classes

Each row has one apply class, shown on the row:

| Class | Examples | Behaviour |
|---|---|---|
| **live** | upscaler, sharpening, AA, effects, post, HUD scale, volumes, input response; `render.scene` and `lighting.night` (they switch the game's scene records on live: `kOptionSceneRecords` is resent every frame) | applied on the next frame, while the page is open; the game frame behind shows it (dimmed less on Graphics pages: section 4.3) |
| **confirm** | window mode, resolution, refresh rate, monitor, HDR, `display.gpu` (the host rebuilds its renderer) | applied, then "Keep these settings? 15 s" with automatic revert, as modern PC games do |
| **next race** | race frames (`display.race_frames`), model detail, mirror detail | stored; a badge says it applies from the next race (`nextRacePending` today) |
| **restart** | aspect ratio (read at the **game's** start, its `--aspect` argument), menu layout (`aspect.hud`) | stored; a badge and a prompt at close that the game must restart |

Only the menu's own sets go through confirm's revert. A setting set by a session (its `set` command) or by `--set`
never does, so sessions stay repeatable (C1). A confirm value is saved only once kept: the page sends the
`confirm.keep` action naming the key; a revert, a refusal or a value still waiting is never written to the settings
file (M4).

Values the current renderer does not use are greyed with `unusedReason` as the reason (ARCHITECTURE section 7).
GT4's own values (Option struct) are shown read-only on the Game tab with a link into GT4's screen, except the race
pause's brightness and contrast, which our pause page writes the way GT4's pause does [I: M0 finds the setter
`SETVAL` calls].

### 3.8 Settings of the menu itself, and `original`

| Key | Values | `original` preset | low / medium / high / ultra |
|---|---|---|---|
| `ui.entry` | `hotkey` (Guide, Esc, the pad chord), `integrated` (also GT4's Options items, Start on the map) | `hotkey` | `integrated` |
| `ui.pause` | `gt4` (GT4's `pause.strb`), `native` (our pause page) | `gt4` | `native` |
| `ui.scale` | 80-150 % | 100 % | 100 % |
| `ui.reduce_motion`, `ui.high_contrast` | `off`, `on` | `off` | `off` |

Until M9 wires the host's page drawing, every preset and the default set `ui.entry = hotkey` and `ui.pause = gt4`: a
setting never sends players into a page the host cannot draw (M6 first shipped `integrated` in low to ultra while the
host still ignored the draw list: GT4's Options got an invisible hub). M9 switches low to ultra to `integrated` and
`native`, one table line each (`settings.cpp` `presetFollowers`); until then M6's and M7's acceptance runs use
`--set`, and `host_settings` checks that no preset sets `integrated` or `native`. Since M9c the host draws the
pages, so the settings file takes `integrated` and `native` again (the opt-in of `docs/modern/settings-playtest.ini`,
the coordinator, 2026-11-09), while the presets and the default stay `hotkey` and `gt4` until M9f.

`applyPreset` sets the `ui.*` keys (as `input.response` already is in every preset), but `detectPreset` ignores
them, so a player who sets `ui.pause = gt4` on High still reads "Preset: High", not "Custom", for a choice that is
not a graphics one.

In `original`, GT4's screens are untouched: no hook acts, the pad path is unchanged and no UI record is sent until
the player presses Guide, Esc or the pad chord. The menu is always reachable, so a player can leave `original` from
it, pad-only too. Checks:
`ORIG` and `STREAM` with the page never opened (no UI record, the same link stream); a session that opens GT4's
Options in `original` sees GT4's `"option"` project start and `pause.strb` load.

### 3.9 Limits GT4's code still imposes (noted, not designed around)

- The page runs at GT4's 60 Hz frame (the host interpolates the motion).
- The pad GT4 and the page read is the PS2 pad record (18 bytes, 8-bit axes); finer input for the page (pointer,
  text) comes as its own records.
- The race pause time limit (`PauseBase::setTimeLimit`) applies to our pause page as to GT4's, but not while a
  Settings tab is shown (3.2, point 4).
- GT4's own values keep GT4's ranges and steps (volumes, colour correction).

---

## 4. The menu

### 4.1 Structure

A hub with tabs along the top (L1 / R1), one list per tab, a detail panel on the right, a help bar at the bottom.
Mockup: `menu/mock_settings.svg`.

| Tab | Rows (keys) | Notes |
|---|---|---|
| **Game** | GT4 options (opens GT4's screen); Gameplay: AI catch-up (`gameplay.ai_catchup`) and the QoL options the owner picks (section 6); Saves: backups and export (D3) | GT4's sound and controller values shown, read-only |
| **Graphics** | Preset (with a card listing what it sets), Renderer (`render.gs`), Scene renderer (`render.scene`, when hybrid ships), Resolution scale, Upscaler, Sharpening, Anti-aliasing, HD textures; **Advanced** (a fold): textures (filter, anisotropy, LOD bias), lighting (`effects.lighting`, `lighting.night`), effects (AO, shadows, reflections, bloom, DOF, motion blur, particles, sky and fog, heat haze), detail (mirror, models), race frames | A change to any advanced row shows "Preset: Custom" |
| **Display** | Window mode, Monitor, Graphics card (`display.gpu`), Resolution, Refresh rate, V-sync, Frame-rate limit, Frame interpolation, HDR (on, paper white, peak, with a calibration page), Aspect ratio, Menu layout, Safe area (with a frame to align) | confirm and restart classes |
| **Image** | Tone mapping, Exposure, Auto exposure, Bloom intensity, Film grain, Vignette, Chromatic aberration, Dither | live, with the game frame visible |
| **Controls** | Devices (D13) -> per device: Bindings, Steering and pedal response (`input.response`, `input.steer.*`, `input.pedal.*`: D12, with a live plot of stick against output), Haptics (D14), Trigger effects, Wheel (D15), Gyro (D16); Trigger travel; "Button functions: set in GT4's options" | E8a's content, as native pages; each page appears when its ticket has landed |
| **Audio** | GT4's volumes (read-only, link to GT4's screen); host output device and master volume; later the host mixer's options (D5, E8b) | |
| **HUD** | HUD scale, safe area, HUD on / off (E9); per-element toggles and gauge choice if the owner takes them (section 6) | |
| **Accessibility** | UI scale, high-contrast menu, reduce motion, hold-to-repeat speed, colour-vision filter (a post pass, lane B, later), button prompts style | |
| **System** | Performance readout, Restore defaults (per tab and all), About (versions, licences of fonts and assets), Quit game (with GT4's save warning) | |

### 4.2 Navigation, controller first

- D-pad / left stick move; Left / Right change a value (held: repeat, accelerating on sliders); Cross confirms or
  opens, and so does Circle; Triangle goes back (GT4's mapping, M0); L1 / R1 tabs; Square resets the row to its
  preset value; Start closes the hub from any depth. Keyboard: arrows, Enter, Backspace, Q / E, Esc.
- Focus is never lost: closing a sub-page returns to the row that opened it; reopening the hub returns to the last
  tab and row.
- Every row has a one-line help text and, where it helps, a picture in the detail panel (for example the preset
  card, the response plot, the safe-area frame).

### 4.3 Live preview

- Graphics and Image pages dim the game frame less (15 % instead of 60 %) and slide the list to one side, so the
  change shows on the game's picture behind. In a race the picture behind is the paused race.
- On a live-preview page the rows' list gets its own backing (ink at 78 %), and the game shows around it: the preview
  stays visible where the eye isn't reading (M9c: over GT4's bright GT mode map the values were unreadable at 15 %).
- Contrast, for M10's review and any later page or asset: every text on its backing at least 4.5:1, measured on the
  brightest game frame behind it (the GT mode map is a good worst case).
- A preset change shows its card before applying (Cross applies, Circle cancels): it can change twenty values.

---

## 5. Visual design

### 5.1 Style guide (from observation of GT4's screens: the GT mode map, the event entry, the race HUD)

- **Mood**: calm, photographic, premium; the content (cars, places) carries the colour, the interface stays
  neutral. Large areas of light grey and off-white or a dimmed photo; black bars.
- **Frame**: a black band at the top (title, status chips) and at the bottom (help text, button prompts), the
  content between. GT4's map uses a pale, desaturated "atlas" background with thin line work; lists sit on
  translucent dark bands over a full-bleed picture, each row a band of its own with alternating shade.
- **Palette** (ours, tuned to sit next to GT4's): ink black #0B0B0C, bar black #000000 at 85 %, panel grey
  #2A2C30 at 70 %, row shade #FFFFFF at 6 % / 10 %, paper #E9EAEC, line grey #9A9DA3, text white #F4F4F2,
  secondary #B9BCC2, **accent amber #F29A1F** (the selected row and values), **chip yellow #E8C21C** (status
  chips), confirm green #2E9B4F, warning red #D8452B. Highlights are text colour and a cursor mark, not big fills.
- **Shapes**: flat bands with square ends; rounded-square icon tiles (radius 10 % of the side) on a soft shadow,
  as plinths; a small solid triangular cursor mark beside the selection; thin 1-2 px rules. No glass blur, no
  neon, no heavy gradients.
- **Type**: a wide grotesque sans for everything, bold italic for titles and section names, regular for rows and
  help, generous tracking in the help bar; a classic serif only for place-like titles. Our fonts are open-licence
  look-alikes in feel, never GT4's: *Saira Semi Expanded* (titles, rows) and *Saira* (help), *Libre Caslon Text*
  (accent), all SIL OFL, rendered as SDF by E5. Numbers are tabular.
- **Icons**: small glossy 3D objects with a soft studio light (a monitor, a gauge, a pad, a speaker, a flag), on a
  grey plinth tile. Our own subjects; no logos, no car brands, no PlayStation shapes.
- **Motion**: short and quiet. Pages fade and slide 24 px in 200 ms (ease out); the selection bar moves in 90 ms;
  the cursor mark bobs gently (2 px, 1 s). Tabs cross-fade. Nothing bounces.
- **Sound**: GT4's own menu sounds (section 3.5); no new sounds.

### 5.2 Screens and their assets

| Screen | Mockup | Assets |
|---|---|---|
| Settings hub (any tab) | `mock_settings.svg` | 9 tab icons (game, graphics, display, image, controls, audio, HUD, accessibility, system) at 256 px with a selected and a disabled state; tab plinth tile (nine-slice); top and bottom bar gradients; row band (nine-slice); cursor mark; slider track and knob; toggle; value arrows; detail panel frame; preset cards (5 illustrations: original, low, medium, high, ultra, abstract car-and-road scenes of our own); badges (restart, next race, confirm, greyed) |
| Native pause page | `mock_pause.svg` | pause panel frame, item band, 5 item icons (continue, retry, settings, screen, exit), screen-adjust slider, confirm dialog frame |
| Controls pages | `mock_controls.svg` | device icons (gamepad, keyboard, wheel), generic pad diagram (our own drawing of a two-stick pad, no brand), response plot frame, haptics meter |
| Display calibration | none yet | safe-area frame corners, HDR test card (our own pattern: grey steps and a sun disc) |
| Dialogs and toasts | in the mockups | dialog frame, toast band, countdown ring |
| Button prompts | in the mockups | per device type, our own drawings: Xbox letters, keyboard keys, PlayStation face shapes, shoulders, sticks, D-pad |

Output sizes: masters at 4K layout scale (the 1920 x 1080 canvas x 2), PNG with alpha, sRGB; the build packs
them into atlases with mipmaps, and nine-slice insets in `manifest.json`. The renderer picks the mip for the
output (the HD layer at 1080p uses the half-size mip). Flat shapes that are drawn better as geometry (bands,
rules, gradients) are not images: the renderer draws them.

### 5.3 Rules for the art

- Original art only, in GT4's style, never a copy: no traced, scanned or reproduced GT4 imagery, no Sony,
  Polyphony or Gran Turismo names, logos or marks, no car manufacturer or tuner logos, no PlayStation button
  shapes copied from anywhere.
- Button prompts match the connected controller (D13 knows its type): Xbox letters, keyboard keys, and for
  PlayStation pads the four face shapes, all our own drawings (decision for the owner, section 9).
- Every generated asset is committed with the prompt that made it, so its origin is clear.

### 5.4 Generation with Codex

1. **Prompts in the repo.** `assets/ui/prompts/<asset>.md` per asset: the prompt, the style guide excerpt it
   uses, the size, the tool and date, the review result. `assets/ui/manifest.json` lists every asset with its ID,
   source file, prompt file, nine-slice insets and states.
2. **Generation.** Codex on this machine (`arc hosts` lists it), one batch per screen, from a shared preamble
   (section 5.1 as text) plus the asset's line. Several candidates per asset; the chosen one is cleaned (alpha,
   size) by `tools/ui_assets.py`, never painted over from a game picture.
3. **Review.** For each screen: the mockup filled with the candidates, shown beside GT4 screens of the same kind
   (local captures, never committed) at the same size. Checklist: palette within the guide, shapes and lighting
   consistent with GT4's icons, legibility at 720p and 4K, nothing that resembles a real logo or a GT4 asset too
   closely. The owner signs off each screen's set; the result goes in the prompt file.
4. **Build.** `tools/ui_assets.py` checks the manifest (every asset has a prompt and a review line), packs the
   atlas and mips, and fails the build on a missing entry.

---

## 6. Quality of life from the community mods

Sources: Gran Turismo 4 Spec II (TheAdmiester and team, v1.10, 2025-10-05; theadmiester.co.uk/specii, its
changelog and FAQ), the PCSX2 patch file for SCUS-97328 (pcsx2_patches), GTPlanet threads. **Licence**: Spec II
states none for the mod; its script source (OpenAdhoc-GT4SpecII, a fork of Nenkai's OpenAdhoc) is GPL-v3. We take
ideas only: no code, no scripts, no assets, and nobody implementing these reads that source.

| Feature (mod) | Native equivalent | Cost |
|---|---|---|
| Quick menu on Select (Spec II) | our hub from Start on the map, Guide / Esc anywhere | in this plan |
| Trigger sensitivity scaling (Spec II, PCSX2) | `input.trigger_travel`, D12 pedal response | exists |
| Always 480p / remembered (Spec II, PCSX2) | `display.race_frames = progressive` | exists |
| 16:9 menu and text scaling (Spec II, PCSX2) | `aspect.ratio`, `aspect.hud` | exists |
| Weaker vibration (PCSX2) | D14 `input.haptics.strength` | D14 |
| Shift on the right stick (Spec II) | D13 binding of a host input to the pad button GT4 shifts with (GT4's function untouched) | S, after D13 |
| HUD elements on / off, hide race text such as GET READY (Spec II, PCSX2) | hooks in `RaceBasic::render_panel` (0038B5A0, matched) per element, `hud.*` | M |
| Analogue or digital gauge in every view (Spec II, PCSX2) | the same hooks, `hud.gauge` | S-M after the above |
| GT3-style chase camera, far chase, higher exterior camera (PCSX2, Spec II) | camera parameters from the camera code, `camera.chase = original, high, far, gt3` | M (finding the parameters: S spike) |
| Cameras on / off in the view cycle (Spec II) | the view-cycle code, `camera.views` | S-M |
| Race length multiplier, laps for arcade up to 99 (Spec II) | the race entry's lap field before the race starts, `gameplay.laps` (prizes unchanged) | M |
| Global ASM / TCS override for all cars (Spec II) | the driving-aid values the race reads at start, `gameplay.assists_override` | M |
| Hidden option menus without the button combination (Spec II) | a row on the Game tab running GT4's hidden option project, if M0 finds how it is reached | S |
| GT mode map shortcuts L2 / R2 to GT Auto and Tuner Village (Spec II) | Map hook sets the next project (names from M0) | S-M |
| L1 / R1 jump 10 in car lists (Spec II) | script lists; only by synthesising presses on known pages | M, fragile: not proposed |
| Arcade all cars and tracks (Spec II) | an unlock flag read in arcade, not saved | M; owner decision (changes progression) |
| Garage sorting, owned-car marks, R3 zoom, regulation skip, typical opponents (Spec II) | script features | L each: not proposed |
| Prize car randomiser, economy and price changes, repaint, wheels, AWD swaps, engine sounds (Spec II) | game content and data | out of scope |
| Uncapped Photo Mode frame rate (PCSX2) | frame interpolation already presents above 60 Hz | check only |

---

## 7. Plan

### 7.1 Tickets

Sizes as PLAN.md. Every ticket: `CTEST`, `REL` where the game changes, `ORIG` and `STREAM` (records off and the
page never opened). Screenshots over the game stay local (game pixels) and are served on lane M's port for review.

**M0: Menu flow spike (S, 3 days).** Local instrumentation only. Log `startProject` names and returns,
`MGame.major_menu_project / page`, `menu_result`, the quick-work command, `MSound.play` names with their handle,
GT4's menu key table and repeat timing, what Start and Select do on the GT mode map and the mode select, the top
menu's Options path, the effect of `MRenderContext + 0x6E0`, the pause's `SETVAL` setter and time limits per mode,
and what `"option"` leaves in G on exit. Output `docs/modern/spikes/M0-menu-flow.md`. Depends: none.

**M1: UI and settings records (S).** `include/gt4/ui.h`, `include/gt4/link/protocol.h` (records 16, 17, 68, host
commands 6, 7, and command 5 documented; the version per section 3.6's order with P1), `runtime/link.cpp` (lane
C's file, lane A reviews) and `platform/gs/stream.cpp` routing. Depends: none left (A1, D2, A10 landed).
Acceptance: `CTEST` (`ui_records`: layouts, a round trip, the 64 KiB cap counted); `STREAM` unchanged with no page
open; a capture taken with no page open carries no UI record.

**M2: Page engine (M).** `platform/ui/menu_model.*`, `menu_layout.*`, `draw_list.h`, `tests/ui_menu.cpp`.
Depends: none. Acceptance: `CTEST`: navigation, repeat, tab memory, apply classes, confirm-revert timer, layout at
16:9, 21:9 and 4:3 against golden draw lists (text, not images).

**M3: UI renderer (M).** `platform/ui/host/ui_renderer.*` (host only, like `settings_schema`: `game_link` compiles
every source in `platform/ui` into the game, and this one includes the overlay and Vulkan). The shorter path (the
coordinator, 2026-11-09): M3 draws in SDR on E5's overlay primitives (its SDF text, quads and images), so M9 needn't
wait for E6. In HDR output until M3b, the UI draws at the player's `display.hdr_paper_white` (200 cd/m2 by default,
lane C's key), as the overlay already does (`Overlay::setHdr`), so the integrated entry behaves the same on SDR and
HDR displays. No asset is packed yet: asset 0 is flat colour, and an unknown asset draws flat and is logged once;
the packer and atlas (`tools/ui_assets.py`, the manifest) come with M10's first asset set. Until then the layout
names no asset at all (`kUiAssetsPacked`, which M10 sets) and draws its own stand-ins, so the owner never sees a
white square (the coordinator and the architect, 2026-11-09): the tabs as text chips, the controller prompts as key
caps with the button's name as text (Xbox letters; PlayStation's as words, Cross, Circle, Square, L1, R1, never a
button symbol; the keyboard's default keys). The style follows the controller in use (M9 gives it to the game).
Depends: E5 (SDF font). Acceptance: `VAL`; the host draws the hub from a recorded draw list at 720p, 1440p and 4K
over a plain backdrop, so the goldens stay small and do not move with the test scene (`tests/ui_page_goldens.py`;
these images are our own art only: committed as goldens); in HDR output the hub at `display.hdr_paper_white`,
measured headless as the overlay's PQ code (no HDR display on the desktop or the Odin): a flat colour of the hub
read in all three channels against the overlay's encode, which gives white at 200 cd/m2 as 148 of 255.

**M3b: The UI on E6's layer, HDR at paper white (S).** M3's renderer moved onto E6's output-resolution 2D layer
(`layer2d`), composed after the tone map with the HUD. Depends: M3, E6. Acceptance: `VAL`; the hub's goldens
unchanged in SDR; HDR at `display.hdr_paper_white` on the layer, measured headless as in M3.

**M4: Settings bridge (S-M).** `platform/ui/host/settings_schema.*` and the page layout table of section 4; host side
of `SETTINGS` / `SETTINGS_SET`. Depends: M1, M2, E1. Acceptance: `CTEST`: every option in `describe()` is on a page
or listed as hidden, from day one; a set through the bridge equals `setOption`; the four apply classes; a session
`set` and `--set` never start confirm's revert; `detectPreset` ignores `ui.*`. Depends also on M7 for two checks
M7 cannot make before the host answers the pause page's schema request: in a race with `--set ui.pause=native`,
pause, Settings, change a live option, close, Continue: race time, lap and car state identical to a pause with no
page; and the pause time limit not counting while a Settings tab shows (3.2, point 4).

**M5: Game-side page host (M).** `runtime/menu/menu_host.*`, the pad focus in `runtime/iop.cpp` (lane D reviews),
sounds through `func_0023F8C8`. Depends: M0, M1, M2. Acceptance: session test (`tools/host_session.py`): with a
page open GT4 receives a neutral pad on both ports, the closing button never reaches GT4, the page's sounds go
through GT4's call by GT4's names (logged; that they are heard is M9's, below), and GT4's page, cursor and project are
unchanged after close; `ORIG` and `STREAM` with no page opened (the hook runs every VBlank in every preset).

**M6: Options entry (S).** A hook in a copy of 00109D00 (both paths reach it), the leave in 00109B38's (M5), the
`ui.entry` setting (`settings_ui.cpp`) sent to the game (`kOptionUiEntry`; `hotkey` in every preset until M9, 3.8).
Depends: M5. Acceptance: session test from the approved save with `--set ui.entry=integrated`: the pre-race quick
menu's Options opens the hub (screenshot), *GT4 options* opens GT4's screen, and closing returns to the quick menu
with the race still startable; the top menu the same; `ui.entry = hotkey` opens GT4's `"option"` as before (project
log); `STREAM` with a page open (lane A: the UI list's rate, up to about 3.8 MB/s).

**M7: Native pause page (M).** A copy of `RacePause::update` 003C3820 (3.2, point 4); `ui.pause = gt4` in every
preset until M9 (3.8). Depends: M5, M0. Acceptance: `DIFFTEST` (a `game_src` copy); `ORIG`; session tests with
`--set ui.pause=native`: a pause in GT mode (`RaceBasic::updatePause`'s switch), in free run (`RaceFreeRun`'s own
switch) and in a mode with Retry, whose answer 5 must reach its switch; in GT mode: pause, Screen, Continue: race
time, lap and car state identical to a pause with no page (D6's fields or A9); Retry and Exit give GT4's results;
brightness and contrast stored as GT4's pause stores them. Settings shows disabled until M4's bridge sends the
schema, and never enabled without one (the Settings-in-a-race check and the time limit on a Settings tab are M4's).
`SimplePause` is GT4's by construction (its vtable's update slot holds 003C3A90, which isn't copied), and no M7 code
path references it. `ui.pause = gt4` loads `pause.strb` as before.

**M8: Start on the GT mode map (S).** The map detection and the button from M0. Depends: M5, M0. Acceptance:
session test: Start on the map opens the hub, other pages of `"gtmode"` are unaffected, the map's state after close
is unchanged. The hub takes Start only on the page detection says is the map (M0 open point 1), never in Home: GT4's
help line promises Start in Home for car settings, so Start in Home is checked unchanged even where it does nothing.

**M8b: Settings at the mode select (S).** The fourth way in the owner asked for (3.2, point 3): the mode select at
game start, a page of project `"boot"` with two scripted items (Arcade Mode, Gran Turismo Mode) and no Options. A
short spike first: what Start does on that page, and how the page is read (`boot`'s root windows have no name at
+ 0xE0, M0 section 9; the stack and state of M8's detection still apply). Then point 2's rule: if Start is free,
Start opens the hub, as on the map; if Start does something, Settings becomes our item beside GT4's two, with its own
button (GT4's scripted lists are not edited; the mode select is itself the menu to add to). M8b's commit says which
case it found, as M8 did for the map. With `ui.entry = integrated` only (3.8). Depends: M5, M8. Acceptance: session
test: on the mode select the button opens the hub and closing it leaves the page as it was; Arcade Mode and Gran
Turismo Mode still start as before; the opening and the attract video are unaffected.

**M9: Host wiring; the old menu deleted (S-M).** `platform/sdl/main.cpp` (C's order; D13's `pad_devices` already
moved the pad reads out of it, so the diff is small), `settings_menu.*` and the overlay menu drawing removed;
`chord_guard.h` kept; Guide, Esc and the pad chord send command 6; host commands 2 and 3 retired; the engine on
the host for replay and the test scene. The tests that go with the old menu move: `host_settings` links
`settings_menu.cpp` and drives the chord (`cmake/Host.cmake:313`): its chord cases (`tests/host_settings.cpp:29-45`)
stay, its menu cases are replaced by M2's golden draw lists; `GT4_HOST_KEYS` (docs/RENDERING.md:70, `scriptedKeys`),
how menu checks run unattended today, sends command 6 and the page's keys, and a session command `open_settings`
does the same. The host gives the game the layout's inputs: the content's canvas width, and the prompt style of
the controller in use (SDL's gamepad type: Xbox or PlayStation names; the keyboard's when it was used last; the
coordinator, 2026-11-09). M9 also switches presets low to ultra to `ui.entry = integrated` and `ui.pause = native`
(3.8; the default too), and `host_settings`' check that no preset sets them turns into the table's values.
In three steps (lane M, 2026-11-09): M9d1, the host side (Esc, Guide and the chord send 6, and 7 while a page is open;
`ChordGuard` kept, the dump chord's buttons kept apart, which a host test checks), with command 6 opening at the next
menu project until M9d2; M9d2, the copy of `RaceBasic::updatePlayCheckPause` of 3.2 point 5 (lane M's spike); M9d3,
B-spec's pause: how a `SimplePause` can carry our page (a copy of `SimplePause::update` 003C3A90 with the same native
branch for a pause command 6 opened, if that function is matched), a spike first. Depends: M3, M4, M5. Acceptance:
`CTEST`; `ORIG`; Guide and the chord in a race pause through GT4 and open Settings, with `ui.pause = native` and with
`ui.pause = gt4`, in GT mode, a time trial, a single race and (M9d3) a B-spec race, a short Start alone still pauses
GT4 (with `ui.pause = gt4`, `pause.strb`); command 6 during the opening, a load or a movie, and before the start,
changes nothing in the game (a session test); the replay and test scene open the host-run hub; the pages' sounds heard
(moved from M5: in the game's own mix, `GT4_AUDIO_WAV`, the map's sequenced music hides a menu sound, GT4's own
included), by an A/B of two runs with the same inputs, one moving a page's cursor and one not.

**M10: Style guide and the first asset set (M).** `docs/modern/menu/STYLE.md` (section 5.1, extended), the prompts
and assets of the hub and pause screens, the review of section 5.4, and the asset pipeline moved here from M3
(`tools/ui_assets.py`, the manifest, the packer and atlas). Depends: M3 (to see them drawn). Acceptance: every asset
in the manifest with a prompt and a review line; the packer's output reproducible; side-by-side review sheets served
for the owner; the owner's sign-off.

**M11: Controls, Audio and HUD pages (M).** E8a, E8b and E9's pages as native pages; per-device profiles (D13).
Depends: M4, M5, D12, D13 (E8a), D5 (E8b), E6 (E9). Acceptance: E8a's: every D12 setting set and reset from the
pad alone, a profile set in the menu matches the file, keyboard only works; E9's.

**M12: Pointer input and accessibility options (S-M).** `UiPointer` record, `ui.scale`, `ui.high_contrast`,
`ui.reduce_motion`. Depends: M9. Acceptance: `CTEST` hit testing; the hub usable with the mouse alone.

**Q1-Q8: QoL options, after M1-M9** (the coordinator's choice for the owner, 2026-11-08). Each its own ticket with
a spike for its hook, a setting off in `original`, and a session test: Q1 HUD element toggles and Q2 the gauge
choice (`hud.*`), Q3 chase camera options and Q4 cameras in the view cycle (`camera.*`), Q5 the hidden options
row, Q6 the GT mode map shortcuts L2 / R2, Q7 shift on the right stick (D13), Q8 the laps multiplier and the
assists override (`gameplay.laps`, `gameplay.assists_override`). Arcade unlocks are left out: the owner decides
about progression later.

### 7.2 Order and lanes

- Lane M owns (PLAN 549aacf) `platform/ui/`, `runtime/menu/`, `assets/ui/`, `tools/ui_assets.py`, the menu-flow
  `game_src` copies (00109D00, 0010FE40, 003C3820, 00109B38), `include/gt4/ui.h`, a future `settings_ui.cpp`, and
  `settings_menu.*`, `layer2d.*`, `overlay.*`, the UI draw list and SDF text, with lane C reviewing; and the tickets
  E5, E6, E7 and E9. So E5 before M3 is lane M's own order.
- Shared files: `protocol.h` serialised with P1 and P3a (section 3.6; A, D review); `runtime/link.cpp` (C's, A
  reviews); `runtime/iop.cpp` (D reviews; M5's pad focus sits after D13's mapping); `stream.cpp`; `main.cpp` (C's
  order); the settings table (`settings.{h,cpp}`, `settings_table.h`, presets and the shared display, render,
  effects and scene files) is lane C's; per-module files stay with their lanes (`settings_input` and
  `settings_audio` D, `settings_lighting` F), lane C reviewing. `settings_menu.*` is deleted by M9.
- PLAN.md's E8a and E8b become M11's pages (their content unchanged); E9's HUD work and its rows are lane M's.

### 7.3 Cost

| | Overlay (a) | Native (c) |
|---|---|---|
| Engine, renderer, bridge, assets (M2, M3, M4, M10) | 6-8 weeks | the same |
| Spike, records, game-side host, entries (M0, M1, M5-M8) | none | 3-5 weeks |
| Host wiring (M9) | 1 week | 1 week |
| Total, one agent | about 7-9 weeks | about 10-14 weeks |

---

## 8. Risks

| Risk | Where | Mitigation |
|---|---|---|
| Unknown script behaviour around our hooks (what `"option"` leaves, Start on the map) | M6, M8 | M0 first; hooks restore exactly the state M0 logs; session tests compare G before and after |
| Adhoc VM coverage | none added | we run no new script; the VM only runs GT4's own scripts as today |
| Drafts on the path | `RacePause::callFunction` (0 % matched), 0022BC20 (corrected draft) | not hooked; our hooks are on matched code only |
| Vtable reachability of the pause | M7 | one copy of `RacePause::update`, which no class overrides and both `updatePause`s reach through the pause's vtable (3.2, point 4); M7 runs a pause in GT mode, in free run and in a mode with Retry; `SimplePause` is GT4's by construction (its vtable's update slot holds 003C3A90) |
| The pad bits GT4 maps to Start (`0x40`, `0x80`) come from its controller config | M5, M8 | the page reads the pad record and GT4's mapping (M0), never a fixed bit |
| A page left open when GT4 changes sequence (a race ends, a timer) | M5 | the page host closes on any sequence or project change and logs it |
| `original` drift | all | `ui.entry = hotkey`, `ui.pause = gt4`; `ORIG` and `STREAM` with no page opened |
| Generated art too close to GT4's or to a real mark | M10 | the review in 5.4; the owner's sign-off per screen |

## 9. Decisions (the coordinator for the owner, 2026-11-08)

1. Start on the GT mode map, as the owner asked. If GT4 already uses Start there, Settings becomes an item of what
   Start opens (section 3.2, point 2); Select only if Start opens nothing.
2. `ui.pause = native` in presets low to ultra: our pause page replaces GT4's. Continue, Retry, Screen and Exit
   behave exactly as GT4's.
3. Button prompts match the connected controller, our own drawings, with the four face shapes for PlayStation pads.
4. QoL Q1-Q8 (section 7.1) after M1-M9; arcade unlocks left out for now.

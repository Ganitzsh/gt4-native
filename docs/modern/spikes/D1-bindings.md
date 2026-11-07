# Spike D1: GT4's own controller setup

Status: answered statically (ticket D1), 2026-11-07, on `main` e673e2e. Read from the decompilation and, read-only,
from `CORE.GT4.elf` (SCUS-97328 v1.01). The DualShock 2 apply step (0043BBC0) and the channel builder (00564C10)
have no C file, so I read them in a disassembly. The name tables, the DS2 defaults and the pressure constants were
checked twice. Then measured live, 2026-11-07, on a release game and host from empty folders (2af64b4): GT4's own
controller screen, and a trigger sweep in a race (below). Questions, from the architect: GT4's default race bindings
(L1/R1/L2/R2 included), whether accelerate and brake can be put on R2/L2, and whether pressure is read.

## Answers

| # | Question | Answer |
|---|---|---|
| 1 | Default DualShock 2 race bindings | Left stick X: steer. Right stick up / down: accelerate / brake (analog). Cross: accelerate. Square: brake. Circle: handbrake. Triangle: reverse gear. **R2 / L2: shift up / down. L1: back view. R1: `SPECIAL`.** Select: change view. Start: pause. R3: the licence best line. D-pad left / right: steer; up: ghost; down and L3: none. One default layout (below). |
| 2 | Can accelerate and brake be put on R2 / L2? | **Yes, as far as the engine goes.** Each of the 16 buttons holds any of the 18 button functions, and each of the 12 stick keys any of the 7 analog functions. The setter checks only the key index. **And GT4's own screen offers it** (seen live: Options > Controllers > DUALSHOCK 2 button config 1P; Gas and Brake each list ○ ✕ △ □ R1 R2 L1 L2, Select, up, down, none and the left stick's halves, and a clash is flagged in orange). That screen shows the defaults of answer 1, with L1 named Rear View. |
| 3 | Is pressure read? | **Yes, for accelerate, brake, handbrake and reverse, on any of the 12 pressure buttons** (R2 and L2 included), when the pad reports DualShock 2 pressure mode. Our host always reports it (`iop.cpp:569`). The game's curve is steep: accelerate is full from pressure 45 of 255, brake from 84. D-pad pressure always steers. |
| 4 | Wheels and presets | Separate configs per port for the DualShock 2, the GT Force (046d:c293), the Driving Force / EX (c294) and the Driving Force Pro (c298), each with its own buttons and defaults. Only the Driving Force Pro has presets (3: lever up/down shifts, the lever reversed, the lever as handbrake/reverse); the DualShock 2 has one. |

## The configuration

`OptionData + 0x11D0` holds 2 ports (`0xB8` bytes each). Each port holds the four device configs
(`OptionRaceInputPortPS2`: DualShock 2 at `+0x18`, `func_0043CCD0`; `getDevice` names "DUALSHOCK 2" and the rest
index 0, "COUGAR" 1, "JAGUAR" 2, "CHEETAH" 3). The DualShock 2 config (`func_0043C0A8`..`0043C160`; saved as `0x1E`
bytes from `+0x10`, `func_0043C170` / `0043C200`):

| Offset | Content |
|---|---|
| `+0x10` | preset (the reset writes 1; any setter writes 0: custom) |
| `+0x11..+0x1C` | 12 stick keys, in the order of `D_00623358`: `LX LY RX RY LXF LXL LYF LYL RXF RXL RYF RYL` (the last 8 are half-axes, `F` the low side: left/up). One `ANALOG_*` id each, -1 none |
| `+0x1D..+0x2C` | 16 buttons, in the order of `D_00623390`: `LEFT RIGHT UP DOWN TRIANGLE CIRCLE CROSS SQUARE L1 R1 L2 R2 L3 R3 START SELECT`. One `BUTTON_*` id each, -1 none |

The function ids are table indexes. `D_00622E08`: `ANALOG_STEER SHIFTDOWN SHIFTUP ACCEL BRAKE SIDEBRAKE
REVERSEGEAR`. `D_00622E28`: `BUTTON_LSTEER RSTEER SHIFTDOWN SHIFTUP ACCEL BRAKE SIDEBRAKE REVERSEGEAR SPECIAL
PLAYVIEW BACKVIEW FRONTVIEW REARVIEW LEFTVIEW RIGHTVIEW LICENCE_BEST_LINE GHOST PAUSE`.

The reset, `func_0043B808` (vtable slot 3), writes `LX = STEER, RYF = ACCEL, RYL = BRAKE`, the other sticks -1. Its
buttons, in table order, are `0, 1, 16, -1, 7, 6, 4, 5, 10, 8, 2, 3, -1, 15, 17, 9`, which is the table in answer 1.
It takes a preset number and stores it, but the layout doesn't depend on it.

Setting: `MOption__setButtonConfig(device, key, function)` resolves the key name (`func_00438588`) and calls
`func_0043C120`, which is `if (key < 16) { preset = 0; button[key] = function; }`. `MOption__setAnalogConfig` works the
same way for the sticks. `MOption__clearButtonFunction` and `MOption__unsetButtonKey` set keys holding a function to
-1, presumably so the screen keeps each function on one key.

## Pressure and the sticks (`0043BBC0`, disassembled)

- **Pressure** is read only when the pad type, the high nibble of the packet's byte 2, is 5. `func_00551ED0` sets
  5 for the libpad mask `0xFFFFFFFF`: 16 buttons plus pressure.
- Each pressure button's channel (`D_006A64E0`) is offset 6..17 of our 18-byte frame, as `ps2_pad.h` orders them:
  Cross 12, Square 13, L2 16, R2 17.
- For a nonzero pressure p on a button bound to one of these functions, the value is
  `min(min(2p, 256) * 100 / K, 256) << 4` (0..4096). Any other function on that button stays digital.

  | Function | K | Full from p = |
  |---|---|---|
  | accelerate | 35 | 45 |
  | brake | 65 | 84 |
  | handbrake | 20 | 26 |
  | reverse | 35 | 45 |
- The D-pad steers by pressure regardless of its bindings: `(f(left) - f(right)) << 4`, with K = 60. Left or right
  held clears up and down.
- **Sticks**: no dead zone, linear curves. A non-steer key reads `D_00623158[raw]` (about 128.5 v). Steer reads
  `D_00622F58[raw]` (about (v - 128) · 256), and is skipped when the D-pad already steered. The half-axis channels
  are built in `00564C10` (low side `max(0, 127 - raw)`, high side `max(0, raw - 128)`; channel table `D_006A64C0`).

## What it means for D1

- **The host keeps the PS2 mapping** (decision, architect, 2026-11-07). The triggers drive the L2 / R2 pressures, as
  they always did; GT4 binds L2 / R2 to shifting and Cross / Square to accelerate and brake. To drive on the
  triggers, the player sets Gas = R2 and Brake = L2 (and moves the shifts) in GT4's own screen (answer 2), which the
  game keeps in its save. The host does not remap buttons.
- **Why not `input.pedals = triggers`** (efb568e, then 87a9657, both removed): any host remap makes GT4's controller
  screen and its button prompts disagree with what happens. The first version put the right trigger on Cross's
  pressure, so pulling a trigger in a menu confirmed. The second also moved the gamepad's south and west buttons to
  R2 / L2 so that a manual gearbox kept its shifts. That made south press R2, so ✕ no longer confirmed: the pre-race
  screen would not start a race (seen live). The options were never in a release; an old settings file that has
  them still loads, with one warning for the unknown key.
- **`input.trigger_travel = game | full`** (default `game` in every preset) is what the host adds. GT4's curve uses
  only the first part of a trigger's travel: with the trigger linear on 0..255, accelerate is full at 18 % and brake
  at 33 %, as on the DualShock 2. With `full`, the game's `scePad2Read` rescales the pressure bytes of the buttons
  GT4's live assignment has accelerate and brake on, to 0..45 and 0..84, with 255 at full travel. The pressed bits
  are left alone. The assignment is read where the race reads it: the ports at `*0x00622F4C + 0x39E80`, `0xB8`
  bytes each from `+4`, each port's DUALSHOCK 2 config at `+0x18`, checked by its vtable word `0x00688210` at
  `+0xC`. A boot snapshot showed both ports there with the default layout, and two more DUALSHOCK 2 objects at
  `0x009A71xx` (the Options screen's working copy). **Where the assignment can't be read** (early in the boot, or a
  port whose config isn't a DUALSHOCK 2 one), nothing is scaled: the game's own behaviour, logged as "assignment
  unknown: not scaled" with `GT4_TRACE_PAD=pedals`. The mode is a live game option (`kOptionTriggerTravel`, 6).
- **The sweep, the acceptance** (2026-11-08: live race, Nürburgring, Ford Ka; a release game and a host from empty
  folders at d752981, the PS2 mapping). Gas = R2, Brake = L2 and the shifts on ✕ / □ were set in GT4's own DUALSHOCK 2
  button config, then the triggers were swept. The pressure sent against the travel comes from
  `GT4_TRACE_PAD=pedals`. Every line read "game's assignment" with accelerate on key 11 (R2) and brake on key 10
  (L2): the scaling followed GT4's live assignment. (An earlier run through the since-removed host remap, on Cross
  and Square, gave the same numbers.)

  | Travel | `game` | `full`, accelerate (R2) | `full`, brake (L2) |
  |---|---|---|---|
  | 18 % | 46 | 8 | 15 |
  | 50 % | 127 | 22 | 42 |
  | 90 % | 229 | 40 | 75 |
  | 98 % | 250 | 44 (GT4's full: 45) | 82 (84) |
  | 99 % | 252 | 44 | 83 |
  | 100 % | 255 | 255 | 255 |

  With `full`, accelerate and brake reach GT4's maximum only at the end of the travel (the `pad_pedals` ctest: from
  99.2 % and 99.6 %). With `game` the pressure is the travel, as before.
- **Port 2's socket** (found while setting the controls live): GT4 creates its two pad sockets with the port in the
  request's second word, and the runtime took it from the first, so both sockets read pad 1. The game's pad update
  reads both ports every VBlank, so a menu that takes either pad (the Options grid, the button-config rows) saw each
  press twice and moved two items, and some rows (Gas, OK) could not be reached. Fixed separately (`fix(pad)`); port
  2 now reads its own, neutral, pad. Reporting port 2 as unplugged is D1's remaining part.

## Open

1. Whether full right stick (half-axis peak 127, about 2040 of 4096 after the `>> 3` in `func_004386F0`) is full
   throttle, or whether a later stage rescales it. It needs the game's computed accelerate value; no decision until
   measured.
2. The device names of the pad types 1..5. They are inferred from the order of the `getDevice` name checks.

## Method

I read the decompiled C: `func_0043B808`, `func_0043C0A8`..`0043C200`, `func_0043CCD0`, `func_0043CDC0`,
`func_004383D8`, `func_00438588`, `func_004386F0`, `func_00438870`, `MOption__setButtonConfig` / `setAnalogConfig` /
`clearButtonFunction` / `unsetButtonKey` / `get` / `setDeviceConfig`, `getDevice`, `func_00551ED0`,
`func_005666E0`, and the wheel defaults `func_0043B8D8` / `0043B928` / `0043B9D8`. `0043BBC0` and `00564C10` were
read in a MIPS disassembly. The tables came from the ELF's loaded segments, read in place: the four name tables and
the channel tables. The defaults were checked against the name tables byte by byte, and the pressure divisors (60,
35, 65, 20, 35) and the type-5 compare in the disassembly.

# Spike D1: GT4's own controller setup

Status: answered statically (ticket D1), 2026-11-07, on `main` e673e2e. Read from the decompilation and, read-only,
from `CORE.GT4.elf` (SCUS-97328 v1.01). The DualShock 2 apply step (0043BBC0) and the channel builder (00564C10)
have no C file, so I read them in a disassembly. The name tables, the DS2 defaults and the pressure constants were
checked twice. Nothing was measured live yet (see Open). Questions, from the architect: GT4's default race bindings
(L1/R1/L2/R2 included), whether accelerate and brake can be put on R2/L2, and whether pressure is read.

## Answers

| # | Question | Answer |
|---|---|---|
| 1 | Default DualShock 2 race bindings | Left stick X: steer. Right stick up / down: accelerate / brake (analog). Cross: accelerate. Square: brake. Circle: handbrake. Triangle: reverse gear. **R2 / L2: shift up / down. L1: back view. R1: `SPECIAL`.** Select: change view. Start: pause. R3: the licence best line. D-pad left / right: steer; up: ghost; down and L3: none. One default layout (below). |
| 2 | Can accelerate and brake be put on R2 / L2? | **Yes, as far as the engine goes.** Each of the 16 buttons holds any of the 18 button functions, and each of the 12 stick keys any of the 7 analog functions. The setter checks only the key index. Not checked: whether the Options screen (a script, not decompiled) offers every pair. |
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

- **`original` as it stands:** our triggers drive the L2 / R2 pressures, and GT4 binds L2 / R2 to shifting. A gamepad
  driver therefore shifts on the triggers and accelerates on Cross, which is digital on a modern pad (pressure 255 or
  0). That is the PS2 behaviour and stays as it is.
- **A player can already get analog trigger pedals inside GT4:** Options, controller: accelerate to R2, brake to L2,
  shifting to two other buttons. The engine allows it (answer 2) and the triggers already carry pressure. It is kept
  in the game's save. Whether the screen offers these pairs is the first open check.
- **`input.pedals = triggers`** (efb568e) puts the right trigger on Cross's pressure and the left on Square's, so the
  defaults reach accelerate and brake with no change in GT4. Two things follow from the code above:
  1. **Fixed with this spike:** it took the trigger axes off L2 / R2, and on a gamepad those axes are L2 / R2's only
     binding. With GT4's defaults the player had no shift up or down, so a manual gearbox couldn't be driven. Now
     `triggers` moves the gamepad buttons on the throttle and brake buttons (south and west) to R2 / L2, which swaps
     them with the triggers. `tests/input_map.cpp` checks it.
  2. **The game's curve uses only the first part of the travel:** with the trigger linear on 0..255, accelerate is
     full at 18 % of the travel and brake at 33 %, as it was on the DualShock 2. A `triggers` option could scale the
     right trigger to pressure 0..45 and the left to 0..84, so the full travel is used. GT4 has only that many steps
     anyway. This works only while the throttle and brake buttons keep GT4's defaults: the host doesn't know the
     game's binding. It would be a setting outside `original`; the decision is the architect's.
- **Port 2** has its own full set (the split-screen menu), so D1's "both ports sent" needs nothing else from the game.

## Open (to check live, in a race on a copy of the approved save)

1. Whether the Options controller screen offers accelerate on R2 and brake on L2 (its script isn't decompiled).
2. Whether full right stick (half-axis peak 127, about 2040 of 4096 after the `>> 3` in `func_004386F0`) is full
   throttle, or whether a later stage rescales it.
3. The device names of the pad types 1..5. They are inferred from the order of the `getDevice` name checks.
4. A trigger sweep with `input.pedals=triggers`: the throttle the game shows (or the car's speed) against the trigger
   position, which would confirm the K = 35 knee at 18 %.

## Method

I read the decompiled C: `func_0043B808`, `func_0043C0A8`..`0043C200`, `func_0043CCD0`, `func_0043CDC0`,
`func_004383D8`, `func_00438588`, `func_004386F0`, `func_00438870`, `MOption__setButtonConfig` / `setAnalogConfig` /
`clearButtonFunction` / `unsetButtonKey` / `get` / `setDeviceConfig`, `getDevice`, `func_00551ED0`,
`func_005666E0`, and the wheel defaults `func_0043B8D8` / `0043B928` / `0043B9D8`. `0043BBC0` and `00564C10` were
read in a MIPS disassembly. The tables came from the ELF's loaded segments, read in place: the four name tables and
the channel tables. The defaults were checked against the name tables byte by byte, and the pressure divisors (60,
35, 65, 20, 35) and the type-5 compare in the disassembly.

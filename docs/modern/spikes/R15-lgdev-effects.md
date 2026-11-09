# R15: the force effects GT4 sends a Logitech wheel (lane D, 2026-11-09)

RISKS.md R15 (blocks D7) asks for the parameter layouts of the effects GT4 sends through LGDEV. Answer: GT4 drives
**three effects, each a 60-byte block** in one layout. Effect A is a constant force whose magnitude GT4 updates every
frame of a race; B and C are a damper and a centring spring, set when the race starts and left alone. The layouts
come from the decomp's matched C (all 28 client functions, `src/libstream/unit_005547E8/func_00560778`..`00561D10`),
and a race with a fake Driving Force Pro confirms them live: GT4 opened it, downloaded the three blocks byte for byte
as the code builds them, and updated A 1,216 times in a race.

## The interface (corrects docs/IOP_SERVICES.md 5.8)

One 0x240-byte buffer (`D_00873F40` blocking, `D_00654AC0` asynchronous), sent and received in place, the result
code at `+0`. Three of 5.8's labels were wrong (fno 7 is the effect download, 8 starts one effect, 14 reads several
devices); 17 and 18 save and restore a 0x160-byte state rather than scan.

| fno | Call | Sends | Gets back |
|---|---|---|---|
| 12 | init (`00560778`) | `+0x5C` the EE address of a 64-byte notify area | `+4` version (0x010Bxxxx) |
| 1 | device info (`00560AC0`) | `+4` index | `+0x70` 0x18 bytes; a negative result ends the scan |
| 2 / 3 | open / close (`00560B88`, `00560C28`) | `+4` index / `+8` handle | `+8` handle |
| 4 / 5 | get / set property (`00560CA0`, `00560D50`) | `+8` handle, `+0x58` id (`+0x5C` value) | `+0x5C` value |
| 6 | read input (`00560DE8`) | `+8` handle, `+0x60` mode | `+0x70` 0x18 bytes |
| 7 | download an effect (`00561040`) | `+8` handle, `+0x70` the 60-byte block | `+0x18` the effect handle |
| 13 | update an effect (`00561170`) | `+0x18` effect handle, `+0x70` the 60-byte block | |
| 8 / 9 / 10 | start / stop / unload an effect | `+0x18` effect handle | |
| 15 / 16 | start / stop several | `+0x18..` up to 16 handles, `+0x5C` count | |
| 14 | read several devices | up to 4 handles, `+0x5C` count, `+0x60..` modes | `+0x70 + 0x18 i` |
| 17 / 18 | save / restore state | (18: `+0x70` 0x160 bytes) | (17: `+0x70` 0x160 bytes) |

Never sent: 14, 16 and 17 have no call site; 18 only if a saved state exists.

**Discovery** (`func_00555760`, from the VBlank thread `005552B8`, every VBlank while no device is open or the notify
area's first word is 1): fno 1 for index 0 and 1 (a negative result stops); the info's byte `+0xC` picks the slot; fno
2 opens it; fno 4 property 0x10005 gives the signature, VID << 16 | PID. For **0x046DC298 (the Driving Force Pro)**
GT4 sets property 4 to 1; any other signature first gets 300 VBlanks of constant force -255, +255, 0. Then it
downloads the three effects (fno 7), updates and starts A, sets property 2 to 0, and starts C. The EE never checks the
VID/PID of fno 1: the supported list is the IOP's.

**Input** (`func_00555678` every VBlank, fno 6 mode 1, read only when the result is not negative and the type is 0 or
1): `+0x70` u16 type; `+0x72` s16 steering (`>> 2`); `+0x74` s16, unused; `+0x7E` and `+0x80` u16 the two pedal
axes (`>> 8`); `+0x82` u8 the hat (0-7 from up clockwise, 8 none); `+0x84` s32 the buttons, remapped per model
(C298: bits 16-29, `func_005665E8`). Which pedal axis is the accelerator is not settled by the code.

## The three effects

Built from the game's actuator packet (`func_00566AC0`, bytes b2-b10) by the setters `00555AA0`, `00555C90`,
`00555D10`, `00555E18`, `00555F68`; each copies its slot's block into an 8-entry queue that merges updates, and the
VBlank thread sends an active entry as fno 13, its handle in one fno 15 set, an inactive one as fno 9. The block, all
32-bit words except the type byte:

| Offset | A (handle at item +0x88) | B (+0xC8) | C (+0x108) |
|---|---|---|---|
| `+0x00` | type 0 | type 0x10 | type 7 |
| `+0x04` | -1 | -1 | -1 |
| `+0x08` | 0 | 0 | 0 |
| `+0x0C` | magnitude, -255..255 (`b2 * 2 - 255`) | 0 | centre offset (b4 \| b5 << 8, signed); 0 in the b3 mode |
| `+0x10` | 90 | 0 | 0 |
| `+0x14`, `+0x18` | 0 | b10, b10 | b7, b7 (0x80 at the start and in the b3 mode) |
| `+0x1C`, `+0x20` | 0 | b8, b9 | b6, b6 (0x80 at the start and in the b3 mode) |
| `+0x24..+0x3B` | 0 | 0 | 0 |

B and C are updated only for the Driving Force Pro's signature. A non-zero b3 stops B, re-centres C and starts it, and
zeroes A; while b3 is non-zero none of the three is updated.

**Live** (`GT4_FAKE_DFP=1`, below: the approved save's GT-mode race, the scripted drive into the walls, the fake
wheel's input taken from pad 1's): at boot A, B, C downloaded exactly as the table, A and C started. At the race's
start (frame 4,382) GT4 stopped C and B and started the set {A, C, B}; B was set to 32, 32, 128, 255 and C to 128 on
all four, then neither changed again. **A was updated 1,216 times** up to the race's end, its magnitude between -205
and +255 (199 values; median 55, 10th to 90th percentile -43 to 181), its direction (90) and duration (-1) fixed.
Without the wheel's input (the read failing) GT4 downloads the three and starts A and C, but sends no updates: it
drives the effects only for the controller in use.

## What it means for D7

- A is a constant force along one axis (90, the wheel's), with infinite duration: GT4's steering force, from its
  physics, a frame at a time. It maps to `SDL_HAPTIC_CONSTANT` (or a left/right force) with level magnitude / 255.
- C is a spring around a centre (type 7), coefficients and saturations 128: the centring. It maps to
  `SDL_HAPTIC_SPRING` with the centre from `+0x0C`.
- B is a damper (type 0x10), coefficients 32, saturations 128 / 255. It maps to `SDL_HAPTIC_DAMPER`.
- The types' names come from what the code and the race show; Logitech's own SDK, which would name them, isn't
  available. The duration -1 is read as infinite and 90 as the axis's direction, from the values never changing.
- The host reports the Driving Force Pro's signature (0x046DC298) whatever wheel is attached: GT4 then skips the 300
  VBlanks of full constant force (-255, +255, 0) it gives any other signature, and updates B and C, which it does
  only for that signature. The physical wheel's own limits are SDL's side.
- A's sign against the wheel (which sign turns it right) is not settled by the code. It is D7's first acceptance
  check, made on the bench before a real wheel runs a race: A's magnitude against the steering in the GT4_FAKE_DFP
  log, then a real wheel at low gain, which must pull towards the centre and never away. D7 also has a gain setting
  and caps the level.
- `GT4_FAKE_DFP=1` (debugging, `runtime/iop.cpp`) stays as D7's test bench: it reports the wheel, feeds it pad 1's
  input (the left stick X as the steering, the pressures of the buttons GT4 has accelerate and brake on as the two
  pedal axes), and logs every effect call (`lgdev:` lines: downloads and updates when the block changes, starts and
  stops, a call count every 600 frames).

RISKS R15 is answered; D7 can start.

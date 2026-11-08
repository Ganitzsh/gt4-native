# Spike R14: where GT4 keeps a car's state

Status: answered (blocks D6, A9, D8, B19, D9), 2026-11-08. Static: the decompilation and, read-only, a MIPS
disassembly of `CORE.GT4.elf` (SCUS-97328 v1.01) where functions have no C file. Live: guest-RAM snapshots of an
Arcade race (Nurburgring, the player's Ford Ka and five AI cars; `GT4_SNAPSHOT=trigger:FILE`), the player driving
into a wall and steering at full lock, the AI cars cornering at 95-133 km/h. Question: [RISKS.md R14](../RISKS.md).

## Reaching the cars

- The race object holds the physics manager (GT HD's name: `DynamicsConductor`) at `race + 0x70`
  (`RaceBasic__updatePlay`: `func_0034C190(F(p, int, 0x70), get(p, 0x310))`).
- Car `i` (0..5) is `cond + 0x6698 + i * 0x10A0` (`func_0034C190`); `cond + 0xF8A0` is the count. The class
  (GT HD: `Automobile`) is 0x10A0 bytes, no RTTI. Car 0 is the player in a single race (entrant byte below).
- Without a pointer chain, the cars are found by their back pointer: each car holds `cond` at `+4`, so the one
  address with `u32(x + 0x669C) == x` (and again at `+ 0x10A0`) is the conductor. Every snapshot gave one candidate.
- One simulation tick for every car is `func_0034DB30(cond, solitary)`: the place to read the state is after it
  returns.

Offsets below: `A` the car, `D = A + 0x104` (the dynamics block, GT HD's `_CarStatus`), `W = A + 0x164 + w * 0xEC` a
wheel (`w` 0..3: front left, front right, rear left, rear right, by the load transfer below). The physics frame is
Z-up.

## The fields

| Field | Where | Evidence |
|---|---|---|
| position | `D + 0x00` (3 floats) | code |
| orientation | rows at `D + 0x0C`, `+0x18`, `+0x24` | code (`func_0036C9F8` from the angles) |
| pitch, roll, yaw (rad) | `D + 0x568`, `+0x56C`, `+0x570` | code (integrated); which is which inferred |
| world velocity (m/s) | `D + 0x544`, `+0x548`, `+0x54C` | code |
| forward, lateral velocity (m/s) | `D + 0x53C`, `D + 0x540` | code; live (60.2 km/h = 16.72 m/s) |
| speed (m/s) | `D + 0x5C4` | code; live |
| yaw, pitch, roll rate (rad/s) | `D + 0x57C`, `+0x580`, `+0x584` | code; live (yaw rate signs follow the corners) |
| longitudinal, lateral acceleration | `D + 0x550`, `D + 0x554` | code; live (lateral below) |
| engine speed (rad/s) | `D + 0x4FC` | code; live (idle 800-960 rpm, 5,000-6,500 rpm racing) |
| gear | `D + 0x520` (u8) | code; live (= the HUD's gear); reverse and neutral numbering unknown |
| steering angle at the wheels (rad) | `D + 0x468` | code; live: 0.698 (40 degrees) at full lock left |
| entrant | `D + 0x440` (u8) | code; live (car `i` = entrant `i` in a single race) |
| tyre load (N) | `W + 0x48` | code (spring + damper + preload, 0 when airborne); live (below) |
| slip ratio | `W + 0x38` (clamped to +-1) | code; live: -0.03 at constant speed, -0.11 driving into a wall |
| slip angle (rad) | `W + 0x34` | code; live (below) |
| lateral tyre force (N) | `W + 0x68` (clamped to the grip at `W + 0x58`) | code; live (below) |
| longitudinal tyre forces (N) | `W + 0x70` (drive), `W + 0x74` | code; live: drive force on the driven axle only |
| wheel surface speed (m/s) | `W + 0x20` | code; live (= the car's speed rolling, 1.6 m/s spinning against the wall) |
| suspension stroke, its speed | `W + 0x94`, `W + 0x98` | code |
| spring, damper force | `W + 0x9C`, `W + 0xA0` | code |
| surface, contact | `W + 0x89` (u8), `W + 0xDC` (u8) | code; live (surface 2 on a kerb) |
| contact points, ground normals | `A + 0x7B8 + w * 0x34` | code |

## Live checks

- **Lateral force against the car's motion.** The four `W + 0x68` sum to the car's mass times its lateral
  acceleration `D + 0x554`, with the sign opposite (a consistent convention): the Dodge RAM (2,390 kg) -17.5 kN at
  +6.99 m/s2, mass 2,500 kg; the Honda TODAY (550 kg) -6.9 kN at +10.1, 690 kg; the Honda S500 (675 kg) +7.4 kN at
  -8.6, 860 kg; the VW Karmann Ghia (840 kg) +11.6 kN at -12.0, 970 kg. The weights in brackets are the race screen's
  (the car's spec); the masses after them are the force sum over the acceleration, the spec weight plus the driver and
  fuel GT4 adds. The physics' own mass is likely `D + 0x4D4` (the divisor of the lateral force sum in `func_00352848`,
  inferred): D6 and D8 should read that rather than the spec.
- **Load transfer.** The outside wheels carry the load and it flips with the corner: the RAM at +7.0 m/s2 reads
  8,357 / 5,176 / 7,239 / 4,411 N, at -9.9 m/s2 5,143 / 9,932 / 3,765 / 7,707 N. The four loads sum to the car's
  weight at constant speed (the Ford Ka: 10.7 kN, 1,090 kg). Cornering, they come to a few percent more (the RAM:
  25.2 and 26.5 kN against its 24.5 kN): the load is spring plus damper plus preload, so a damper still moving, a
  vertical acceleration over a bump, or downforce adds to it; which of them was not separated.
- **Slip angle** grows with the lateral acceleration (0.03 at 6 m/s2, 0.12-0.17 rad at 11-14 m/s2) and follows its
  sign. **Drive force** sits on the driven wheels: the front for the Ford Ka and the Honda TODAY, the rear for the
  rear-engined Karmann Ghia, all four for the RAM.

## Open

1. The steering rack's torque is not stored: `func_00353EE8` builds a self-aligning estimate from the front wheels'
   `W + 0x68`, `+0x34`, `+0x70`, `+0x74` into byte 2 of the feedback packet at `cond + 0xCA58 + 16 * slot`
   (`0x80 - torque * K`). To check live while cornering (D8 needs it).
2. The slip ratio's sign under braking, and the wheel spin in rad/s (`W + 0x20` over a radius array in the
   parameter block `E = *(A + 0x10)`; which of three arrays is unsettled).
3. A wheel at load 0 (an AI car hopping a kerb) still showed a lateral force (+187 and +1,703 N): `W + 0x68` may be
   the tick's force before the contact test, or the load the next tick's.
4. Gear numbering for reverse and neutral; throttle and brake in the control packet (`_AutomobileControl`, 0x20
   bytes: +4 s16 steering; +8 to +0xE s16, which is which inferred only).

## Method

Static (scratchpad, not committed): the findings and the disassembly script; the functions read are named above.
Live: three headless sessions on a copy of a test card (no game data kept), each snapshot decoded by the layout
above, then deleted. The race's rolling start drives every car at 60 km/h for about 25 s: snapshots taken earlier
show every car alike.

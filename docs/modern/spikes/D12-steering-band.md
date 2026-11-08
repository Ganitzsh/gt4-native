# D12: GT4's steering band, measured (lane D, 2026-11-08)

What the player's stick goes through after the host's response (`platform/input/response.h`) before the wheels turn,
and what the host does about it. PLAN.md D12 has the ticket; this note has the measurements behind it.

## Where the steering goes

- **The controller layer.** The stick's byte goes through the steer table `D_00622F58` (256 int16 entries, read from
  the ELF): linear, within 255 of `(v - 128) * 256`, with ±1 byte already off 0. D1's "no dead zone, linear"
  (`spikes/D1-bindings.md`) holds for this table.
- **The steer command.** The car's input struct carries a steer command at +4 (±4096), which `func_00349418` copies
  each tick to the car at `D + 0x454` (and the previous one to `+ 0x458`). The D-pad writes its command directly,
  `(f(left) - f(right)) << 4` (at `0043BDE4`). The analogue path goes through the input channels (`func_004386F0`)
  and reaches the command with a band (below). The code that applies the band was not traced.
- **The target.** `func_00348850` makes the wheels' target angle from the command: `command * lock / 4096`, plus
  two assists that act only with an aid flag set and at speed (a yaw-rate term, and a course-line term).
- **The rack.** `func_003485D0` moves the angle at `D + 0x468` towards the target with a spring, a damper and a rate
  limit. The rate limit varies with `D + 0x53C` (speed, it seems). No band.

## The band

With `GT4_VEHICLE_LOG` (the steer byte the game read, `D + 0x454`, `D + 0x468`), slow sweeps of the stick:

| | right | left |
|---|---|---|
| last byte with the command at 0 | 165 (37 off centre) | 90 (38 off centre; 89 not sampled) |
| first non-zero | 166 (-32) | 88 (97) |
| slope past the edge | about 45.6 a byte | about 45.6 a byte |
| full stick | 255: -4096 | 0: +4096 |

Both edges sit at the same table value, about 29 % of the channel's range (the table is 258 a byte on the right and
256 on the left, hence 37 and 38 bytes).

The same band in:
- an Arcade race (empty save, Ford Ka) and a GT-mode race (the owner's save, Pan Euro race 2, another car);
- at a standstill and at speed: in the GT-mode run up to 166 km/h, the command was 0 for bytes 92-164 at 98-160 km/h,
  and the edge values matched the standstill ones byte for byte (-323 at 172 at 123 km/h; 289 at 84 and 97 at 88 at
  160 km/h).

So it is a constant, before the car.

## What modern does (PLAN D12, 1120e0a, 88a2910)

- The compensation is game-side, where the band is: the host sends `input.steer.game_band` as `kOptionSteerBand`
  (game option 7, per mille; 0 in `original`), and `scePad2Read` (runtime/iop.cpp) remaps the left stick's byte
  with `pad::steer_band` (runtime/pad_steer.h): d off the centre becomes `floor(g * 127 + (1 - g) * d)` on the right
  and `floor(g * 128 + (1 - g) * d)` on the left. The first byte off the centre lands on 165 / 91 at 29 % (the
  band's last zero byte on the right: no overshoot into a step; one or two bytes of residual band on the left),
  every byte past it is reached, and full stick stays 255 and 0.
- Only while a race's physics runs (`vehicle_live()`: some car has weight on a wheel). The conductor alone is no
  sign of a race: GT4 keeps one with its six cars through the menus from the boot on (VBlank 276 in the GT-mode run),
  every wheel's load 0. So menus outside a race read the stick as they always did; a race's own pause menu and its
  replays see the remapped byte (at most 37 bytes further out than the stick).
- The host's response (dead zones, curve, smoothing, the keys' ramp) is unchanged and applies everywhere; its dead
  zone is back at 5 % (it was 0 in the interim, 1499576, while the band was not compensated).
- Cost: the conductor is found from the boot on, so a pad read in a menu checks its back pointers and its cars'
  wheel loads, no scan. A race is compensated from its first frame with weight on the wheels (the grid, a rolling
  start), before the player takes control.
## GT4's own lock reduction with speed

At about 120 km/h the wheel angle per unit of command was roughly an eighth of the standstill one. This is indicative
only: the run's full-stick pulses came after the car had stopped, so no full-lock angle at speed was measured. A clean
measurement needs full-stick pulses on a straight at speed.

Decision (coordinator, 2026-11-08): GT4 already cuts lock strongly with speed, so the host adds no speed sensitivity
by default. `input.steer.speed_reduction`, if it is added, is an option, off in every preset, and only after that
clean measurement.

## Open

- **A Driving Force Pro.** Whether a wheel's input goes through the same analogue path (and so the same band) is not
  known: the code that applies the band was not traced. D15 checks it empirically with the same log column
  (`D + 0x454`) when the host first presents a wheel to the game.

## Tools

`GT4_VEHICLE_LOG=PATH` (runtime/vehicle.cpp): per VBlank and car, speed, yaw rate, the wheels' load and slips, pad 1's
steer byte, `D + 0x468`, `D + 0x454` and `D + 0x456`. The runs used live sessions with the virtual pad; their logs and
any save copies stay in the clone's `build/`.

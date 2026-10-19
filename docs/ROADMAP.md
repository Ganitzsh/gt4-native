# Roadmap

GT4 Native is a native port: the product contains neither emulated PS2 hardware nor translated game code. This page is the public
statement of the goal and of how each step is accepted. Dates are not promised; each milestone is closed by measured evidence.

| Milestone | Goal | Accepted when |
|---|---|---|
| G6 | Game code runs natively (from the matched decompilation), without any translated code | The reference sessions (boot, menus, a race) complete with no translated code; a function that is still missing stops the game with a log of its address |
| R1 | Native renderer | The boundary between game code and the PS2 graphics commands (GIF/VIF packets, VU1 programs) is mapped, real traffic is captured as test data, effects are implemented one by one. A native frame matches the reference frame within a measured tolerance |
| A1 | Native sound | The calls to the I/O processor and the sound chip are mapped and captured; mixing and effects are implemented. The waveform matches the reference within a measured tolerance |
| P1 | Native platform services | Disc, controller, memory card, threads and timers use operating-system services; no emulated kernel |

## How correctness is checked

A reference run produces a **recording** of the game at several layers (state hashes, calls and cycles, graphics and sound commands,
frames and audio, scene data). The same deterministic scripted sessions run on the native build and every recorded point is compared
automatically, with tolerances that are measured and validated (a comparator must flag a known injected difference before it is trusted).
Each comparison ends as PASS, FAIL or DOUBT; a person signs off on passes and looks at every doubt. Game data is never committed: only
hashes, sizes, counts and ids appear in documents.

## Where things stand

See the earlier bootstrap notes under `docs/history/` (MILESTONE_1 to MILESTONE_4) and `docs/DIFFERENTIAL_TESTING.md`. The decompilation it builds on
reproduces the PS2 executable byte for byte and is far along; this repository starts from that.

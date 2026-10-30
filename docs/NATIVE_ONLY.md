# No PS2 code at run time

Goal (2026-10-29): the game runs as C++ only. No interpreter of PS2 code (VU0 or VU1 microprograms), no
emulated EE instructions (VU0 macro mode, MMI, 128-bit registers), no emulated hardware. The original
MIPS code stays available as a test oracle (`tools/eeref`, docs/DIFFERENTIAL_TESTING.md), never in the game.

Each step replaces a PS2 path with C++ and proves the C++ against the old path before removing it: bit-exact
where the old path is deterministic (VU programs against the interpreters), otherwise against the original
code with eeref.

## What is left (2026-10-29)

| Layer | What runs PS2 semantics | Size |
|---|---|---|
| 1. VU0 microprograms | done 2026-10-30: no VU0 model in the game; what each program did is plain float C++ in its callers (layer 1 below) | 14 entry points in one 0xA80-byte image, called from 19 functions |
| 2. VU1 fallback | done 2026-10-29: no interpreter in the host (`GT4_VU1_INTERPRETER` OFF by default, a development option); the clipper's record overflow wraps natively | the C++ programs cover the 1,590 instructions GT4's image runs |
| 3. PS2 instructions in game code | done 2026-10-30: no VU0 macro, MMI or system instruction is left in the linked game code; every such function has a plain C++ copy in `game_src/` (the state the original kept in VU0 as plain globals, `runtime/shared_state.h`, `gt4_vec.h`; critical sections, $gp and timers through `gt4_interrupts_*`, `gt4_get_gp`, `gt4_cycle_count`), or a runtime replacement | 281 sources keep single FPU instructions (`sqrt.s`, `min.s`, `max.s`, `rsqrt.s`, `abs.s`), which game_cc writes as C expressions |
| 4. Provisional drafts | C++ that compiles to nearly the original, not proven | 525 functions (the eeref audit of the ones a GT mode session runs: all agree or understood) |
| 5. Hardware data paths | the game builds DMA chains, VIF and GIF packets; `runtime/gs.cpp` walks them, `gt4_host` decodes VIF1, runs the VU1 programs and renders GS primitives (texture formats, CLUTs, blending, Z as the GS defines them). Since 2026-10-30 the native renderer is the only one: paraLLEl-GS (a GS emulator) is out of gt4_host, a test reference behind `GT4_GS_REFERENCE`; the native front end raises SIGNAL/FINISH/LABEL and serves GS -> host reads, drawn pixels from the GPU (the car thumbnails GT4 renders and keeps) | the graphics library (pgl, 0x494578..) and the host's GS command layer (`platform/gsnative`, `platform/gs/vif1.*`) |
| 6. EE platform | the EE kernel (threads, semaphores, interrupts, VBlank), SIF and IOP services as C++ implementations of the PS2 APIs; guest RAM identity-mapped at PS2 addresses; the gcc 2.9 ABI | runtime/ |

Order: 1 and 2 first (bounded, mechanical to verify), then 3 (function by function, eeref-verified), 4
alongside (matching or proving drafts), then 5 (the game's graphics library drawing through a native
renderer interface instead of GS packets), then 6.

## Layer 3: PS2 instructions in game code (done)

State 2026-10-30: every linked function with such asm rewritten in `game_src/` (VU0 macro code, MMI packing and saturation, PLZCW, EE
FPU library code, pref/sync/cache, ei/di/Status critical sections, $gp), each checked with `GT4_DIFFTEST`
(`tools/eeref/difftest_*.cpp`, 109 tests since 2026-10-30, 300 random cases each) and the ones a race reaches by recorded calls
replayed on the original code (`tools/eeref/replay.py`, `known.txt` for the understood differences).

2026-10-30, no VU0 model left at run time: the register file `__gt4_vu0`, `vcallms` and the matrix stack in VU0
data memory are gone from the game.

- The state functions share is plain data (`runtime/shared_state.h`, declared for game code in `gt4_vec.h`):
  `gt4_shared_vectors[32][4]` (the original's vf0..vf31; `gt4_vec(n)`), `gt4_matrix_stack[256][4]` with its top
  `gt4_matrix_stack_top` (the original's stack in VU0 data memory at vi13, in rows; the scratchpad's three matrix
  sets keep their own tops, 004A5348), `gt4_random_state` (the R register of 004AC400 / 004AC41C). ACC, Q, I, the
  flags and the other integer registers were temporaries or unused and have no counterpart.
- The microprograms still called became plain functions: the sine (0x58) and the rotations about x, y, z (0x78,
  0xB8, 0xF8) in `runtime/native/unit_00494578.cpp` (`sine`, `rotate_rows`), the 2D projection (0x678) as
  `gt4_project_2d` / `gt4_colour_2d` in `gt4_vec.h` for 0049A200, 0049A360, 0049A4F8, 0049A6A0; push and pop
  (0x900, 0x930) are 004A53F8 / 004A5400 on `gt4_matrix_stack`. They write only what callers read (the sine leaves
  sin and cos in vector 1 for 004A5EB0, 004A5ED8, 004A5F00, 004A75D8, 0049F658).
- Nothing else touches VU0 memory: the game's only VIF0 transfer is the micro image upload (004A12B8, MPG), which
  `runtime/gs.cpp` hands to the host, which ignores it.
- 22 linked functions still went through the asm translator's VU0 paths (20 sources the decomp moved back to
  assembly, which game_cc recovers from its history, and RaceCarModel__virtual_37 / _41); they have `game_src/`
  versions now: 0048D3E0, 0048D408, 0048D430, 0048D4C0, 0048D530, 0048D680, 0048D6C0, 0048D700, 0048DF98, 0048DFD8,
  0048E018, 0048EB10, 00499F18, 0049E228, 0049E430, 0049E548, 0049EB90, 0049EE00, 0049F658, 003E33C0, 00390660,
  003908C8. The translator refuses VU0 instructions now (`vnop`/`vwait*` excepted), so a source with VU0 code no
  longer compiles natively until it has a rewrite.
- The test tools keep a VU0 model of their own: `tools/eeref/vu0_programs.cpp` (the 14 programs in the VU's
  arithmetic, which the GT4_ORIG bridge runs for the original code's `vcallms`; its data memory is
  `gt4_matrix_stack`), `vu0_interp.cpp` and `vu0_state.h`. The bridge copies the shared vectors, the stack's top and
  the random state to and from VU0's vf, vi13 and R at each crossing; GT4_RECORD files keep the old layout (the
  shared state written as VU0 registers and data memory), so replay.py and eeref read them as before.

The history of this layer:

The game keeps state in VU0's registers across functions, so these functions are rewritten in groups that share a
register, and the register becomes a named piece of C++ state once all of its users are rewritten.
`tools/vu0_state_map.py --decomp ...` lists, for every function of the native build whose asm uses VU0, the
registers it reads before writing (state another function left) and the ones it writes, following vcallms into
the micro image, and then each state register's readers and writers (2026-10-29, 72 functions):

- vf28..vf31: the current matrix of the graphics library (its push/pop programs 0x900/0x930 keep a stack of them in
  VU0 data memory at vi13; the rotations 0x78/0xB8/0xF8 turn it); some ten writers, twenty readers;
- vf20..vf23 and vf24..vf27: view and projection matrices, set by the camera code (003Exxxx) and the graphics
  library, read by the drawing functions and the screen projection programs (0x678, 0x700);
- vf8..vf19: the four bone matrices (0049D970) the skinning programs read, and the projection constants of the text
  and sprite paths;
- vi18 (CLIP flags) is read only after the same function's vclip: no state.

The functions with MMI or FPU asm and no VU0 state (byte and halfword packing, compares, saturating arithmetic)
need no group and go first. (FPU asm is not counted: game_cc and asm_translate make it plain float arithmetic.)

Baseline before rewriting (2026-10-29): a B-spec Sunday Cup race recorded every layer-3 function it reaches, two
calls each, and replayed them on the original code: 65 agree, 4 within float drift, 11 understood (known.txt),
2 wait on another thread. Rewritten so far, each checked with `GT4_DIFFTEST` (200 random images each):
001C1850, 001C18B0, 001C1C50, 001C1CA8 (the movie player's byte masks, XOR and negation).

## Layer 2: VU1 (done)

GT4's VU1 programs were already C++ (`platform/gs/vu1_native.cpp`, docs/VU1_PROGRAMS.md); the interpreter
remained as a fallback for one case, a clipped polygon whose records run past VU memory, which the C++ now
wraps as the hardware does (random geometry reaches it, the game's has not). The interpreters, the diff tools
and the tests that use them build only with the CMake option `GT4_VU1_INTERPRETER` (OFF by default); the host
binary has none of their code. GT mode from boot through the menus to the garage: 335,142 VU1 programs, all native,
identical to the reference.

## Layer 1: VU0 microprograms (done)

2026-10-30: the game runs none of this any more; its callers do what they need in plain float arithmetic (layer 3).
What follows is the test reference, `tools/eeref/vu0_programs.cpp`.

The 14 programs in C++, operation for operation in the interpreter's
arithmetic (vu_float on operands and results, MAX/MINI on the bits, FTOI toward zero, Q at WAITQ), every register
they write included. Checked against the interpreter, which is now a test tool (`tools/eeref/vu0_interp.cpp`):
bit for bit on every call of two GT mode sessions (to GT Auto, and through the menus to the garage; the ten
programs those reach), and on
20,000 random states per program, edge values of the number format included (`eeref --vu0-fuzz 20000 --elf
...`; one changed bit in a constant makes every state of the programs using it differ). `tools/vu0_disasm.py`
prints the micro image from the user's executable. A vcallms to any other address stops the bridged call.

| Entry | Called from (in the original) |
|---|---|
| 0x58 | 004A7438 |
| 0x78, 0xB8, 0xF8 | 004A7988, 004A79B0, 004A79D8 (hand-written VU0 helper library) |
| 0x2E8, 0x308, 0x460, 0x478 | 0049DCB0, 0049D728, 0049DB08, 0049D580 (skinned vertex batches) |
| 0x548 | 004AC118 |
| 0x678 | 0049A200, 0049A360, 0049A4F8, 0049A6A0 |
| 0x700 | 00499948, 00499D00 |
| 0x858 | 0049E480 |
| 0x900, 0x930 | 004A53F8, 004A5400 |

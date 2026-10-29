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
| 1. VU0 microprograms | done 2026-10-29: C++ (`runtime/native/vu0_programs.cpp`), no interpreter in the game | 14 entry points in one 0xA80-byte image, called from 19 functions |
| 2. VU1 fallback | done 2026-10-29: no interpreter in the host (`GT4_VU1_INTERPRETER` OFF by default, a development option); the clipper's record overflow wraps natively | the C++ programs cover the 1,590 instructions GT4's image runs |
| 3. PS2 instructions in game code | `tools/game_toolchain/asm_translate.py` turns VU0 macro, MMI and multi-instruction asm into C over an emulated register file (`compat/game/gt4_asm.h`) | 157 sources: 55 matched, 6 game_src copies, 96 provisional drafts (single FPU instructions with a C meaning, `sqrt.s`, `min.s`..., are plain C already) |
| 4. Provisional drafts | C++ that compiles to nearly the original, not proven | 525 functions (the eeref audit of the ones a GT mode session runs: all agree or understood) |
| 5. Hardware data paths | the game builds DMA chains, VIF and GIF packets; `runtime/gs.cpp` walks them, `gt4_host` decodes VIF1, runs the VU1 programs and renders GS primitives (texture formats, CLUTs, blending, Z as the GS defines them) | the graphics library (pgl, 0x494578..) and the host's GS layer |
| 6. EE platform | the EE kernel (threads, semaphores, interrupts, VBlank), SIF and IOP services as C++ implementations of the PS2 APIs; guest RAM identity-mapped at PS2 addresses; the gcc 2.9 ABI | runtime/ |

Order: 1 and 2 first (bounded, mechanical to verify), then 3 (function by function, eeref-verified), 4
alongside (matching or proving drafts), then 5 (the game's graphics library drawing through a native
renderer interface instead of GS packets), then 6.

## Layer 2: VU1 (done)

GT4's VU1 programs were already C++ (`platform/gs/vu1_native.cpp`, docs/VU1_PROGRAMS.md); the interpreter
remained as a fallback for one case, a clipped polygon whose records run past VU memory, which the C++ now
wraps as the hardware does (random geometry reaches it, the game's has not). The interpreters, the diff tools
and the tests that use them build only with the CMake option `GT4_VU1_INTERPRETER` (OFF by default); the host
binary has none of their code. A race: 335,142 VU1 programs, all native, identical to the reference.

## Layer 1: VU0 microprograms (done)

`runtime/native/vu0_programs.cpp` has the 14 programs in C++, operation for operation in the interpreter's
arithmetic (vu_float on operands and results, MAX/MINI on the bits, FTOI toward zero, Q at WAITQ), every register
they write included. Checked against the interpreter, which is now a test tool (`tools/eeref/vu0_interp.cpp`):
bit for bit on every call of a GT mode session to GT Auto and of a race (the ten programs those reach), and on
20,000 random states per program, edge values of the number format included (`eeref --vu0-fuzz 20000 --elf
...`; one changed bit in a constant makes every state of the programs using it differ). `tools/vu0_disasm.py`
prints the micro image from the user's executable. A vcallms to any other address stops the game.

| Entry | Called from |
|---|---|
| 0x58 | 004A7438 |
| 0x78, 0xB8, 0xF8 | 004A7988, 004A79B0, 004A79D8 (hand-written VU0 helper library) |
| 0x2E8, 0x308, 0x460, 0x478 | 0049DCB0, 0049D728, 0049DB08, 0049D580 (skinned vertex batches) |
| 0x548 | 004AC118 |
| 0x678 | 0049A200, 0049A360, 0049A4F8, 0049A6A0 |
| 0x700 | 00499948, 00499D00 |
| 0x858 | 0049E480 |
| 0x900, 0x930 | 004A53F8, 004A5400 |

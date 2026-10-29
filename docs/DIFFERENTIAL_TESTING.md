# Differential testing of the decompiled code

`gt4-decomp` proves that its sources reproduce the PS2 executable byte for byte. That does not
prove they behave the same when compiled for a PC: a pointer is 4 bytes on the PS2 and 8 on x86-64,
`long` is 64-bit on the EE, the FPU has no denormals or NaNs, and so on. This harness checks the
behaviour directly, one function at a time.

## What it does

`tools/diff_harness/diff_test.py` takes a function of a `gt4-decomp` checkout and

1. runs the **original** MIPS bytes from the game image in a MIPS emulator (Unicorn);
2. compiles the **decomp source** for a 32-bit host and runs it natively, with guest memory mapped at
   the same addresses, so raw pointers in the sources mean what they meant on the PS2;
3. feeds both the same random trials (four arguments, some pointing into a shared random memory
   block, memory words chosen to look like small integers, pointers or floats);
4. requires the same return value and the same memory afterwards.

A fault is a result: if both sides fault on a trial it counts as agreement, if one does it counts
as a difference.

Host conventions (see `compat/host32/types.h` and `tools/compile_census.py --guest32`):
32-bit process, the EE `long` written as `long long`, `-malign-double` (the EE aligns 64-bit
members to 8), 128-bit registers as a two-word struct, SSE arithmetic with denormals flushed (the PS2
FPU has none).

## Running it

Needs Linux (WSL), `gcc-multilib`, the decomp's Python venv (rabbitizer) with `unicorn` installed,
and the decomp's `orig/` image.

```sh
python tools/diff_harness/diff_test.py --decomp /path/to/gt4-decomp 005AEFA8 005AEFE8
python tools/diff_harness/diff_test.py --decomp /path/to/gt4-decomp all --sample 400 --trials 100
python tools/compile_census.py --decomp /path/to/gt4-decomp --guest32   # does it even compile?
```

## First results (2026-10-17)

Compile census of all 29,438 sources: 96.9% compile for the 32-bit host, with 2 files (not 2,032 as in
a 64-bit build) casting between pointers and integers of another width.

Differential run on a random sample of 400 matched functions:

| Result | Functions |
|---|---|
| agree with the original | 124 |
| differ | 2 (both known harness gaps, see below) |
| no usable trial (every trial faults on both sides) | 7 |
| original uses an R5900 instruction the emulator lacks | 4 |
| not run: calls another function or a global | 230 |
| not run: does not compile / link, or returns 64 bits | 33 |

Of the 126 functions with a verdict, 124 agree. The mismatches seen along the way were all
harness gaps or host differences, none a logic bug in a decomp source: denormal floats, `movz`
missing from the default Unicorn MIPS CPU, 64-bit members aligned to 4 on a 32-bit host (fixed
with `-malign-double`), NaN payloads.

## What it found in the decomp itself (2026-10-18)

Triage of the first 16 mismatches on the 400-function sample: 8 were real problems in the decomp
sources, 5 harness gaps, 2 float edge cases, 1 harmless.

The real ones share one cause: a wrapper or tail call that drops the arguments the register ABI passed
through. On the PS2 a caller's `a0`/`a1`/`f12` reach the callee untouched, so a source such as
`void *f(void *self) { g(); }` generates the same code as `g(self)`, but on x86 `g` reads garbage.
The sources match byte for byte and are wrong as C. Twelve were fixed in gt4-decomp (commit
`037c64b3`), each re-judged (`match.py check` still prints MATCH) and re-tested (AGREE). One
function, `00398968`, is declared `s32` and has no `return`; declaring it `void` costs two
instructions, so it stays as it is. Its only caller ignores the value.

Float results are compared within 4 ulp and two NaNs count as equal: the PS2 FPU rounds toward zero,
flushes denormals and has no NaN, an x86 does none of that.

A full run over every source is the worklist for the same kind of fix; its output is not committed.

## Known gaps

- Callees are linked from the decomp, the whole game image is mapped, and `.rodata` and jump tables
  work. What is still skipped: functions that call something without a decomp source, a library
  group source (expat and others, whose functions are `static` in a multi-function file), and
  sources that do not compile for a 32-bit host (about 4%).
- 64-bit arguments and returns (`__ashldi3`) need a 64-bit argument kind.
- A pointer or vtable in data points at original-address code the native process cannot run.
- The emulator is a generic MIPS64 CPU, not an R5900: MMI, VU0 and the three-operand `mult` are
  unsupported, so those functions report `ORACLE-UNSUPPORTED`.
- The 32-bit host is a test device. The port itself still needs the guest pointer model
  (`include/gt4/guest_abi.hpp`) for a 64-bit build.

# The native game against the original code, in a race (2026-10-28)

The harness above runs one decomp source on random inputs. The tools below test the *linked native game*
against the original machine code on real game state: any function, mid-race, with all of its callees.

## The reference: the original code, translated

`tools/eeref/build.cmd C:\path\to\gt4-decomp` translates the user's `CORE.GT4.elf` to C++ with `eerecomp`
(ps2-web-recomp, MIT, vendored unmodified in `third_party/ps2-web-recomp`). The translation is instruction by
instruction and keeps the R5900's rules: 128-bit registers, MMI, VU0 macro instructions, and the EE FPU (no
infinities or denormals, overflow clamped). Every function start of the decomp's `build/functions.csv` is an
entry (`entries.py`), so code reached only through pointers is translated too; `hook_entries.py` adds a hook at
each function's first instruction (used by the bridge below). The output, `build/eeref/gen` and `genh`, is
the user's game and stays in `build/`.

## Record and replay

- `GT4_RECORD=ADDR[,ADDR...][@VBLANK][xCOUNT]` (runtime/record.cpp) records the first COUNT calls of each
  function from VBlank VBLANK into `GT4_RECORD_DIR`: at entry the arguments, VU0, the scratchpad, VU memory,
  the 32 MB of guest RAM and the caller's 64 KB of native stack (arguments point at its locals); at return the
  same (RAM as the 4 KB pages that changed) and eax/edx. An int3 on the native function's `push %ebp`, which
  the handler performs; interrupts are held from entry to return. With `GT4_RECORD_ROOT=ADDR`, the functions
  are recorded only inside the first recorded call of ADDR, and their breakpoints are armed only then (a
  breakpoint hit every frame in the menus slows them off the pad script).
- `tools/eeref/replay.py --decomp ... DIR` runs each recorded call on the original code (`eeref.exe`) from the
  entry state and compares the return and every changed word of memory: changed alike, within float rounding
  (1e-5 relative: the EE truncates, the x87 rounds), or different. Arguments are mapped from the native stack to
  the EE's registers with the kinds of the function's definition (abi_lint's parser): a float that goes to an
  integer register makes the original compute garbage. Arguments past $t3 or $f19 go to the caller's 8-byte
  stack slots from 0($sp) (EABI).
- Many functions at once: `GT4_RECORD_DELTA=1` writes the whole RAM once (`base.ram`) and each entry as the
  pages that differ from the entry recorded before it, in `order.txt` order (an audit of ~400 calls: 150 MB
  instead of 13 GB); replay.py rebuilds each entry by walking the chain. The changed pages are copied in one
  pass: the game's other threads keep writing while a call is recorded, and a count taken apart from the
  copy could disagree with it (replay.py reports a file whose length disagrees with its header as `CORRUPT`).
- What the reference models: the kernel calls whose effects are outside EE RAM (thread id and priority,
  wake-up, semaphores, cache flushes, interrupt masks, SIF DMA status) return what an uncontended kernel
  would; VU0 microprograms (`vcallms`) run on the micro-mode interpreter (`tools/eeref/vu0_interp.cpp`, the
  reference the game's C++ programs were checked against; `eeref --vu0-fuzz N --elf ...` compares the two on N
  random states per program); the scratchpad
  and VU memory are mapped. A call still running after 2^28 instructions stops as `spinning` (it waits on a
  hardware register or another thread); other syscalls and EE hardware registers stop it or read 0.
- Verdicts: `agree`; `close` (the only differences are within 1e-3 relative or 1e-5 absolute: long float
  computations drift that far); `known` (an understood difference listed in `tools/eeref/known.txt`);
  `DIFFER`; `STOPPED` (with where and why); `CORRUPT`. An audit of the native-only code (every provisional
  draft and game_src copy, two calls each, boot to GT mode's GT Auto, 2026-10-29): 265 agree, 1 close, 28
  known, 109 stopped (107 waiting on hardware), 0 differ, after fixing the one real divergence it found
  (00360B60 passed an int and a pointer before the floats 00360980 takes: the native callee read them as
  its floats).
- `tools/eeref/calltree.py --elf ... --functions ... ADDR --depth N --min-size B` lists a function's static
  callees (jal and tail calls) for a GT4_RECORD list. `GT4_PROFILE=FILE:FROM:TO` (runtime/profile.cpp) samples
  the game's threads (frame-pointer walk, game code only) to find a scene's call tree in the first place.
- `GT4_SNAPSHOT=VBLANK,...` writes the guest RAM at those VBlanks, to compare game state between two runs.
  `GT4_SNAPSHOT=trigger:FILE` writes the RAM and the scratchpad whenever FILE appears (screens reached by hand).
- `GT4_WATCH_WRITE=ADDR[,ADDR...][@FILE]` (runtime/crash.cpp): hardware write watchpoints, up to four, on every thread
  (re-scanned as threads start), armed when FILE appears; each write is logged with the writer and two callers.
  `GT4_WATCH_ODD=1` logs only values no computed float has (an address or an integer stored as a float).
  `gt4_debug_watch(addr)` (runtime/watch.cpp) arms one from game-side code for a heap object.
- `GT4_FPU_TRAP=file:FILE` arms the x87 trap (`GT4_FPU_TRAP`) only once FILE appears, so a screen is reached at
  full speed first.

## The original code inside the game

`game_link.py --eeorig build/eeref/genh` links the translated code into gt4_game (`tools/eeref/orig_bridge.cpp`;
its RAM and scratchpad are the guest's, at their PS2 addresses):

- `GT4_ORIG=ADDR[=KINDS[:RET]],...` runs the original code for those functions and everything under them;
- `GT4_ORIG=all` sends every PS2 function address to the original code: all game logic is the original's,
  and only the runtime's hardware and host layer (DMA starts, GS, IOP services, syscalls, libc helpers) is
  native. `GT4_ORIG_KINDS=build/eeref/kinds.txt` (`kinds.py`) gives every function's parameter kinds.
  `GT4_ORIG_KEEP=ADDR|LO-HI,...` keeps those functions (or address ranges) native under `all` (a test hook such
  as the GT4_MONEY floor, 0043AF68; ranges bisect a difference). A bisection that needs several separate ranges
  original at once points at data the native code leaves unwritten (GT Auto's lights: stack garbage), not at
  one function.

Calls from the original code to an address whose trampoline leads into the runtime are made natively, with
the EE's eight integer argument registers and four stack slots; setjmp, longjmp and the runtime's stand-ins
for unmatched game functions stay translated (a native longjmp on an EE jmp_buf loads EE values into x86
registers). VU0 microprograms ran on the native interpreter (the game has C++ programs now). The translated code runs at about 35 frames per
second in a race. `GT4_ORIG_TRACE` logs each native call.

## What it found

- **The physics step (0034DB30) and the AI.** Recorded mid-race with its whole call tree (86 functions, among
  them the provisional drafts on the AI's path: 00349FD0 driver input, 00465078 AI driver, 00463EA8 speed
  planning), every callee agrees with the original within float rounding. The two words 003F7EA8 leaves
  that the original clears are denormals (the EE flushes them to zero). 00465078's outputs differ by up to a
  few percent on values of 1e-4: rounding, through its branches.
- **No pile-up at New York's turn 1.** A first reading of the snapshots took the car matrix's first row for the
  up axis and saw cars "pitched 30-70 degrees" there, the same with all game code as original code. The car
  state (conductor + 0x6698 + i * 0x10A0 + 0x104) is position x, y, z (z up) and then the rows forward, side
  and up: those angles were headings through the hairpin. Read correctly, every car stays within 7 degrees of
  level while it turns about 190 degrees (VBlanks 6100-6900), and the `GT4_ORIG=all` run's headings match the
  native run's up to the corner.
- With `GT4_ORIG=all` the menus, the HUD and the race run, but the race's 3D scene is not drawn (sky only): a
  difference between the two worlds in the renderer's path, not investigated yet.
- **The AI's catch-up** (rubber banding) is DynamicsConductor::ControlEnemySpeed (00365790), called by the
  physics step for pairs of cars along the running order: it sets each car's time scale (state +0x444; the car's
  physics step is that many 60ths of a second, 003658A0) from the event's slow car boost and fast enemy restrain
  parameters (conductor +0xF818). Its native copy (game_src/func_00365790.cpp, the `gameplay.ai_catchup` option)
  was checked by forcing the parameters on in a B-spec race (`GT4_POKE` at VBlank 4600: 20% over 10..100 m) and
  replaying six recorded calls on the original: all agree (two within float rounding). Original slowed the
  leader ahead of the player to 0.79 of real time; reduced to 0.86; off kept every car at 1.0.

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

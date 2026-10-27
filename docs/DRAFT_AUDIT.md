# Draft audit (2026-10-27)

The native game links a provisional *draft* for every function the decomp has not matched yet
(`tools/draft_natives.py`, `build/game/draft_natives.txt`): C or C++ that compiled to nearly the original
instructions. Nearly is not the same behaviour. GT mode's world map came out as giant translucent polygons
because the draft of the pgl texture upload chain (004A4F48) wrote one DMA tag word into the wrong 16-byte
slot, so the audit below checked every draft whose difference class is not `registers` or `order`.

## How

- **Which drafts run.** `GT4_TRACE_HITS=FILE` (runtime/trace.cpp) appends the PS2 address of each function
  compiled with `GT4_CC_EXTRA=-finstrument-functions` the first time it runs. With the drafts compiled that
  way, an Arcade race and a GT mode session reached 115 drafts; three ran only in GT mode, among them
  004A4F48.
- **The audit.** Each draft against `tools/match.py asm` and `match.py check` (in gt4-decomp), by agents
  working from one brief (`build/hits/AUDIT_BRIEF.md`): 95 drafts the runs reached, then the 332 others whose
  class is `other`, `worker-best`, `length` or `branch-form`. Verdicts are in `build/hits/*_result.txt`
  (not versioned).
- The decomp's differential harness (`tools/diff_harness`, Unicorn) skipped nearly all of them: they call
  functions without a decomp source or touch the scratchpad.

## Results

427 drafts: 412 behave like the original, 15 did not (fixed in `game_src/`):

| Function | What the draft got wrong |
|---|---|
| 004A4F48 | pgl texture upload chain: the REF tag's QWC/ID word written into the GIF tag's slot (GT mode's map) |
| 003F28B8 | assist output, mode 6: state 0's transitions under case 1, state 0 never left |
| 0022BC20 | a handle released before the loop that still uses it |
| 00458340 | grid row/column counts read as 32-bit words at 0x10/0x14 instead of halves at 0x10/0x12 |
| 004C7B38 | a character-class switch: case 0 merged with default (one extra index increment) |
| 004C2048 | a list head dereferenced before the null test |
| 004AA6B8 | a scratchpad list entry's state cleared after `return` (dead code) |
| 0057F2F8, 0057FAF8 | soft-float double to 64-bit / unsigned 32-bit integer: unrelated computations, rewritten |
| 0057F8B8 | soft-float double multiply: a shift by a negative count (undefined in C) |
| 00334158, 003D51B0, 003D6D30, 003E3ED0, 004961B8, 005186A8 | float or double constants one ulp off |

Left as they are, by decision:

- **004AC034** (64-byte copy, hand-written in the original): the original has a typo, `sdl $t1, 0xA7($a0)` at
  0x4AC0D8 where 0x17 was meant, so with an unaligned destination it writes 0xA0 bytes past it and leaves
  bytes 0x10..0x17 unwritten. The draft is the intended copy; the port keeps it.
- **003CD188**: tests a `bool` argument `== 1` where the original tests non-zero; the same for 0 and 1.

## Float constants everywhere

The one-ulp constants are not a draft problem. ee-gcc converts decimal floating constants toward zero
(0.1f is 0x3DCCCCCC, 1.02 is 0x3FF051EB851EB851); the native gcc 2.9 for x86 rounds to nearest (0x3DCCCCCD).
Every inexact constant of every matched source was one ulp off natively. `tools/game_cc.py` now rewrites
each decimal constant to the decimal that rounds, to nearest, to the EE's value (outside comments and
literals; hexadecimal constants were already exact).

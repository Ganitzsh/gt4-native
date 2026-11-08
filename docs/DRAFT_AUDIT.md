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
| 004C7B38 | a character-class switch: case 0 merged with default (one extra index increment; a stack overrun, below) |
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

## Re-check, 2026-11-08 (decomp 6dbbf48f4)

- **Which drafts run.** Until 25d737c, `GT4_TRACE_HITS` logged most game functions as 00000000: they are
  called through their PS2 trampolines, and the address `-finstrument-functions` gives is the PS2 one, which
  the host-code lookup does not know. On lane A's race route (`docs/modern/routes/race_gt_pe2.txt`) 56 of the
  336 drafts run where the old trace showed 31, so the 115 drafts reached above are likely an undercount.
- **004C7B38 was a stack overrun.** On an empty text run the old draft's start index is 1, so 004C7478 copies
  255 characters into a 48-entry stack buffer and calls on with a corrupted owner. `GT4_DIFFTEST=004C7B38`
  (`tools/eeref/difftest_text.cpp`): the game_src copy agrees on 200 trials; the old draft crashes on trial 1.
- **The route's drafts, replayed.** 55 of the 56 recorded (three calls each, `GT4_RECORD_DELTA`) and replayed
  on the original. 0026F0A8 is not checked: armed, but it first runs at the route's very end (line 8,952 of the
  trace's 9,035) and the recording run did not reach it. Of the 55: 36 agree on every call, 2 agree where they
  do not stop, 9 stop on syscalls (they wait on other threads: untestable in eeref), 2 are `known`. 6 differ
  for reasons outside the draft: three replays follow host pointers left in guest RAM and derail; 00316EA8 and
  002F4A10 store, where the original stores PS2 addresses (`00317BF0`; `0068FD50`, `00697678`, `0069ECC8`,
  `0069D9C8` in its data), addresses in the native image's `.text` above the guest section (`gt4fn_00317BF0`'s
  entry; `0235CF0C`, `0235C004` and code inside `gt4fn_005D52C0` and `gt4fn_005D0B10`); 00214D28's differing
  words are all in the block its matched callee 0055FB40 fills. No new draft bug.
- **Host addresses in guest RAM.** Native code uses those values as pointers into its own `.text`, just above the
  guest RAM (0x02000000); original code (eeref, `GT4_ORIG`) reads them as PS2 pointers and cannot follow them
  (`0235CF0C`, where the three replays above derail). For R17: guest RAM beyond 32 MB would collide with the native
  image there. Not a draft bug.
- **Can they reach a save?** Not in any way that matters to a load: saves round-trip with PCSX2. Four of the five
  values are the first word (the vptr) of RTTI type_info nodes that gcc 2.96's `__tf` functions build at run time
  (`RefPointer__tf` fills `D_006D5E20`, `HObject__tf` fills `D_0088DA60`), so natively they point at the native C++
  runtime's type_info code; the fifth is `adhoc::GetMicroSecond` in the ADHOC engine's native binding table. All are
  rebuilt at every boot and read only by `__dynamic_cast` and the script engine (the decomp's
  `GT4MC__FileGT4GameData__loadInstance`, 0x005C0FC8, takes `__dynamic_cast`'s arguments and sits among the C++
  runtime's helpers: a name from another game's map). Game objects carry PS2 vptrs natively: all 166 gcc 2.x vtable
  references (`_vt$...`) are PROVIDEd at their PS2 addresses in the game's link map (lane A, a release game at decomp
  6dbbf48f4), none defined by a native object; host vptrs come only with classes of the native C++ runtime. The
  approved save loads and plays alike across builds whose host layouts differ. Tested 2026-11-08: a save the native
  game wrote (lane D, from an empty card) put on a PCSX2 `.ps2` card with mymcplus loads in PCSX2's GT4 with the same
  career as natively (L's Home: Days 1, 10,000 Cr, 2 APR, 0.0 %); saved again there (PCSX2 rewrote the game data and
  garage), exported as `.psu` and brought in with `gt4_host --import-save`, it loads natively alike and resumes at
  Home, where it was saved. An empty save folder starts a new career (the name prompt), so those values come from the
  saves. A native save folder copied as is into a PCSX2 folder card is not seen (lane C): go through a card image or
  a `.psu`. The same with a copy of the approved save (a career in the middle of the Pan Euro Championship): loaded
  and saved natively, loaded and saved in PCSX2, imported and loaded natively, it shows the same career at each step
  (the owner's Home: 13.7 %, 3,674 A-spec points, Days 300, 629,068 Cr, 26 JAN, the BMW M3 CSL '03, the championship
  still running). A second copy, withdrawn from the championship first so GT4 opens the rest, kept the same through
  the same three steps: the garage (12 cars, the same list, power and driven order), one car's record (the NISMO 400R
  '96: Lightning Yellow, 492 HP, 408.37 ft-lb, 98.7 miles) and the licences (B, IB, A and IA passed, Super not). The
  garage list's sort order, a display setting, is not kept by a save. Left open: the save files are compressed or
  encrypted (8.0 bits a byte), so no scan tells what else they hold. A host pointer no load ever follows would
  round-trip silently too: this shows that no load depends on one, not that none is written.

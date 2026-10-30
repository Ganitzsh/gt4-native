# Rewriting a VU0 function in C++

Layer 3 of docs/NATIVE_ONLY.md: game functions whose source has VU0 instructions in inline asm get a native
version in plain C++, `game_src/func_ADDR.cpp`, which the native link uses in place of the source. The game has no
VU0 model at run time (2026-10-30): `tools/game_toolchain/asm_translate.py` refuses VU0 instructions, so such a
source does not compile natively until it has its rewrite. Examples: `game_src/func_003E21D8.cpp`,
`game_src/func_003B7C98.cpp`, `game_src/func_0049EE00.cpp`.

## Rules

1. **Signature.** Keep exactly the signature of the source the native link uses now (`tools/vu0_state_map.py` prints
   its path: `src/...` a matched source, `drafts/...` or `build/swarm/...` a provisional draft, `game_src/...` an
   earlier native copy): the name, the parameters' types and order, the return type. Callers are compiled against
   it. Keep the source's first line (`/* compiler: ... */`).
2. **Semantics from the original.** `py tools/match.py asm 0xADDR` in the decomp prints the original assembly. A
   matched source compiles to it exactly; a draft only nearly (it may be wrong): when they disagree, the assembly is
   right. VU0 microprograms the function starts (`vcallms ADDR`): `tools/vu0_disasm.py` prints them, and
   `tools/eeref/vu0_programs.cpp` has them in C++ in the VU's arithmetic (a test reference, not in the game).
3. **No asm.** Include `gt4_vec.h`. The original keeps state in VU0's registers across functions; natively these
   are the 32 *shared vectors* `gt4_vec(n)` (`gt4_shared_vectors`, runtime/shared_state.h). A register the
   function reads before writing it (`in:` in `tools/vu0_state_map.py`) is a shared vector it reads. A register it
   writes is a shared vector only if it is a state register (vf8..vf31): write it as the original leaves it. The
   matrix stack in VU0 data memory is `gt4_matrix_stack` with its top `gt4_matrix_stack_top` (the original's vi13,
   in rows), the R register `gt4_random_state`. vf1..vf7, ACC, Q and I are temporaries: local variables (exception:
   the sine and cosine 004A7438 leaves in vector 1). A microprogram the function starts is done in place, in plain
   float arithmetic (gt4_vec.h has the 2D projection of 0x678, `gt4_project_2d`).
4. **Arithmetic.** Form sums in the original's order (`gt4_rows3`, `gt4_rows4`, or by hand: row 0 times x, plus row 1
   times y, ...). Compute only the lanes the original's results use; a lane of garbage multiplied by zero is a NaN in
   x87 (the VU has none). VU specifics: FTOI*n* truncates toward zero and saturates (`gt4_ftoi(x, frac)`); ITOF*n* is
   `(float)(int)bits / (1 << n)`; DIV and RSQRT by zero give the largest float of the right sign (`gt4_vu_div`);
   MAX/MINI (`gt4_maxf`/`gt4_minf`).
5. **Memory.** Copy the structures from the source. A 128-bit store (`sqc2`, `sq`) writes four lanes: store the w
   lane the original stores (often 1, 0, or a value it computed). Unaligned three-float points: `memcpy`.
6. **gcc 2.9.** The native compiler is gcc 2.9 (C++98 without most of the library): no `auto`, `constexpr`,
   lambdas, `<cstdint>` or `<algorithm>`; `extern "C" void *memcpy(void *, const void *, unsigned int);` if needed.
7. **Test.** Add a test to `tools/eeref/difftest_*.cpp` (see `difftest.h`, and `difftest.cpp` for examples): fill
   the shared vectors it reads, the memory it reads, and the arguments, all random (within ranges the game uses).
   `GT4_DIFFTEST` then runs the native and the original code from the same state and compares what they return and
   leave in the scratch area, the matrix stack, the scratchpad, a data region the test names, shared vectors 8..31,
   the stack's top and the random state. A function whose
   inputs are impractical to build (deep object graphs) is checked by recording real calls instead
   (docs/DIFFERENTIAL_TESTING.md): say so.
8. **Compile check.** Copy the file to `C:\Users\julie\gt4-decomp\build\native_check_NAME\`, list it in a text
   file as `build/native_check_NAME/func_ADDR.cpp` (one per line), then from `C:\Users\julie\gt4-native-game`:
   `py tools/game_cc.py --decomp C:/Users/julie/gt4-decomp --out build/check_NAME --list LISTFILE`. Delete the
   staging and output directories afterwards.

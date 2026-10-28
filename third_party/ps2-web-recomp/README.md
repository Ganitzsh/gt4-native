# ps2-web-recomp (MIT)

From https://github.com/snuri00/ps2-web-recomp at commit `1b4808793a12c45125262fd2d5f50a9ec9f7c020`, unmodified:

- `recomp/`: `eerecomp`, the translator of R5900 machine code to C++ (main, r5900, elf, analysis, codegen, snr2);
- `runtime/include/ps2/`: the headers the translated code includes (the R5900 registers, the EE's FPU rules,
  MMI, VU0 macro instructions, memory access).

Used only by `tools/eeref` (docs/DIFFERENTIAL_TESTING.md): the original game code, translated from the user's
own executable, as a reference to test the native code against. Nothing of it is linked into the game or the host.
The license is in `LICENSE`.

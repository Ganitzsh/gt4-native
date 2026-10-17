# Portable primitives (30 modules)

This directory contains **30 small, dependency-light host C++20 modules** for the future GT4 native port. Each module is deliberately isolated so that its behavior can be validated before it is used by the game runtime.

| Group | Modules |
|---|---|
| Addressing and integers (9) | `align`, `range`, `bits`, `sign_extend`, `rotate`, `byteswap`, `saturate`, `clamp`, `fixed16` |
| Math and colors (6) | `lerp`, `vec2`, `vec3`, `vec4`, `mat4`, `color` |
| Checksums and deterministic state (4) | `lcg`, `crc32`, `fnv1a`, `adler32` |
| Binary data (6) | `cursor`, `u16reader`, `u32reader`, `u64reader`, `u32writer`, `cstring` |
| PS2-facing helpers (5) | `eucjp_digits`, `mips_jump`, `mips_branch`, `mips_fields`, `pad_buttons` |

**Important:** These are general primitives and illustrative PS2-facing helpers, **not** 30 decompiled GT4 functions. The MIPS helpers decode only limited instruction/address fields, not the instruction set. The active-low pad helper is a raw mask operation, not a complete DualShock 2 driver. Vector/matrix storage and multiplication conventions must be validated against the actual upstream functions before use.

Tests: `tests/portable_primitives.cpp`, registered in CTest as `portable_primitives`. This initial test suite covers one or more normal-case assertions per module; boundary, fuzz, sanitizer, Windows and real GT4 runtime tests remain future work.

Do not use these modules to claim native game compatibility until the relevant upstream functions have been compiled and compared.

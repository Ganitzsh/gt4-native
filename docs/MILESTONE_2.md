# Milestone 2 — linked GT4 source functions

The native build now compiles **three authentic GT4-decomp functions** from the pinned upstream revision `de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1`:

| Original source | Native test | Host dependencies |
|---|---|---|
| `src/runtime/MboolReader/MboolReader__structor_19.cpp` | `upstream_mboolreader` | Base constructor, release function, vtable test shims |
| `src/runtime/hArrayCompare/func_005C9930.cpp` | `upstream_harraycompare` | None |
| `src/runtime/hArrayCompare/func_005C9970.c` | `upstream_harraycompare` | `func_001465D8` divisor shim |

## Real shared-state chain

The second test compiles the **original setter** `func_005C9930` and **original normalized-value reader** `func_005C9970` together. Both operate on the same original 0x174-byte object field:

1. Setter writes 120 to field at offset 0x174.
2. The object representation is copied into the reader's matching layout using `memcpy`, avoiding C++ cross-type aliasing.
3. Reader returns 120 / 4 = 30.
4. Setter changes field to -18; reader returns -18 / 3 = -6.

The divisor comes from an **explicit stub** for `func_001465D8`. There is no synthetic reimplementation of either original function's logic. The imported source bodies are unchanged except for prepended provenance comments.

This proves a **two-function original GT4 dataflow on the host**, but **not** a direct call from one original function into another. They are called in sequence by the integration test. The external divisor dependency remains a stub, and guest-pointer ABI integration is still pending. The test uses host-native memory, not actual game assets or a PS2 emulator.

## Reproduction

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build --parallel
ctest --test-dir build -R 'upstream_' --output-on-failure
```

Both tests are part of the cross-platform CI matrix and the sanitizer job. See the [GitHub Actions runs](https://github.com/gustavocanepa/gt4-native/actions) for results of the latest commit.

## Next steps

Replace `func_001465D8` with its real matched source once located and ported; then bring in original code that **calls** another imported function, and test guest 32-bit memory layout rather than host pointer layouts.

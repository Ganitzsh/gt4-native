# Milestone 2 — linked GT4 source functions

> Historical milestone. [Milestone 4](MILESTONE_4.md) supersedes the earlier guest-pointer limitations and the separate divisor-context bridge.

The native build now compiles **four authentic GT4-decomp functions** from the pinned upstream revision `de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1`:

| Original source | Native test | Host dependencies |
|---|---|---|
| `src/runtime/MboolReader/MboolReader__structor_19.cpp` | `upstream_mboolreader` | Base constructor, release function, vtable test shims |
| `src/runtime/hArrayCompare/func_005C9930.cpp` | `upstream_harraycompare` | None |
| `src/runtime/hArrayCompare/func_005C9970.c` | `upstream_harraycompare` | ABI bridge to original divisor |
| `src/gamedata/unit_0013B5F8/func_001465D8.cpp` | `upstream_harraycompare` | None; takes object pointer |

## Real shared-state chain

The second test compiles the **original setter** `func_005C9930` and **original normalized-value reader** `func_005C9970` together. Both operate on the same original 0x174-byte object field:

1. Setter writes 120 to field at offset 0x174. The real divisor source reads an integer at offset 0x178 in its own context object and divides by 100.
2. The object representation is copied into the reader's matching layout using `memcpy`, avoiding C++ cross-type aliasing.
3. Reader returns 120 / 4 = 30.
4. Setter changes field to -18; reader returns -18 / 3 = -6.

The divisor now comes from the **original** `func_001465D8` implementation. The test provides a context object with field 400 (giving divisor 4) and later 300 (giving divisor 3). **ABI mismatch:** the decompiled reader calls `func_001465D8()` without arguments, whereas the original definition requires `Obj001465D8*`. A host-only wrapper passes the context object to the real function. The original definition is renamed at compile time to avoid a symbol collision; the source body remains unchanged. There is no synthetic reimplementation of either original function's logic. The imported source bodies are unchanged except for prepended provenance comments.

This proves a **two-function original GT4 dataflow on the host**, but **not** a direct call from one original function into another. They are called in sequence by the integration test. The divisor computation is real, but the host-only ABI bridge is still required, and guest-pointer ABI integration is pending. The test uses host-native memory, not actual game assets or a PS2 emulator.

## Reproduction

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build --parallel
ctest --test-dir build -R 'upstream_' --output-on-failure
```

Both tests are part of the cross-platform CI matrix and the sanitizer job. See the [GitHub Actions runs](https://github.com/gustavocanepa/gt4-native/actions) for results of the latest commit.

## Next steps

Resolve the incompatible declarations of `func_001465D8` by inspecting the target's call-site assembly and original ABI; replace the context bridge with verified behavior. Then test guest 32-bit memory layout rather than host pointer layouts.

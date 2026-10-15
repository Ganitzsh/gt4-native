# Milestone 2 — first real GT4 function on a native host

## Provenance

The first imported unit is **not a reimplementation or synthetic example**. It is the C++ source from the matching decompilation repository:

- Upstream: https://github.com/gustavocanepa/gt4-decomp
- Pinned upstream revision: `de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1`
- Source: `src/runtime/MboolReader/MboolReader__structor_19.cpp`
- Imported source: `upstream/gt4/src/runtime/MboolReader/MboolReader__structor_19.cpp`
- Native test: `tests/upstream_mboolreader.cpp`

The imported function body and declarations are **identical to the upstream source**; only provenance comments were prepended.

## What the test proves

CMake compiles the imported original C++ function directly with the **host C++20 compiler** and links it into a native executable. The test invokes `MboolReader__structor_19` on host memory and verifies:

1. It writes the address of the imported vtable symbol into the object.
2. It calls `MReaderBase__structor_0` with the expected object and null second argument.
3. It calls `func_005C1628` only when the low bit of `flags` is set (tested with flags 0, 1, 2, 3).

Two external function dependencies and the vtable symbol are supplied as **explicit test shims**. This is a meaningful first host execution of original GT4 decomp source, **not** a fully integrated game subsystem. It does not prove PS2 binary matching (that is the upstream project's job), correct guest 32-bit pointer layout on 64-bit hosts, or compatibility with real runtime objects.

## Reproduce

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build --target gt4_upstream_mboolreader_test
ctest --test-dir build -R upstream_mboolreader --output-on-failure
```

## Next milestone-2 gates

- [x] Select an actual upstream GT4 source and pin its commit.
- [x] Import original C++ and compile it with a host compiler.
- [x] Test both constructor branches with host-side dependency shims.
- [ ] Obtain passing GitHub CI results for the final commit.
- [ ] Replace shims with correctly ported runtime dependencies.
- [ ] Exercise a real guest-layout object, rather than a host-pointer object.
- [ ] Execute a second function connected to the first in the same dependency chain.

The repository contains no game assets, BIOS or original executable.

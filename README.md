# GT4 Native

**A native PC port of Gran Turismo 4, in early planning.** Goal: the game's own code (the [GT4 matching decompilation](https://github.com/gustavocanepa/gt4-decomp)) running on a modern 64-bit PC with a native renderer, native sound and native platform services, **with no PS2 hardware emulation and no translated game code in the product.**

> **Status: no playable build exists yet.** The code in this repository is the bootstrap and the tools that prepare the port (guest memory model, ELF inspection, source import and triage, differential testing against the original machine code). It is not the game.

## What this project is, and is not

- It **is** the plan and the code for a native port: game code adapted from the decompilation, a native renderer (R1), native sound (A1) and native platform services (P1). See [docs/ROADMAP.md](docs/ROADMAP.md).
- It **is not** an emulator, a static recompilation or a hybrid build: the product contains no PS2 hardware emulation and no translated game code. Correctness is checked against recorded reference behaviour of the original game, and results are published here as numbers and documents.
- Matching improvements to the decompiled sources belong in [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp), which is the source of truth for the game code.

## Projects and attribution

- **[gt4-decomp](https://github.com/gustavocanepa/gt4-decomp)** is the source of truth for matching PS2 code (MIT).
- **GT4 Native** develops host-platform adaptations, ABI handling, modern compiler builds, the native renderer, sound and platform layers.
- Initiated by [Gustavo Canepa](https://github.com/gustavocanepa). See [CREDITS.md](CREDITS.md) for contributors and upstream attribution.
- Do not manually fork thousands of source files: import a pinned upstream revision with provenance and license checks.

## Build and test

Requires CMake 3.21+ and a C++20 compiler.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The bootstrap executable prints a status message; `--object-demo` runs the guest-backed accessor chain. `include/gt4/guest_abi.hpp` provides **data-only**, compile-time checked guest layouts; it is not a functioning guest/host ABI bridge.

## Implemented host-side components

- Checked guest memory access (`MemoryView`), multi-region address space (`AddressSpace`), and typed scalar/structure `guest_ptr<T>` access, checked guest arrays and field proxies.
- Legacy ee-gcc virtual-call resolution and a limited native callback registry.
- Read-only copy-on-write guest string header inspection.
- ELF32 little-endian MIPS PT_LOAD parser and `gt4_elf_inspect` command.
- Sixteen CTest targets (bootstrap, ABI, memory, virtual dispatch, strings, callbacks, address space, typed guest pointers/objects, ELF parser, portable primitives, robustness, ELF mutation stress, and three original-GT4 integration tests).

**These are foundational compatibility tools, not playable game code.** Cross-platform builds and tests are automated for Linux (GCC/Clang), Windows (MSVC) and macOS (AppleClang), in Debug and Release, plus Linux ASan/UBSan. See [Milestone 1 validation](docs/history/MILESTONE_1.md), [Milestone 2](docs/history/MILESTONE_2.md), [Milestone 3](docs/history/MILESTONE_3.md), and the [GitHub Actions results](https://github.com/gustavocanepa/gt4-native/actions). Fifteen original GT4 source functions are selected in the native build. Thirteen accessor/calculation bodies run over checked PS2-format memory through reviewed native adaptations and are linked into the production executable. The normalized reader now explicitly passes the same object to the original divisor calculation, based on inspection of the PS2 call site; the previous global-context bridge is removed. See [Milestone 4](docs/history/MILESTONE_4.md).

Run `gt4_native --object-demo` to demonstrate the original chain on a synthetic object without game assets. This establishes a narrow guest-memory integration slice, not full PS2 ABI compatibility, real asset loading, or playability.

## Automated upstream integration pipeline

The GitHub Actions [bulk source inventory workflow](https://github.com/gustavocanepa/gt4-native/actions/workflows/import-decomp.yml) imports a **pinned** revision of [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) into a downloadable CI artifact, without bundling proprietary game assets. The pipeline generates a source manifest with provenance and hashes, performs heuristic native-compatibility triage, probes selected translation units with the host compiler, checks relocatable and executable linking, and produces a prioritized integration review queue. Its Python tools include `tools/import_decomp.py`, `tools/triage_native.py`, `tools/native_link_gate.py`, and `tools/integration_queue.py`.

**A successful import, syntax check, compile, or isolated link does not mean a function is integrated, ABI-compatible, behaviorally verified, or playable.** Candidates still require reviewed host adaptations and meaningful tests. The workflow runs on GitHub-hosted runners; a local PC is not needed to run these checks. See [bulk import documentation](docs/BULK_IMPORT.md) and [workflow runs](https://github.com/gustavocanepa/gt4-native/actions).

## Upstream source inventory

Clone [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) next to this repository, then:

```sh
python3 tools/import_sources.py --upstream ../gt4-decomp
```

This writes `build/upstream-inventory.json` with the upstream commit and source paths; it **does not** copy sources or game assets.

## Support

[GitHub Sponsors is active](https://github.com/sponsors/gustavocanepa). Sponsorship is voluntary; it does not purchase game content, grant access to copyrighted assets, or promise a playable release.

## Legal

Independent, unofficial project, not affiliated with or endorsed by Sony Interactive Entertainment or Polyphony Digital. No game data, original executable, BIOS or proprietary SDK files are distributed here. Users must supply their own legally obtained game data where required.

This repository's original code is licensed under the [GNU General Public License, version 3](LICENSE) (GPL-3.0). Copyright (c) 2026 Gustavo Canepa. Anyone who distributes this port or a work based on it must make the corresponding source available under the same license.

Sources imported from [gt4-decomp](https://github.com/gustavocanepa/gt4-decomp) under `upstream/gt4/` keep their own MIT license and notices; MIT code may be combined with GPL code. Revisions of this repository up to and including commit `5799594b1fa25218f1d1c0b87602036d508aaf53` were published under the MIT license and remain available under it. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/RELEASE_POLICY.md](docs/RELEASE_POLICY.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Matching contributions go to upstream; portability changes go here.

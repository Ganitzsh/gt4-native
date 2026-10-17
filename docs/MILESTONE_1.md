# Milestone 1 — validated native build infrastructure

## Scope

Milestone 1 establishes a repeatable C++20 build, automated tests, and CI diagnostics for the **bootstrap and compatibility helpers**. It does not prove native GT4 gameplay or PS2 ABI equivalence.

## Build locally

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

On Visual Studio / other multi-config generators, pass `--config Debug` or `--config Release` to `cmake --build` and `-C Debug` or `-C Release` to `ctest`.

## Sanitizers (Linux Clang)

```sh
CC=clang CXX=clang++ cmake -S . -B build-sanitized \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DGT4_STRICT_WARNINGS=ON -DGT4_ENABLE_SANITIZERS=ON
cmake --build build-sanitized --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitized --output-on-failure
```

## CI coverage

`.github/workflows/build.yml` runs:
- Linux GCC Debug + Release
- Linux Clang Debug + Release
- Windows MSVC Debug + Release
- macOS AppleClang Debug + Release
- Linux Clang Debug with AddressSanitizer + UndefinedBehaviorSanitizer

All matrix jobs build with additional warnings and run the CTest suite. Tests based on `assert` keep assertions enabled in Release via `/UNDEBUG` or `-UNDEBUG`. The independent robustness test uses a `CHECK` macro and exercises invalid address mappings, read-only memory, vtable offsets, string headers, and malformed ELF input. The deterministic ELF mutation test processes 10,000 one-byte variations with a 64 KiB allocation budget under every CI configuration.

## Input hardening

`parse_mips_elf32` limits total PT_LOAD memory allocation to 256 MiB by default (overridable through the function's second argument), preventing a malformed ELF from requesting multi-gigabyte allocations.

## Exit criteria

- [x] Reproducible CMake C++20 build with a bootstrap executable.
- [x] Automated unit and negative/boundary tests.
- [x] CI matrix for Linux, Windows, macOS and both build configurations.
- [x] GCC and Clang exercised on Linux.
- [x] ASan and UBSan CI job.
- [ ] A representative original GT4 function compiled and compared against upstream: **Milestone 2**, not Milestone 1.
- [ ] Full fuzzing, real game binaries, real game data, and runtime validation: later milestones.

CI results are the authoritative cross-platform verification; local container tests are supplementary.

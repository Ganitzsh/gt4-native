# Milestone 3 — first checked guest-pointer call

> Historical milestone. [Milestone 4](MILESTONE_4.md) supersedes the earlier guest-pointer limitations and the separate divisor-context bridge.

The upstream inventory and review queue were refreshed against GT4 decomp commit
`69c3585cff2a48f98c6cc9c2c495012d28c022cd`. The pipeline imported 28,434 translation units;
all 30 syntax probes passed, and the isolated link gate compiled and linked all 20 candidates it
examined. After adding one candidate to a test target, the refreshed queue marks it as
`already_integrated` and leaves 19 review-ready. That label records CMake inclusion only.

One review-ready leaf, `src/race/unit_00378778/func_00385B28.cpp`, was added to the native test
build. Its original body reads an integer through a pointer and returns that value plus `0x14`.
`gt4::guest::guest_ptr<int>` keeps the PS2 address as a 32-bit value, reads it through the checked
`AddressSpace`, and the test bridge copies the scalar into host storage before calling the original
function. The test covers negative and positive inputs and verifies the source function leaves the
guest memory unchanged.

This is a host-tested call of one original source function through a narrow scalar-pointer bridge.
It does not establish the original function's caller ABI, class layout, gameplay role, runtime
interoperability, or playability. Guest structures and guest pointers embedded in structures still
need explicit layouts and reviewed adapters.

## Reproduction

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DGT4_STRICT_WARNINGS=ON
cmake --build build --parallel
ctest --test-dir build -R 'guest_ptr|upstream_guest_ptr' --output-on-failure
```

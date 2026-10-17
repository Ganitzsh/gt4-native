#include "gt4/guest_ptr.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

int func_00385B28(int*);
static_assert(sizeof(int) == 4, "the original GT4 source assumes 32-bit int");

// Explicit host bridge: resolve the 32-bit guest address, copy its scalar to
// host storage, then call the original source function with a real host pointer.
static int call_original(const gt4::guest::AddressSpace& memory,
                         gt4::guest::guest_ptr<int> guest_value) {
    int host_value = guest_value.load(memory);
    return func_00385B28(&host_value);
}

#define CHECK(expr) do { if (!(expr)) { std::cerr << "GT4 guest pointer integration failed: " #expr << '\n'; return 1; } } while (false)

int main() {
    std::array<std::byte, 8> ram{};
    gt4::guest::AddressSpace memory;
    memory.map(0x00130000, ram);
    const gt4::guest::guest_ptr<int> value{0x00130004};

    value.store(memory, -7);
    CHECK(call_original(memory, value) == 13);
    value.store(memory, 0x100);
    CHECK(call_original(memory, value) == 0x114);
    CHECK(value.load(memory) == 0x100); // original accessor leaves guest state unchanged

    std::cout << "Original func_00385B28 called through checked guest_ptr bridge: passed\n";
}

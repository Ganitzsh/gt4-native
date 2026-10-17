#include "gt4/guest_address_space.hpp"
#include "gt4/guest_dispatch.hpp"
#include "gt4/guest_string.hpp"
#include "gt4/elf32.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " failed: " #expr << '\n'; return 1; } } while (false)

template<class E, class F> bool raises(F&& f) {
    try { f(); } catch (const E&) { return true; }
    return false;
}

int main() {
    using namespace gt4::guest;
    std::array<std::byte, 32> ram{};
    AddressSpace map;
    map.map(0xFFFFFFE0u, ram);
    map.write32_le(0xFFFFFFFCu, 0xCAFEBABEu);
    CHECK(map.read32_le(0xFFFFFFFCu) == 0xCAFEBABEu);
    CHECK(raises<std::out_of_range>([&] { (void)map.read32_le(0xFFFFFFFFu); }));
    CHECK(raises<std::invalid_argument>([&] { map.map(0xFFFFFFF0u, ram); }));
    std::array<std::byte, 32> rom{};
    map.map(0x1000u, rom, false);
    CHECK(raises<std::runtime_error>([&] { map.write8(0x1000u, 1); }));
    CHECK(raises<std::out_of_range>([&] { (void)map.read8(0x2000u); }));

    std::array<std::byte, 256> data{};
    MemoryView mem{0x1000u, data};
    mem.write32_le(0x1024u, 0x1080u);
    mem.write16_le(0x1088u, 0xFFFCu);
    mem.write16_le(0x108Au, 0);
    mem.write32_le(0x108Cu, 0x00401234u);
    const auto call = resolve_virtual_call(mem, 0x1020u, 0);
    CHECK(call.adjusted_this == 0x101Cu && call.target == 0x00401234u);
    CHECK(raises<std::out_of_range>([&] { (void)resolve_virtual_call(mem, 0x1020u, 100u); }));
    CHECK(raises<std::out_of_range>([&] { (void)read_string_rep(mem, 8u); }));
    mem.write32_le(0x1030u, 4u);
    mem.write32_le(0x1034u, 3u);
    CHECK(raises<std::invalid_argument>([&] { (void)read_string_rep(mem, 0x1040u); }));

    std::array<std::byte, 52> elf{};
    CHECK(raises<std::invalid_argument>([&] { (void)parse_mips_elf32(elf); }));
    elf[0] = std::byte{0x7f}; elf[1] = std::byte{'E'};
    elf[2] = std::byte{'L'}; elf[3] = std::byte{'F'};
    elf[4] = std::byte{1}; elf[5] = std::byte{1}; elf[6] = std::byte{1};
    elf[16] = std::byte{2}; elf[18] = std::byte{8}; elf[20] = std::byte{1};
    elf[28] = std::byte{52}; elf[42] = std::byte{32}; elf[44] = std::byte{1};
    CHECK(raises<std::out_of_range>([&] { (void)parse_mips_elf32(elf); }));
    std::cout << "Robustness checks passed\n";
}

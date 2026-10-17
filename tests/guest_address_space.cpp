#include "gt4/guest_address_space.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

template<class Exception, class F> bool throws(F&& f) {
    try { f(); } catch (const Exception&) { return true; }
    return false;
}
int main() {
    std::array<std::byte, 16> ram{};
    std::array<std::byte, 8> rom{};
    gt4::guest::AddressSpace space;
    space.map(0x2000, ram);
    space.map(0x1000, rom, false); // intentionally insert out of order
    assert(space.region_count() == 2);
    space.write32_le(0x2000, 0x12345678);
    assert(space.read32_le(0x2000) == 0x12345678);
    assert(space.read8(0x2000) == 0x78);
    space.write16_le(0x200E, 0xBEEF);
    assert(space.read16_le(0x200E) == 0xBEEF);
    assert(throws<std::runtime_error>([&] { space.write8(0x1000, 1); }));
    assert(throws<std::out_of_range>([&] { (void)space.read8(0x1800); }));
    assert(throws<std::out_of_range>([&] { (void)space.read32_le(0x200E); }));
    assert(throws<std::invalid_argument>([&] { space.map(0x2008, rom); }));
    assert(throws<std::invalid_argument>([&] { space.map(0xFFFFFFFF, rom); }));
    assert(throws<std::invalid_argument>([&] { space.map(0x3000, std::span<std::byte>{}); }));
    // Adjacent regions are legal, but accesses cannot silently span them.
    space.map(0x2010, rom);
    assert(throws<std::out_of_range>([&] { (void)space.read32_le(0x200F); }));
    space.write8(0x2010, 0xA5);
    assert(space.read8(0x2010) == 0xA5);
}

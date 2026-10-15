#include "gt4/guest_memory.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

template <typename F> bool throws_range(F f) {
    try { f(); } catch (const std::out_of_range&) { return true; }
    return false;
}
int main() {
    std::array<std::byte, 64> data{};
    gt4::guest::MemoryView m{0x00100000u, data};
    assert(m.base() == 0x00100000u && m.size() == 64);
    m.write32_le(0x00100004u, 0x12345678u);
    assert(m.read32_le(0x00100004u) == 0x12345678u);
    assert(m.read8(0x00100004u) == 0x78u);
    assert(m.read8(0x00100007u) == 0x12u);
    m.write16_le(0x00100008u, 0xBEEFu);
    assert(m.read16_le(0x00100008u) == 0xBEEFu);
    m.write8(0x0010003Fu, 0xA5u);
    assert(m.read8(0x0010003Fu) == 0xA5u);
    assert(throws_range([&] { (void)m.read8(0x000FFFFFu); }));
    assert(throws_range([&] { (void)m.read32_le(0x0010003Eu); }));
    assert(throws_range([&] { (void)m.read8(0xFFFFFFFFu); }));
    assert(throws_range([&] { m.write16_le(0x0010003Fu, 0xFFFFu); }));
    // Empty view at the end of the mapping is valid; reading is not.
    assert(m.bytes(0x00100040u, 0).empty());
    assert(throws_range([&] { (void)m.bytes(0x00100040u, 1); }));
    bool rejected = false;
    try {
        gt4::guest::MemoryView invalid{0xFFFFFFF0u, data};
    } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

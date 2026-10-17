#include "gt4/elf32.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

int main() {
    std::array<std::byte, 128> elf{};
    auto w8 = [&](std::size_t o, std::uint8_t v) { elf[o]=std::byte(v); };
    auto w16 = [&](std::size_t o, std::uint16_t v) { w8(o,v); w8(o+1,v>>8); };
    auto w32 = [&](std::size_t o, std::uint32_t v) { w16(o,v); w16(o+2,v>>16); };
    w8(0,0x7f); w8(1,'E'); w8(2,'L'); w8(3,'F');
    w8(4,1); w8(5,1); w8(6,1);
    w16(16,2); w16(18,8); w32(20,1); w32(24,0x1000);
    w32(28,52); w16(42,32); w16(44,1);
    w32(52,1); w32(56,96); w32(60,0x1000); w32(68,4);
    w32(72,8); w32(76,5);
    w8(96,1); w8(97,2); w8(98,3); w8(99,4);
    auto image=gt4::guest::parse_mips_elf32(elf);
    assert(image.entry==0x1000 && image.segments.size()==1);
    assert(image.segments[0].bytes.size()==8);
    assert(image.segments[0].bytes[0]==std::byte(1));
    assert(image.segments[0].bytes[4]==std::byte(0));
    bool capped=false;
    try { (void)gt4::guest::parse_mips_elf32(elf, 7); }
    catch (const std::length_error&) { capped=true; }
    assert(capped);
    w32(72,3);
    bool rejected=false;
    try { (void)gt4::guest::parse_mips_elf32(elf); }
    catch (const std::out_of_range&) { rejected=true; }
    assert(rejected);
}

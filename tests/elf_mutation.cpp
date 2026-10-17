#include "gt4/elf32.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>

int main() {
    std::array<std::byte, 128> base{};
    auto put8 = [&](std::size_t p, std::uint8_t v) { base[p] = std::byte(v); };
    auto put16 = [&](std::size_t p, std::uint16_t v) { put8(p, v); put8(p+1, v>>8); };
    auto put32 = [&](std::size_t p, std::uint32_t v) { put16(p,v); put16(p+2,v>>16); };
    put8(0, 0x7f); put8(1, 'E'); put8(2, 'L'); put8(3, 'F');
    put8(4, 1); put8(5, 1); put8(6, 1);
    put16(16, 2); put16(18, 8); put32(20, 1);
    put32(24, 0x1000); put32(28, 52); put16(42, 32); put16(44, 1);
    put32(52, 1); put32(56, 96); put32(60, 0x1000);
    put32(68, 4); put32(72, 8); put32(76, 5);
    put8(96, 1); put8(97, 2); put8(98, 3); put8(99, 4);
    const auto original = gt4::guest::parse_mips_elf32(base, 65536);
    if (original.segments.size() != 1 || original.segments[0].bytes.size() != 8)
        return 1;
    std::uint32_t state = 0x475434u;
    for (unsigned i = 0; i < 10000; ++i) {
        auto mutated = base;
        state = state * 1664525u + 1013904223u;
        const auto position = (state >> 8) % mutated.size();
        state = state * 1664525u + 1013904223u;
        mutated[position] ^= std::byte(state >> 24);
        try {
            auto parsed = gt4::guest::parse_mips_elf32(mutated, 65536);
            for (const auto& segment : parsed.segments) {
                if (segment.bytes.size() > 65536) return 2;
            }
        } catch (const std::exception&) {
            // Rejecting malformed data is expected; crashing or OOM is not.
        }
    }
    std::cout << "10,000 deterministic ELF mutations completed\n";
}

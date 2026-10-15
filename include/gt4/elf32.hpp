#pragma once
#include "gt4/guest_abi.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace gt4::guest {
struct ElfSegment {
    Address virtual_address;
    std::uint32_t flags;
    std::vector<std::byte> bytes;
};
struct ElfImage {
    Address entry;
    std::vector<ElfSegment> segments;
};

// Parse a little-endian 32-bit MIPS ELF. This loads PT_LOAD data only; it does
// not execute instructions, relocate symbols, or emulate PS2 hardware.
[[nodiscard]] inline ElfImage parse_mips_elf32(std::span<const std::byte> input) {
    auto u8 = [&](std::uint64_t p) -> std::uint8_t {
        if (p >= input.size()) throw std::out_of_range("truncated ELF");
        return std::to_integer<std::uint8_t>(input[static_cast<std::size_t>(p)]);
    };
    auto u16 = [&](std::uint64_t p) -> std::uint16_t {
        return std::uint16_t(u8(p)) | (std::uint16_t(u8(p+1)) << 8);
    };
    auto u32 = [&](std::uint64_t p) -> std::uint32_t {
        return std::uint32_t(u16(p)) | (std::uint32_t(u16(p+2)) << 16);
    };
    if (input.size() < 52 || u8(0) != 0x7f || u8(1) != 'E' ||
        u8(2) != 'L' || u8(3) != 'F' || u8(4) != 1 || u8(5) != 1 ||
        u8(6) != 1)
        throw std::invalid_argument("expected little-endian ELF32");
    if (u16(16) != 2 || u16(18) != 8 || u32(20) != 1)
        throw std::invalid_argument("expected executable MIPS ELF32");
    const auto entry = u32(24);
    const auto phoff = u32(28);
    const auto phentsize = u16(42);
    const auto phnum = u16(44);
    if (phentsize < 32 || std::uint64_t(phoff) + std::uint64_t(phentsize)*phnum > input.size())
        throw std::out_of_range("invalid ELF program header table");
    ElfImage result{entry, {}};
    for (std::uint32_t i=0; i<phnum; ++i) {
        const auto p = std::uint64_t(phoff) + std::uint64_t(i)*phentsize;
        if (u32(p) != 1) continue;
        const auto offset = u32(p+4);
        const auto address = u32(p+8);
        const auto filesz = u32(p+16);
        const auto memsz = u32(p+20);
        const auto flags = u32(p+24);
        if (memsz < filesz || std::uint64_t(offset)+filesz > input.size() ||
            std::uint64_t(address)+memsz > (std::uint64_t{1} << 32))
            throw std::out_of_range("invalid ELF loadable segment");
        if (memsz == 0) continue;
        ElfSegment segment{address, flags, std::vector<std::byte>(memsz)};
        for (std::size_t j=0; j<filesz; ++j)
            segment.bytes[j] = input[std::size_t(offset)+j];
        result.segments.push_back(std::move(segment));
    }
    return result;
}
} // namespace gt4::guest

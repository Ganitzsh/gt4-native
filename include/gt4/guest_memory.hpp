#pragma once
#include "gt4/guest_abi.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>

namespace gt4::guest {

// Maps a contiguous region of PS2 address space onto host-owned storage.
// This is a checked address translator, NOT a MIPS emulator or allocator.
class MemoryView {
public:
    MemoryView(Address base, std::span<std::byte> storage)
        : base_(base), storage_(storage) {
        constexpr auto max_space = std::uint64_t{1} << 32;
        if (std::uint64_t(base) + storage.size() > max_space)
            throw std::invalid_argument("guest memory mapping exceeds 32-bit address space");
    }

    [[nodiscard]] Address base() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }

    [[nodiscard]] std::span<std::byte> bytes(Address address, std::size_t count) {
        const auto offset = checked_offset(address, count);
        return storage_.subspan(offset, count);
    }

    [[nodiscard]] std::span<const std::byte> bytes(Address address, std::size_t count) const {
        const auto offset = checked_offset(address, count);
        return std::span<const std::byte>(storage_).subspan(offset, count);
    }

    [[nodiscard]] std::uint8_t read8(Address address) const {
        return std::to_integer<std::uint8_t>(bytes(address, 1)[0]);
    }

    [[nodiscard]] std::uint16_t read16_le(Address address) const {
        auto b = bytes(address, 2);
        return std::uint16_t(std::to_integer<std::uint8_t>(b[0])) |
               (std::uint16_t(std::to_integer<std::uint8_t>(b[1])) << 8);
    }

    [[nodiscard]] std::uint32_t read32_le(Address address) const {
        auto b = bytes(address, 4);
        return std::uint32_t(std::to_integer<std::uint8_t>(b[0])) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[1])) << 8) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[2])) << 16) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[3])) << 24);
    }

    void write8(Address address, std::uint8_t value) {
        bytes(address, 1)[0] = std::byte(value);
    }

    void write16_le(Address address, std::uint16_t value) {
        auto b = bytes(address, 2);
        for (unsigned i = 0; i != 2; ++i) b[i] = std::byte(value >> (i * 8));
    }

    void write32_le(Address address, std::uint32_t value) {
        auto b = bytes(address, 4);
        for (unsigned i = 0; i != 4; ++i) b[i] = std::byte(value >> (i * 8));
    }

private:
    [[nodiscard]] std::size_t checked_offset(Address address, std::size_t count) const {
        if (address < base_) throw std::out_of_range("guest address below mapping");
        const auto offset = std::size_t(std::uint64_t(address) - base_);
        if (offset > storage_.size() || count > storage_.size() - offset)
            throw std::out_of_range("guest address outside mapping");
        return offset;
    }

    Address base_;
    std::span<std::byte> storage_;
};

} // namespace gt4::guest

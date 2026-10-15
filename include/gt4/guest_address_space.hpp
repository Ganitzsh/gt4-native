#pragma once
#include "gt4/guest_memory.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gt4::guest {

// Explicit, non-owning guest address-space mappings. Backing buffers must outlive
// this object. A single access must fit inside one mapping; no implicit MMIO.
class AddressSpace {
public:
    struct Region {
        Address base;
        std::span<std::byte> data;
        bool writable;
    };

    void map(Address base, std::span<std::byte> storage, bool writable = true) {
        constexpr std::uint64_t limit = std::uint64_t{1} << 32;
        const auto end = std::uint64_t(base) + storage.size();
        if (storage.empty() || end > limit)
            throw std::invalid_argument("invalid guest mapping size or range");
        for (const auto& region : regions_) {
            const auto other_end = std::uint64_t(region.base) + region.data.size();
            if (std::uint64_t(base) < other_end && std::uint64_t(region.base) < end)
                throw std::invalid_argument("overlapping guest mappings");
        }
        regions_.push_back({base, storage, writable});
        std::sort(regions_.begin(), regions_.end(), [](const auto& a, const auto& b) {
            return a.base < b.base;
        });
    }

    [[nodiscard]] std::size_t region_count() const noexcept { return regions_.size(); }

    [[nodiscard]] std::span<const std::byte> read(Address address, std::size_t count) const {
        const auto& region = find_region(address, count);
        return std::span<const std::byte>(region.data).subspan(
            std::size_t(std::uint64_t(address) - region.base), count);
    }

    [[nodiscard]] std::span<std::byte> write(Address address, std::size_t count) {
        auto& region = find_region(address, count);
        if (!region.writable) throw std::runtime_error("guest region is read-only");
        return region.data.subspan(std::size_t(std::uint64_t(address) - region.base), count);
    }

    [[nodiscard]] std::uint8_t read8(Address address) const {
        return std::to_integer<std::uint8_t>(read(address, 1)[0]);
    }

    [[nodiscard]] std::uint16_t read16_le(Address address) const {
        const auto b = read(address, 2);
        return std::uint16_t(std::to_integer<std::uint8_t>(b[0])) |
               (std::uint16_t(std::to_integer<std::uint8_t>(b[1])) << 8);
    }

    [[nodiscard]] std::uint32_t read32_le(Address address) const {
        const auto b = read(address, 4);
        return std::uint32_t(std::to_integer<std::uint8_t>(b[0])) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[1])) << 8) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[2])) << 16) |
               (std::uint32_t(std::to_integer<std::uint8_t>(b[3])) << 24);
    }

    void write8(Address address, std::uint8_t value) {
        write(address, 1)[0] = std::byte(value);
    }

    void write16_le(Address address, std::uint16_t value) {
        auto b = write(address, 2);
        for (unsigned i = 0; i < 2; ++i) b[i] = std::byte(value >> (8 * i));
    }

    void write32_le(Address address, std::uint32_t value) {
        auto b = write(address, 4);
        for (unsigned i = 0; i < 4; ++i) b[i] = std::byte(value >> (8 * i));
    }

private:
    [[nodiscard]] const Region& find_region(Address address, std::size_t count) const {
        for (const auto& region : regions_) {
            if (address < region.base) continue;
            const auto offset = std::uint64_t(address) - region.base;
            if (offset <= region.data.size() && count <= region.data.size() - offset)
                return region;
        }
        throw std::out_of_range("unmapped guest address or access crosses a mapping");
    }

    [[nodiscard]] Region& find_region(Address address, std::size_t count) {
        return const_cast<Region&>(std::as_const(*this).find_region(address, count));
    }

    std::vector<Region> regions_;
};

} // namespace gt4::guest

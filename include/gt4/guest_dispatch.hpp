#pragma once
#include "gt4/guest_memory.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace gt4::guest {
// The ee-gcc 2.96 vtable has 8-byte entries: {i16 delta, i16 index, u32 target}.
// Slot zero is RTTI; virtual method N is slot N+1.
struct VirtualCall {
    Address target;
    Address adjusted_this;
    std::int16_t index;
};

[[nodiscard]] inline Address checked_dispatch_address(std::uint64_t address) {
    if (address > std::numeric_limits<Address>::max())
        throw std::out_of_range("guest address overflow");
    return static_cast<Address>(address);
}

[[nodiscard]] inline VirtualCall resolve_virtual_call(const MemoryView& memory,
                                                        Address object,
                                                        std::uint32_t method_index) {
    const auto vptr = memory.read32_le(
        checked_dispatch_address(std::uint64_t(object) + 4));
    const auto slot = std::uint64_t(method_index) + 1;
    const auto entry64 = std::uint64_t(vptr) + slot * 8;
    if (entry64 > std::numeric_limits<Address>::max())
        throw std::out_of_range("vtable address overflow");
    const auto entry = static_cast<Address>(entry64);
    const auto delta = static_cast<std::int16_t>(memory.read16_le(entry));
    const auto index = static_cast<std::int16_t>(memory.read16_le(
        checked_dispatch_address(std::uint64_t(entry) + 2)));
    const auto target = memory.read32_le(
        checked_dispatch_address(std::uint64_t(entry) + 4));
    const auto adjusted = std::int64_t(object) + delta;
    if (adjusted < 0 || adjusted > std::numeric_limits<Address>::max())
        throw std::out_of_range("this adjustment overflow");
    return {target, static_cast<Address>(adjusted), index};
}
} // namespace gt4::guest

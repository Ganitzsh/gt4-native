#pragma once
#include "gt4/guest_memory.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace gt4::guest {
// Read-only view of the ee-gcc 2.96 copy-on-write std::string representation.
// A string object holds a 32-bit address of the first character; its
// 16-byte header precedes that address. No mutation/refcount operations here.
struct StringRep {
    std::int32_t length;
    std::int32_t capacity;
    std::int32_t references;
    std::int32_t selfish;
};

[[nodiscard]] inline StringRep read_string_rep(const MemoryView& memory, Address chars) {
    if (chars < 16) throw std::out_of_range("string header underflow");
    const auto base = Address(chars - 16);
    const auto length = static_cast<std::int32_t>(memory.read32_le(base));
    const auto capacity = static_cast<std::int32_t>(memory.read32_le(base + 4));
    const auto references = static_cast<std::int32_t>(memory.read32_le(base + 8));
    const auto selfish = static_cast<std::int32_t>(memory.read32_le(base + 12));
    if (length < 0 || capacity < length)
        throw std::invalid_argument("invalid guest string representation");
    if (std::uint64_t(chars) + std::uint64_t(length) >= (std::uint64_t{1} << 32))
        throw std::out_of_range("guest string address overflow");
    (void)memory.bytes(chars, std::size_t(length) + 1);
    if (memory.read8(Address(chars + length)) != 0)
        throw std::invalid_argument("guest string lacks terminator");
    return {length, capacity, references, selfish};
}

[[nodiscard]] inline Address read_string_pointer(const MemoryView& memory,
                                                 Address string_object) {
    return memory.read32_le(string_object);
}
} // namespace gt4::guest

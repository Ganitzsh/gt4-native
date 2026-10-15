#pragma once

#include "gt4/guest_address_space.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace gt4::guest {

// A typed guest address, never a host pointer. Loads and stores use the PS2's
// little-endian representation and pass through AddressSpace bounds/permission
// checks. This intentionally supports scalar integers only; guest structures
// need explicit layouts and accessors.
template <typename T>
class guest_ptr {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>,
                  "guest_ptr<T> currently supports non-bool integer scalars");
    static_assert(sizeof(T) <= sizeof(std::uint64_t), "guest scalar is wider than 64 bits");

public:
    constexpr guest_ptr() noexcept = default;
    explicit constexpr guest_ptr(Address address) noexcept : address_(address) {}

    [[nodiscard]] constexpr Address address() const noexcept { return address_; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return address_ == 0; }

    [[nodiscard]] T load(const AddressSpace& memory) const {
        check_dereferenceable();
        const auto bytes = memory.read(address_, sizeof(T));
        using Unsigned = std::make_unsigned_t<T>;
        Unsigned bits{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bits |= Unsigned(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8);
        return std::bit_cast<T>(bits);
    }

    void store(AddressSpace& memory, T value) const {
        check_dereferenceable();
        auto bytes = memory.write(address_, sizeof(T));
        using Unsigned = std::make_unsigned_t<T>;
        const auto bits = std::bit_cast<Unsigned>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = std::byte((bits >> (i * 8)) & Unsigned{0xFF});
    }

private:
    void check_dereferenceable() const {
        if (is_null()) throw std::out_of_range("cannot dereference null guest pointer");
        if (address_ % alignof(T) != 0)
            throw std::out_of_range("unaligned guest pointer access");
    }

    Address address_{};
};

} // namespace gt4::guest

#pragma once

#include "gt4/guest_address_space.hpp"
#include "gt4/guest_layout.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace gt4::guest {

// A typed 32-bit address, never a host pointer. Structures are layout descriptors;
// their fields and arrays are accessed individually through the checked memory.
template <typename T>
class guest_ptr {
    using Value = std::remove_cv_t<T>;
    static_assert(wire_layout<T>::size > 0 && wire_layout<T>::size <= (std::uint64_t{1} << 32));
    static_assert(wire_layout<T>::alignment > 0 &&
                  (wire_layout<T>::alignment & (wire_layout<T>::alignment - 1)) == 0);
    static_assert(wire_layout<T>::size % wire_layout<T>::alignment == 0);

public:
    constexpr guest_ptr() noexcept = default;
    explicit constexpr guest_ptr(Address address) noexcept : address_(address) {}

    [[nodiscard]] constexpr Address address() const noexcept { return address_; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return address_ == 0; }

    [[nodiscard]] Value load(const AddressSpace& memory) const requires(wire_scalar<T>) {
        check_dereferenceable();
        const auto bytes = memory.read(address_, sizeof(T));
        std::uint64_t bits{};
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bits |= std::uint64_t(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8);
        if constexpr (sizeof(T) == 1) return std::bit_cast<Value>(std::uint8_t(bits));
        else if constexpr (sizeof(T) == 2) return std::bit_cast<Value>(std::uint16_t(bits));
        else if constexpr (sizeof(T) == 4) return std::bit_cast<Value>(std::uint32_t(bits));
        else return std::bit_cast<Value>(bits);
    }

    void store(AddressSpace& memory, Value value) const requires(wire_scalar<T> && !std::is_const_v<T>) {
        check_dereferenceable();
        auto bytes = memory.write(address_, sizeof(T));
        std::uint64_t bits{};
        if constexpr (sizeof(T) == 1) bits = std::bit_cast<std::uint8_t>(value);
        else if constexpr (sizeof(T) == 2) bits = std::bit_cast<std::uint16_t>(value);
        else if constexpr (sizeof(T) == 4) bits = std::bit_cast<std::uint32_t>(value);
        else bits = std::bit_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = std::byte((bits >> (i * 8)) & std::uint64_t{0xFF});
    }

    [[nodiscard]] guest_ptr add(std::size_t elements) const {
        if (elements == 0) return *this;
        check_dereferenceable();
        const auto room = (std::uint64_t{0xFFFFFFFF} - address_) / wire_layout<T>::size;
        if (elements > room) throw std::out_of_range("guest pointer arithmetic overflow");
        return guest_ptr(Address(std::uint64_t(address_) + elements * wire_layout<T>::size));
    }

    template<class U> [[nodiscard]] auto field(std::size_t offset) const {
        check_dereferenceable();
        if (offset > wire_layout<T>::size || wire_layout<U>::size > wire_layout<T>::size - offset ||
            std::uint64_t(address_) + offset > 0xFFFFFFFFu)
            throw std::out_of_range("guest field outside its object");
        using Field = std::conditional_t<std::is_const_v<T>, std::add_const_t<U>, U>;
        return guest_ptr<Field>(Address(std::uint64_t(address_) + offset));
    }

    template<class U> [[nodiscard]] guest_ptr<U> load_pointer(const AddressSpace& memory) const
        requires(std::is_same_v<Value, Address>) {
        return guest_ptr<U>(load(memory));
    }

    template<class U> void store_pointer(AddressSpace& memory, guest_ptr<U> value) const
        requires(std::is_same_v<Value, Address> && !std::is_const_v<T>) {
        store(memory, value.address());
    }

private:
    void check_dereferenceable() const {
        if (is_null()) throw std::out_of_range("cannot dereference null guest pointer");
        if (address_ % wire_layout<T>::alignment != 0)
            throw std::out_of_range("unaligned guest pointer access");
    }

    Address address_{};
};

} // namespace gt4::guest

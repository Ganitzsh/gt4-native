#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace gt4::guest {

template<class T>
inline constexpr bool wire_scalar =
    (std::is_integral_v<std::remove_cv_t<T>> &&
     !std::is_same_v<std::remove_cv_t<T>, bool> && sizeof(T) <= 8) ||
    (std::is_floating_point_v<std::remove_cv_t<T>> &&
     std::numeric_limits<std::remove_cv_t<T>>::is_iec559 &&
     (sizeof(T) == 4 || sizeof(T) == 8));

// Structures declare their wire extent/alignment explicitly. Never use a host
// structure's sizeof/alignof as a substitute for the PS2 object representation.
template<class T, bool = wire_scalar<T>> struct wire_layout;
template<class T> struct wire_layout<T, true> {
    static constexpr std::size_t size = sizeof(T);
    static constexpr std::size_t alignment = sizeof(T);
};
template<class T> struct wire_layout<T, false> {
    static constexpr std::size_t size = T::wire_size;
    static constexpr std::size_t alignment = T::wire_alignment;
};

} // namespace gt4::guest

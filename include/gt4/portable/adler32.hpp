#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint32_t adler32(std::span<const std::byte> bytes) { std::uint32_t a=1,b=0; for(auto c:bytes) { a=(a+std::to_integer<std::uint8_t>(c))%65521u; b=(b+a)%65521u; } return (b<<16)|a; }
}

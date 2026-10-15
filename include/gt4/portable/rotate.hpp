#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t rotl32(std::uint32_t x,unsigned n) { n&=31; return n?((x<<n)|(x>>(32-n))):x; }
}

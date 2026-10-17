#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t byteswap32(std::uint32_t x) { return (x>>24)|((x>>8)&0xFF00)|((x<<8)&0xFF0000)|(x<<24); }
}

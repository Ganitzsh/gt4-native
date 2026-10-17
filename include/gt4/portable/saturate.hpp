#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint8_t saturate_u8(int v) { return v<0?0:v>255?255:static_cast<std::uint8_t>(v); }
}

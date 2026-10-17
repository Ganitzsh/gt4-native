#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint16_t u16le(std::span<const std::byte> b) { if(b.size()<2) throw std::out_of_range("u16"); return std::uint16_t(std::to_integer<std::uint8_t>(b[0])) | (std::uint16_t(std::to_integer<std::uint8_t>(b[1]))<<8); }
}

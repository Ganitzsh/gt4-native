#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint32_t u32le(std::span<const std::byte> b) { if(b.size()<4) throw std::out_of_range("u32"); std::uint32_t v=0; for(unsigned i=0;i<4;++i) v|=std::uint32_t(std::to_integer<std::uint8_t>(b[i]))<<(8*i); return v; }
}

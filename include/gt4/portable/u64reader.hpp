#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint64_t u64le(std::span<const std::byte> b) { if(b.size()<8) throw std::out_of_range("u64"); std::uint64_t v=0; for(unsigned i=0;i<8;++i) v|=std::uint64_t(std::to_integer<std::uint8_t>(b[i]))<<(8*i); return v; }
}

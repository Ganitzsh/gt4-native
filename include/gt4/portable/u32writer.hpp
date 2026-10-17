#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline void put_u32le(std::span<std::byte> b,std::uint32_t v) { if(b.size()<4) throw std::out_of_range("write u32"); for(unsigned i=0;i<4;++i) b[i]=std::byte(v>>(8*i)); }
}

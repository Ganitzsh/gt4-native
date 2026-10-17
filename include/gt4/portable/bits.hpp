#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t extract_bits(std::uint32_t v,unsigned shift,unsigned width) { if(shift>=32 || width==0 || width>32-shift) throw std::invalid_argument("bit field"); return (v>>shift)&(width==32?UINT32_MAX:((1u<<width)-1)); }
}

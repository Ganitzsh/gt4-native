#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::size_t bounded_cstrlen(std::span<const std::byte> b) { for(std::size_t i=0;i<b.size();++i) if(b[i]==std::byte{0}) return i; throw std::invalid_argument("unterminated string"); }
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint64_t align_up(std::uint64_t n, std::uint64_t a) { if(!a || (a&(a-1))) throw std::invalid_argument("alignment"); if(n>UINT64_MAX-(a-1)) throw std::overflow_error("alignment overflow"); return (n+a-1)&~(a-1); }
}

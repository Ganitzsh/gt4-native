#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t lcg_next(std::uint32_t s) { return s*1664525u+1013904223u; }
}

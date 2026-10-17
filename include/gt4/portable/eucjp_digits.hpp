#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr int eucjp_fullwidth_digit(std::uint8_t lead,std::uint8_t trail) { return lead==0xA3 && trail>=0xB0 && trail<=0xB9 ? int(trail-0xB0) : -1; }
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr bool pad_pressed(std::uint16_t active_low_state,std::uint16_t mask) { return (active_low_state&mask)==0; }
}

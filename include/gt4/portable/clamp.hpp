#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
template<class T> constexpr T clamp_value(T v,T lo,T hi) { if(hi<lo) throw std::invalid_argument("bounds"); return v<lo?lo:v>hi?hi:v; }
}

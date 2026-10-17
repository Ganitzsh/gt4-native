#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline float lerp_f32(float a,float b,float t) { return a+(b-a)*t; }
}

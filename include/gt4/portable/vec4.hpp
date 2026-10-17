#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
struct Vec4 { float x,y,z,w; }; inline Vec4 scale(Vec4 a,float s) { return {a.x*s,a.y*s,a.z*s,a.w*s}; }
}

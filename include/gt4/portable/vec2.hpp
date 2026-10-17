#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
struct Vec2 { float x,y; }; inline Vec2 add(Vec2 a,Vec2 b) { return {a.x+b.x,a.y+b.y}; }
}

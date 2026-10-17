#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr float from_fixed16(std::int32_t x) { return static_cast<float>(x)/65536.0f; }
}

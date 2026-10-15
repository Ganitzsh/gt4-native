#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint32_t fnv1a(std::span<const std::byte> bytes) { std::uint32_t h=2166136261u; for(auto b:bytes) h=(h^std::to_integer<std::uint8_t>(b))*16777619u; return h; }
}

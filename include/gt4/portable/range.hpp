#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr bool contains_range(std::uint64_t base,std::uint64_t size,std::uint64_t at,std::uint64_t count) { return at>=base && at-base<=size && count<=size-(at-base); }
}

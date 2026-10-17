#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::int32_t sign_extend(std::uint32_t v,unsigned width) { if(!width||width>32) throw std::invalid_argument("width"); if(width==32) return static_cast<std::int32_t>(v); const auto mask=(1u<<width)-1; v&=mask; return static_cast<std::int32_t>((v^(1u<<(width-1)))-(1u<<(width-1))); }
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
inline std::uint32_t crc32(std::span<const std::byte> bytes) { std::uint32_t c=~0u; for(auto b:bytes) { c^=std::to_integer<std::uint8_t>(b); for(int i=0;i<8;++i) c=(c>>1)^((c&1)?0xEDB88320u:0u); } return ~c; }
}

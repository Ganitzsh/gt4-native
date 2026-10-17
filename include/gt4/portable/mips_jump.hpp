#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t mips_jump_target(std::uint32_t pc,std::uint32_t instruction) { return ((pc+4)&0xF0000000u)|((instruction&0x03FFFFFFu)<<2); }
}

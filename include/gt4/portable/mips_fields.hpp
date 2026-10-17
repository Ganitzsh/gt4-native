#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
struct MipsFields { unsigned opcode,rs,rt,rd,shamt,funct; }; constexpr MipsFields decode_mips(std::uint32_t w) { return {w>>26,(w>>21)&31,(w>>16)&31,(w>>11)&31,(w>>6)&31,w&63}; }
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
namespace gt4::portable {
constexpr std::uint32_t mips_branch_target(std::uint32_t pc,std::int16_t offset) { return pc+4u+static_cast<std::uint32_t>(static_cast<std::int32_t>(offset)*4); }
}

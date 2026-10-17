#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace gt4::guest {
using Address = std::uint32_t;
struct Pointer { Address address; };
struct VtableEntry { std::int16_t this_delta; std::int16_t index; Address target; };
struct RefCounterLayout { std::uint32_t count; Pointer vptr; };
struct ObjectLayout { RefCounterLayout ref; std::uint8_t payload[8]; };
static_assert(sizeof(Pointer) == 4);
static_assert(sizeof(VtableEntry) == 8);
static_assert(offsetof(VtableEntry, target) == 4);
static_assert(sizeof(RefCounterLayout) == 8);
static_assert(sizeof(ObjectLayout) == 16);
static_assert(std::is_trivially_copyable_v<VtableEntry>);
} // namespace gt4::guest
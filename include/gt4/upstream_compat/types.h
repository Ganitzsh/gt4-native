#pragma once
// Minimal type aliases required by the two imported upstream translation units.
// This is a host compilation shim, not a complete PS2 SDK types.h.
#include <cstdint>
using s32 = std::int32_t;
using f32 = float;
static_assert(sizeof(s32) == 4);
static_assert(sizeof(f32) == 4);

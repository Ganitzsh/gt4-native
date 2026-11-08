#pragma once
// GT4_LOAD_STATS=1 (measurement, PLAN.md D10): the game's loads timed, one log line each (runtime/load_stats.cpp).
#include <cstdint>

namespace gt4::rt {

// A disc read: load_stats_begin before it (0 when off), load_stats_read after it with its bytes.
double load_stats_begin();
void load_stats_read(double started, uint32_t bytes);
// A frame the game drew (its kick, runtime/frame.cpp).
void load_stats_kick();
// Each VBlank (runtime/gs.cpp's frame hook): a load opens at the first VBlank with a read and closes, logged, after a
// second without one.
void load_stats_vblank(uint32_t vblank);

}  // namespace gt4::rt

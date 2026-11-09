#pragma once
// Reading game data that may lie outside the guest's RAM (a game array on a native thread's stack), as far as it is
// mapped: the graphics library's unpacks (native/pgl.cpp) and the frame's texture and model data (frame.cpp).
#include <cstddef>
#include <cstdint>

namespace gt4::rt {

// `bytes` from host address `src` into `out`, which the caller has zeroed, as far as they are mapped and readable:
// zeros past (a game array at the very top of a native stack is read past its end when a count is rounded up; on the
// PS2 other memory followed). The guest's RAM and the calling thread's committed stack are read at once; anything
// else asks the system region by region (VirtualQuery), as before.
void read_mapped_into(uint8_t *out, const uint8_t *src, size_t bytes);

// Since the start: reads outside the guest's RAM taken at once from the calling thread's stack, and those that
// asked the system (the watch line's, so the saving shows).
struct MappedReadCounts {
    uint64_t stack, asked;
};
MappedReadCounts mapped_read_counts();

}  // namespace gt4::rt

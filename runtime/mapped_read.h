#pragma once
// Reading game data that may lie outside the guest's RAM (a game array on a native thread's stack), as far as it is
// mapped: the graphics library's unpacks (native/pgl.cpp) and the frame's texture and model data (frame.cpp).
#include <cstddef>
#include <cstdint>

namespace gt4::rt {

// `bytes` from host address `src` into `out`, which the caller has zeroed, as far as they are mapped and readable:
// zeros past (a game array at the very top of a native stack is read past its end when a count is rounded up; on the
// PS2 other memory followed). The guest's RAM, the scratchpad and the calling thread's committed stack are read at
// once; anything else asks the system region by region (VirtualQuery), as before.
void read_mapped_into(uint8_t *out, const uint8_t *src, size_t bytes);

// Since the start, the reads outside the guest's RAM (the watch line's, so the saving shows): taken at once from
// the calling thread's stack or the scratchpad, and those that asked the system, starting on the calling thread's
// stack and running past its top, or elsewhere (another thread's stack, the frame buffers, the mirrors).
struct MappedReadCounts {
    uint64_t stack, scratchpad, askedPastTop, askedElsewhere;
};
MappedReadCounts mapped_read_counts();

}  // namespace gt4::rt

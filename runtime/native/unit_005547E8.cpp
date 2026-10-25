// Native code for the hand-written MMI assembly of decomp unit 005547E8 (the MPEG decoder).
// Written from the original instructions (gt4-decomp: tools/match.py asm ADDR).
#include "guest.h"

#include <cstdint>
#include <cstring>

extern "C" {

// 00563E10 (288 bytes): writes a decoded macroblock: the 384 signed 16-bit IDCT outputs at
// `blocks` (4 luma + 2 chroma 8x8 blocks) become 384 bytes at `dest`, each clamped to 0..255; with
// `add` set (a predicted block) each output is first added (16-bit, wrapping) to the byte already at
// dest. Both buffers are quadword-aligned (lq/sq).
void gt4_replace_func_00563E10(int dest, unsigned char *blocks, int add) {
    auto *out = static_cast<uint8_t *>(gt4::rt::guest_ptr(static_cast<uint32_t>(dest) & ~15u));
    const auto *in = reinterpret_cast<const int16_t *>(reinterpret_cast<uintptr_t>(blocks) & ~uintptr_t{15});
    for (int i = 0; i < 384; ++i) {
        int16_t v = in[i];
        if (add) v = static_cast<int16_t>(v + out[i]);
        out[i] = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

}  // extern "C"

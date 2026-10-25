#pragma once
// VU0 for the native game: the register file the game's VU0 code (inline asm translated by
// tools/game_cc.py, compat/game/gt4_asm.h) and the native replacements share, and the micro-mode
// runner behind vcallms.
#include <cstdint>

extern "C" {

// Layout shared with compat/game/gt4_asm.h (struct __gt4_vu0_state).
struct Gt4Vu0State {
    float vf[32][4];
    float acc[4];
    float q, i, p, pad0;
    uint32_t r;      // the random register: 0x3F800000 | 23 bits
    uint32_t clip;   // clipping flags (vi18)
    uint32_t vi[32]; // integer registers 0-15, control registers 16-31
};

extern Gt4Vu0State __gt4_vu0;

// vcallms: runs the VU0 microprogram at addr (bytes) to its end (the E bit), in the calling thread.
void __gt4_vu0_callms(unsigned int addr);
}

namespace gt4::native {

inline Gt4Vu0State &vu0() { return __gt4_vu0; }

// Field masks as the asm writes them (x 1, y 2, z 4, w 8).
inline void vu_set(float *d, const float *s, int mask) {
    for (int k = 0; k < 4; ++k)
        if (mask & (1 << k)) d[k] = s[k];
}

}  // namespace gt4::native

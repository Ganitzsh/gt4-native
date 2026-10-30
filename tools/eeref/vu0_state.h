#pragma once
// A VU0 register file, for the test tools only (docs/NATIVE_ONLY.md): the game has no VU0 model at run time. The
// original code eeref and the GT4_ORIG bridge run calls VU0 microprograms (vcallms), which run on this state
// (vu0_programs.cpp in C++, vu0_interp.cpp interpreted); GT4_RECORD files store the game's shared state in this
// layout (runtime/record.cpp, eeref.cpp NativeVu0).
#include <cstdint>

struct Gt4Vu0State {
    float vf[32][4];
    float acc[4];
    float q, i, p, pad0;
    uint32_t r;      // the random register: 0x3F800000 | 23 bits
    uint32_t clip;   // clipping flags (vi18)
    uint32_t vi[32]; // integer registers 0-15, control registers 16-31
};

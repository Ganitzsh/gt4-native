#pragma once
// The state GT4's functions share outside guest memory (docs/NATIVE_ONLY.md, layer 3). The original kept it in VU0:
// matrices and constants in its vector registers, a stack of matrices in its data memory, a random generator in its
// R register. Natively these are plain C++ data, read and written by the game's rewrites (game_src/, through
// compat/game/gt4_vec.h, which declares the same names) and the runtime's (runtime/native/).
#include <cstdint>

extern "C" {

// The 32 shared vectors (x, y, z, w): the graphics library's current matrix in 28..31, the view and projection
// matrices in 20..27, bone matrices and projection constants in 8..19, and in 1..7 the values a few helpers hand
// back (004A7438: sine and cosine in vector 1). Vector 0 is (0, 0, 0, 1).
extern float gt4_shared_vectors[32][4];

// The matrix stack: 004A53F8 pushes the current matrix (vectors 28..31) as four rows at gt4_matrix_stack_top,
// 004A5400 pops them. The top counts rows (16 bits, the stack wraps at 256 rows), as the original's stack pointer
// did: the scratchpad's three matrix sets keep their own top (004A5348) and start at rows 0, 0x40 and 0x50.
extern float gt4_matrix_stack[256][4];
extern uint16_t gt4_matrix_stack_top;

// The random generator of 004AC400 / 004AC41C: 0x3F800000 | 23 bits, a float in [1, 2).
extern uint32_t gt4_random_state;
}

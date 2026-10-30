// The state GT4's functions share outside guest memory (shared_state.h).
#include "shared_state.h"

extern "C" {
float gt4_shared_vectors[32][4] = {{0.0f, 0.0f, 0.0f, 1.0f}};
float gt4_matrix_stack[256][4];
uint16_t gt4_matrix_stack_top;
uint32_t gt4_random_state = 0x3F800000u;
}

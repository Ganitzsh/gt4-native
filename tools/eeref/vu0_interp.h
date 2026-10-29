#pragma once
// The VU0 micro-mode interpreter, a test reference (vu0_interp.cpp).
#include "../../runtime/native/vu0.h"

// vcallms: runs the micro image's program at addr on __gt4_vu0 and the VU0 memory at 0x11000000.
void vu0_interpret(unsigned int addr);
// Checks GT4's VU0 programs in C++ against the interpreter on `trials` random states each; returns how many
// programs differed (logged per program).
long vu0_fuzz(long trials);

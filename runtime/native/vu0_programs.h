#pragma once
// GT4's VU0 microprograms in C++ (vu0_programs.cpp).
#include "vu0.h"

namespace gt4::native {

// Runs the program GT4's VU0 image has at addr (a vcallms address, bytes) on s and VU0 data memory, as the
// micro image would; false for an address that is not one of the image's 14 programs.
bool vu0_program(unsigned int addr, Gt4Vu0State &s);

}  // namespace gt4::native

#pragma once
// GT4's VU0 microprograms in C++, in the VU's arithmetic (vu0_programs.cpp): a test reference for the original code
// the GT4_ORIG bridge runs (tools/eeref/orig_bridge.cpp), not part of the game.
#include "vu0_state.h"

namespace vu0ref {

// Runs the program GT4's VU0 image has at addr (a vcallms address, bytes) on s and the 4 KB of VU0 data memory at
// data, as the micro image would; false for an address that is not one of the image's 14 programs.
bool vu0_program(unsigned int addr, Gt4Vu0State &s, void *data);

}  // namespace vu0ref

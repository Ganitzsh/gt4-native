// VU0 on the native game side: the register file of the whole program, and vcallms.
//
// The game's VU0 code reaches this file two ways: macro-mode instructions in inline asm, which
// tools/game_cc.py translates into C over __gt4_vu0 (compat/game/gt4_asm.h), and native
// replacements (runtime/native/*.cpp) of the hand-written VU0 helpers. VU0 microprograms
// (vcallms ADDR) are GT4's 14 programs written in C++ (vu0_programs.cpp), checked bit for bit against
// the micro-mode interpreter that used to run them here (now tools/eeref/vu0_interp.cpp, a test tool).
#include "vu0.h"
#include "vu0_programs.h"

#include "log.h"

extern "C" {
Gt4Vu0State __gt4_vu0 = {
    {{0.0f, 0.0f, 0.0f, 1.0f}},  // vf0 = (0, 0, 0, 1)
    {0, 0, 0, 0}, 0.0f, 0.0f, 0.0f, 0.0f, 0x3F800000u, 0, {0},
};
}

extern "C" void __gt4_vu0_callms(unsigned int addr) {
    if (!gt4::native::vu0_program(addr, __gt4_vu0))
        gt4::rt::fatal("vcallms %04x: not one of GT4's VU0 programs (runtime/native/vu0_programs.cpp)", addr);
}

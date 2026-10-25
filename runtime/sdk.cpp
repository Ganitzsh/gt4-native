// Replacements of Sony SDK functions linked in the image (config/sdk_functions.tsv in gt4-decomp):
// a function named gt4_replace_<SDK name> takes over that function's address (tools/game_link.py).
#include "kernel.h"
#include "log.h"

#include <cstdint>

using namespace gt4::rt;

// diei.o: DIntr/EIntr disable/enable interrupts (di/ei on the EE) and return the previous state.
extern "C" int gt4_replace_DIntr() { return kernel_di(); }
extern "C" int gt4_replace_func_005B72F8() { return kernel_ei(); }  // EIntr (diei.o, after DIntr)

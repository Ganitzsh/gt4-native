#pragma once
// A wheel's input as the Driving Force Pro GT4 sees through LGDEV (PLAN.md D7; docs/modern/spikes/R15-lgdev-effects.md
// "Input"): the wheel's pad input through its profile's bindings (D13), before D12's response (GT4 applies its own),
// to GT4_REC_WHEEL's fields. No SDL here: tests/input_wheel_dfp.cpp.
//
//   steering   the left stick's X, full lock -32768..32767
//   pedal[0]   R2's pressure, pedal[1] L2's (a wheel's pedals sit on the triggers by default: the profile binds
//              them, :full for one that rests at an end), 0..65535. Which of GT4's two pedal axes accelerates isn't
//              settled by the code: D7's bench check settles it, and this order follows.
//   hat        the D-pad, 0-7 from up clockwise, 8 none
//   buttons    the other buttons in the DFP's bits 16-29, the inverse of func_005665E8 (GT4's remap to its pad's
//              bits): Cross 16, Square 17, Circle 18, Triangle 19, Start 20, Select 21, L1 22, R1 23, L3 26, R3 27.
//              R2 and L2 are the pedals here, so their button bits (24, 25) are never set. Bits 28 and 29 (GT4's
//              0x20000000 and 0x10000000) have no DualShock 2 button and are never set either.
#include "gt4/link/protocol.h"
#include "ps2_pad.h"

#include <cstdint>

namespace gt4::input {

// GT4's own remap of the DFP's buttons to its pad's bits (func_005665E8, the decomp's matched C): the test checks the
// inverse against it.
uint32_t gt4DfpRemap(uint32_t dfpButtons);

// The wheel's pad input to GT4_REC_WHEEL (index 0, connected 1).
Gt4LinkWheel dfpWheel(const ps2::PadInput& in);

// A wheel GT4 sees as connected but untouched (a settings page open, or the pad held back after one).
Gt4LinkWheel dfpWheelNeutral();

}  // namespace gt4::input

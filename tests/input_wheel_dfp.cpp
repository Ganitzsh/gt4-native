// platform/input/wheel_dfp.cpp (PLAN.md D7): a wheel's pad input to the Driving Force Pro GT4 reads. Each mapped
// button, sent alone, comes back through GT4's own remap (func_005665E8) as that DualShock 2 button's bit; the pedals
// set no button bit; the steering, the pedals and the hat over their ranges; neutral is connected, centred, no hat.
#include "wheel_dfp.h"

#include <cstdio>

using namespace gt4;
using namespace gt4::input;

namespace {

int failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

ps2::PadInput pressed(ps2::PadButton b) {
    ps2::PadInput in;
    in.button[b] = 1.0f;
    return in;
}

}  // namespace

int main() {
    // every button GT4 remaps to its pad's DualShock 2 bits comes back as that button's own bit (bit i = PadButton i)
    const ps2::PadButton mapped[] = {ps2::PadSelect, ps2::PadL3, ps2::PadR3, ps2::PadStart, ps2::PadTriangle,
                                     ps2::PadCircle, ps2::PadCross, ps2::PadSquare};
    for (ps2::PadButton b : mapped) {
        const Gt4LinkWheel w = dfpWheel(pressed(b));
        char what[64];
        std::snprintf(what, sizeof what, "button %u through GT4's remap", static_cast<unsigned>(b));
        expect(gt4DfpRemap(w.buttons) == (1u << b), what);
    }
    // L1 and R1: the DFP bits GT4 remaps to its own bits 26 and 27 (not a DualShock 2 bit)
    expect(gt4DfpRemap(dfpWheel(pressed(ps2::PadL1)).buttons) == 0x4000000, "L1: GT4's bit 26");
    expect(gt4DfpRemap(dfpWheel(pressed(ps2::PadR1)).buttons) == 0x8000000, "R1: GT4's bit 27");
    // the pedals are axes, not buttons
    expect(dfpWheel(pressed(ps2::PadR2)).buttons == 0 && dfpWheel(pressed(ps2::PadL2)).buttons == 0,
           "R2 and L2 set no button bit");
    expect(dfpWheel(pressed(ps2::PadR2)).pedal[0] == 65535 && dfpWheel(pressed(ps2::PadL2)).pedal[1] == 65535,
           "R2 full is pedal 0 full, L2 full is pedal 1 full");
    {
        ps2::PadInput in;
        in.button[ps2::PadR2] = 0.5f;
        expect(dfpWheel(in).pedal[0] == 32768 && dfpWheel(in).pedal[1] == 0, "half a pedal");
    }
    // the steering over its range
    {
        ps2::PadInput in;
        in.leftX = -1.0f;
        expect(dfpWheel(in).steering == -32767, "full left");
        in.leftX = 1.0f;
        expect(dfpWheel(in).steering == 32767, "full right");
        in.leftX = 0.0f;
        expect(dfpWheel(in).steering == 0, "centre");
        in.leftX = -1.00003f;
        expect(dfpWheel(in).steering == -32768, "SDL's -32768 stays in range");
    }
    // the hat: 0 up, clockwise; none 8; opposite directions cancel
    {
        auto hat = [](bool up, bool right, bool down, bool left) {
            ps2::PadInput in;
            in.button[ps2::PadUp] = up, in.button[ps2::PadRight] = right;
            in.button[ps2::PadDown] = down, in.button[ps2::PadLeft] = left;
            return dfpWheel(in).hat;
        };
        expect(hat(0, 0, 0, 0) == 8 && hat(1, 0, 0, 0) == 0 && hat(1, 1, 0, 0) == 1 && hat(0, 1, 0, 0) == 2 &&
                   hat(0, 1, 1, 0) == 3 && hat(0, 0, 1, 0) == 4 && hat(0, 0, 1, 1) == 5 && hat(0, 0, 0, 1) == 6 &&
                   hat(1, 0, 0, 1) == 7,
               "the hat's eight directions");
        expect(hat(1, 0, 1, 0) == 8 && hat(0, 1, 0, 1) == 8, "opposites cancel");
        expect(dfpWheel(pressed(ps2::PadUp)).buttons == 0, "the D-pad sets no button bit");
    }
    const Gt4LinkWheel n = dfpWheelNeutral();
    expect(n.index == 0 && n.connected == 1 && n.steering == 0 && n.pedal[0] == 0 && n.pedal[1] == 0 && n.hat == 8 &&
               n.buttons == 0, "neutral: connected, centred, nothing pressed");
    if (failures) return 1;
    std::printf("input_wheel_dfp: all checks passed\n");
    return 0;
}

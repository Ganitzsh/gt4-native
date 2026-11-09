#include "wheel_dfp.h"

#include <algorithm>
#include <cmath>

namespace gt4::input {

namespace {

// The DFP's bit for each DualShock 2 button GT4's remap gives (0: none), by ps2::PadButton.
constexpr uint32_t kDfpBit[ps2::PadButtonCount] = {
    0x200000,   // Select
    0x4000000,  // L3
    0x8000000,  // R3
    0x100000,   // Start
    0, 0, 0, 0, // the D-pad: the hat
    0,          // L2: a pedal
    0,          // R2: a pedal
    0x400000,   // L1
    0x800000,   // R1
    0x80000,    // Triangle
    0x40000,    // Circle
    0x10000,    // Cross
    0x20000,    // Square
};

bool down(const ps2::PadInput& in, ps2::PadButton b) { return in.button[b] > 0.5f; }

}  // namespace

uint32_t gt4DfpRemap(uint32_t in) {
    uint32_t out = 0;
    if (in & 0x10000) out = 0x4000;
    if (in & 0x20000) out |= 0x8000;
    if (in & 0x40000) out |= 0x2000;
    if (in & 0x80000) out |= 0x1000;
    if (in & 0x800000) out |= 0x8000000;
    if (in & 0x400000) out |= 0x4000000;
    if (in & 0x2000000) out |= 0x200;
    if (in & 0x1000000) out |= 0x100;
    if (in & 0x200000) out |= 0x1;
    if (in & 0x100000) out |= 0x8;
    if (in & 0x8000000) out |= 0x4;
    if (in & 0x4000000) out |= 0x2;
    if (in & 0x10000000) out |= 0x20000000;
    if (in & 0x20000000) out |= 0x10000000;
    return out;
}

Gt4LinkWheel dfpWheel(const ps2::PadInput& in) {
    Gt4LinkWheel w = dfpWheelNeutral();
    w.steering = static_cast<int16_t>(std::clamp<long>(std::lround(in.leftX * 32767.0f), -32768, 32767));
    const auto pedal = [](float v) { return static_cast<uint16_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65535.0f)); };
    w.pedal[0] = pedal(in.button[ps2::PadR2]);
    w.pedal[1] = pedal(in.button[ps2::PadL2]);
    const bool up = down(in, ps2::PadUp), right = down(in, ps2::PadRight), dn = down(in, ps2::PadDown),
               left = down(in, ps2::PadLeft);
    // 0 up, 1 up-right, 2 right, 3 down-right, 4 down, 5 down-left, 6 left, 7 up-left; opposite directions cancel
    const int x = (right ? 1 : 0) - (left ? 1 : 0), y = (dn ? 1 : 0) - (up ? 1 : 0);
    static constexpr uint8_t kHat[3][3] = {{7, 0, 1}, {6, 8, 2}, {5, 4, 3}};   // [y + 1][x + 1]
    w.hat = kHat[y + 1][x + 1];
    for (uint32_t b = 0; b < ps2::PadButtonCount; ++b)
        if (kDfpBit[b] && down(in, static_cast<ps2::PadButton>(b))) w.buttons |= kDfpBit[b];
    return w;
}

Gt4LinkWheel dfpWheelNeutral() {
    Gt4LinkWheel w{};
    w.index = 0;
    w.connected = 1;
    w.hat = 8;
    return w;
}

}  // namespace gt4::input

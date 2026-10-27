#pragma once
// The pad state the game reads through libpad2 (DualShock 2 layout, 18 bytes; docs/IOP_SERVICES.md
// section 7), built from a host controller or the keyboard and sent as GT4_REC_PAD.
//
//   bytes 0-1   digital buttons, active low, bit = libpad2 index: 0 Select, 1 L3, 2 R3, 3 Start, 4 Up,
//               5 Right, 6 Down, 7 Left, 8 L2, 9 R2, 10 L1, 11 R1, 12 Triangle, 13 Circle, 14 Cross, 15 Square
//   bytes 2-5   sticks RX, RY, LX, LY: 0x00 left/up, 0x80 centre, 0xFF right/down
//   bytes 6-17  pressures 0x00 (released) .. 0xFF: Right, Left, Up, Down, Triangle, Circle, Cross, Square,
//               L1, R1, L2, R2
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gt4::ps2 {

enum PadButton : uint32_t {
    PadSelect = 0, PadL3, PadR3, PadStart, PadUp, PadRight, PadDown, PadLeft,
    PadL2, PadR2, PadL1, PadR1, PadTriangle, PadCircle, PadCross, PadSquare, PadButtonCount
};

struct PadInput {
    // Pressure of each button, 0 (released) .. 1 (fully pressed); digital inputs use 0 or 1.
    float button[PadButtonCount] = {};
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0;   // -1 .. 1, +y down
};

// -1 -> 0x00, 0 -> 0x80, +1 -> 0xFF.
inline uint8_t padAxis(float v) {
    const float clamped = std::clamp(v, -1.0f, 1.0f);
    const float scaled = 128.0f + clamped * (clamped < 0.0f ? 128.0f : 127.0f);
    return static_cast<uint8_t>(std::clamp(std::lround(scaled), 0L, 255L));
}

inline uint8_t padPressure(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f), 0L, 255L));
}

// A digital button reads as pressed from a quarter of its travel (analogue triggers).
inline void encodePad(const PadInput& in, uint8_t out[18]) {
    uint16_t released = 0xFFFF;
    for (uint32_t b = 0; b < PadButtonCount; ++b)
        if (in.button[b] >= 0.25f) released = static_cast<uint16_t>(released & ~(1u << b));
    out[0] = static_cast<uint8_t>(released & 0xFF);
    out[1] = static_cast<uint8_t>(released >> 8);
    out[2] = padAxis(in.rightX);
    out[3] = padAxis(in.rightY);
    out[4] = padAxis(in.leftX);
    out[5] = padAxis(in.leftY);
    static const PadButton pressureOrder[12] = {PadRight, PadLeft, PadUp, PadDown, PadTriangle, PadCircle,
                                                PadCross, PadSquare, PadL1, PadR1, PadL2, PadR2};
    for (uint32_t k = 0; k < 12; ++k) out[6 + k] = padPressure(in.button[pressureOrder[k]]);
}

}  // namespace gt4::ps2

#pragma once
// input.trigger_travel = full (docs/modern/spikes/D1-bindings.md): GT4 turns a DualShock 2 pressure p into
// accelerator as min(2p * 100 / 35, 256) and brake as min(2p * 100 / 65, 256), so accelerate is full from p = 45
// and brake from p = 84. A trigger that sends its travel linearly (0..255) is then full at 18 % and 33 % of it.
// `full` rescales the pressure of the buttons GT4 has accelerate and brake on to 0..45 and 0..84 (255 at full
// travel), so the whole travel is used. The pressed bits are left alone (the host sets them from the travel).
#include <cstdint>

namespace gt4::rt::pad {

// GT4's DualShock 2 button functions (the BUTTON_* table at 0x00622E28): accelerate and brake.
constexpr int8_t kAccelerate = 4, kBrake = 5;
// The pressure byte (offset 6 + k of the 18-byte frame) of each button in GT4's key order (0x00623390: LEFT RIGHT UP
// DOWN TRIANGLE CIRCLE CROSS SQUARE L1 R1 L2 R2 L3 R3 START SELECT); -1: the button has none.
constexpr int kPressureOf[16] = {1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1};
// GT4's default DualShock 2 assignment (the reset, func_0043B808), in that order: Cross accelerate, Square brake.
constexpr int8_t kDefaultButtons[16] = {0, 1, 16, -1, 7, 6, 4, 5, 10, 8, 2, 3, -1, 15, 17, 9};
// The pressure from which GT4 gives full accelerate and full brake.
constexpr uint8_t kAccelerateFull = 45, kBrakeFull = 84;

// p (0..255, linear in the travel) to 0..full, and 255 at 255: full only at the end of the travel (from about 99 %).
constexpr uint8_t rescale(uint8_t p, uint8_t full) {
    return p == 255 ? uint8_t(255) : static_cast<uint8_t>((unsigned(p) * full + 127) / 255);
}

// Rescales, in an 18-byte scePad2Read frame, the pressures of the buttons that `buttons` (GT4's assignment, in its
// key order) gives accelerate or brake. Returns how many bytes it rescaled.
inline int rescale_pedals(uint8_t frame[18], const int8_t buttons[16]) {
    int n = 0;
    for (int key = 0; key < 16; ++key) {
        const int k = kPressureOf[key];
        if (k < 0) continue;
        if (buttons[key] == kAccelerate) frame[6 + k] = rescale(frame[6 + k], kAccelerateFull), ++n;
        else if (buttons[key] == kBrake) frame[6 + k] = rescale(frame[6 + k], kBrakeFull), ++n;
    }
    return n;
}

}  // namespace gt4::rt::pad
